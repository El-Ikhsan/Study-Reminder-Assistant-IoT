#include "audio.h"
#include "config.h"
#include <Audio.h> // Library ESP32-audioI2S
#include <LittleFS.h>
#include <driver/i2s.h>
#include <math.h>
#include <string.h>

namespace
{
    // Buat object audio di dalam anonymous namespace agar terisolasi
    Audio audio;
    bool isLittleFsReady = false;
    bool isStreamReady = false;
    bool isSyncwordReady = false;
    unsigned long lastSfxStartMs = 0;
    bool isTypingSfxBusy = false;
    unsigned long typingSfxStartMs = 0;
    constexpr unsigned long TYPING_SFX_BUSY_TIMEOUT_MS = 90;
    unsigned long lastCodeClickMs = 0;
    int16_t typingPcmCache[512]; // 16ms audio @ 16kHz (Stereo)
    bool isPcmCached = false;
    constexpr int SYNC_DELAY_MS = 60;
    constexpr int SYNC_SAMPLES = (16000 * SYNC_DELAY_MS) / 1000;
    constexpr size_t SYNC_BYTES = SYNC_SAMPLES * 4; // 2 channel * 2 byte (16-bit)

    int16_t soundBuffer[SYNC_SAMPLES * 2];   // Berisi: Klik + Senyap
    int16_t silenceBuffer[SYNC_SAMPLES * 2]; // Berisi: Senyap Total
    bool isSyncCached = false;

}

void playTypingSync(bool withSound)
{
    // 1. Siapkan isi buffer SATU KALI SAJA saat pertama dipanggil
    if (!isSyncCached)
    {
        memset(soundBuffer, 0, SYNC_BYTES);
        memset(silenceBuffer, 0, SYNC_BYTES);

        // Generate suara ketikan hanya di 15ms pertama dari soundBuffer
        int clickSamples = (16000 * 15) / 1000;
        constexpr float twoPi = 6.28318530718f;
        for (int i = 0; i < clickSamples; ++i)
        {
            float t = (float)i / 16000.0f;
            float p = (float)i / (float)clickSamples;
            float attack = (p < 0.20f) ? (p / 0.20f) : 1.0f;
            float decay = 1.0f - p;
            float env = attack * decay * decay;
            float s = sinf(twoPi * 1250.0f * t) * env;
            int16_t v = (int16_t)(s * 4000.0f);

            soundBuffer[i * 2] = v;     // Kiri
            soundBuffer[i * 2 + 1] = v; // Kanan
        }
        isSyncCached = true;
    }

    size_t written = 0;

    // ✨ THE MAGIC: portMAX_DELAY
    // CPU akan tertahan di baris ini TEPAT SELAMA 60ms, tidak kurang tidak lebih,
    // menunggu I2S Hardware memutar isi buffer sampai tuntas.
    if (withSound)
    {
        i2s_write((i2s_port_t)I2S_NUM_0, soundBuffer, SYNC_BYTES, &written, portMAX_DELAY);
    }
    else
    {
        i2s_write((i2s_port_t)I2S_NUM_0, silenceBuffer, SYNC_BYTES, &written, portMAX_DELAY);
    }
}

void playTypingCodeClick()
{
    if (!isPcmCached)
    {
        constexpr float twoPi = 6.28318530718f;
        for (int i = 0; i < 256; ++i)
        {
            float t = (float)i / 16000.0f;
            float p = (float)i / 256.0f;
            float attack = (p < 0.20f) ? (p / 0.20f) : 1.0f;
            float decay = 1.0f - p;
            float env = attack * decay * decay;
            float s = sinf(twoPi * 1250.0f * t) * env;
            int16_t v = (int16_t)(s * 4000.0f);

            typingPcmCache[i * 2] = v;     // Kiri
            typingPcmCache[i * 2 + 1] = v; // Kanan
        }
        isPcmCached = true;
    }

    size_t written = 0;
    // Tembak 16ms langsung! Pipa I2S akan selalu kosong saat huruf berikutnya datang.
    i2s_write((i2s_port_t)I2S_NUM_0, (const char *)typingPcmCache, sizeof(typingPcmCache), &written, portMAX_DELAY);
}

void initAudio()
{
    Serial.println("[AUDIO] Menginisialisasi I2S MAX98357A...");

    // Coba mount normal dulu agar tidak selalu trigger format saat boot.
    if (!LittleFS.begin(false))
    {
        Serial.println("[AUDIO] Mount LittleFS gagal. Mencoba format ulang...");

        if (!LittleFS.format())
        {
            Serial.println("[ERROR] Format LittleFS gagal!");
        }
        else if (!LittleFS.begin(false))
        {
            Serial.println("[ERROR] Mount LittleFS tetap gagal setelah format!");
        }
        else
        {
            isLittleFsReady = true;
            Serial.println("[AUDIO] LittleFS berhasil dipulihkan.");
        }
    }
    else
    {
        isLittleFsReady = true;
    }

    // Set pin I2S (BCLK, LRC, DOUT)
    audio.setPinout(RinchanConfig::Audio::I2S_BCLK,
                    RinchanConfig::Audio::I2S_LRC,
                    RinchanConfig::Audio::I2S_DOUT);

    Serial.println("[AUDIO] MAX98357A siap.");
}

void playAudioSFX(const char *path)
{
    // 1. Keamanan Dasar
    if (!isLittleFsReady)
        return;
    if (!LittleFS.exists(path))
        return;

    const bool isTypingSfx = (strcmp(path, "/typing_1.wav") == 0) ||
                             (strcmp(path, "/typing_2.wav") == 0);

    // Cegah trigger terlalu rapat agar SFX pendek tidak terus terpotong.
    const unsigned long now = millis();
    // typing_2.wav dari log berdurasi ~95ms, jadi cooldown harus di atas itu agar tidak kepotong.
    const unsigned long minRetriggerMs = isTypingSfx ? 120UL : 45UL;
    if (now - lastSfxStartMs < minRetriggerMs)
        return;

    // Untuk typing, jangan retrigger saat blip sebelumnya masih aktif.
    if (isTypingSfx)
    {
        if (isTypingSfxBusy && (now - typingSfxStartMs < TYPING_SFX_BUSY_TIMEOUT_MS))
            return;

        // Timeout fail-safe jika callback EOF terlewat.
        if (isTypingSfxBusy && (now - typingSfxStartMs >= TYPING_SFX_BUSY_TIMEOUT_MS))
            isTypingSfxBusy = false;
    }

    // 2. Untuk SFX umum: hentikan dulu audio aktif. Untuk typing pendek: biarkan natural.
    if (!isTypingSfx)
        audio.stopSong();

    // Reset flag (opsional, tapi bagus untuk mencegah bug library)
    isStreamReady = false;
    isSyncwordReady = false;

    // 3. LANGSUNG PUTAR TANPA WARMUP!
    audio.connecttoFS(LittleFS, path);
    lastSfxStartMs = now;

    if (isTypingSfx)
    {
        isTypingSfxBusy = true;
        typingSfxStartMs = now;
    }

    // Beri kesempatan decoder mulai output supaya suara tidak hanya terdengar di trigger terakhir.
    const unsigned long primeStart = millis();
    const unsigned long primeMs = isTypingSfx ? 0UL : 8UL;
    while (millis() - primeStart < primeMs)
    {
        audio.loop();
        delay(1);
    }

    // (Boleh di-comment jika Serial Monitor terlalu penuh karena ngetik)
    // Serial.printf("[AUDIO SFX] Tembakan Cepat: %s\n", path);
}

void playAudioLocal(const char *path)
{
    if (!isLittleFsReady)
    {
        Serial.println("[AUDIO] LittleFS belum siap. Lewati pemutaran lokal.");
        return;
    }

    if (LittleFS.totalBytes() == 0 || LittleFS.usedBytes() == 0)
    {
        Serial.println("[AUDIO] LittleFS kosong. Upload filesystem image (uploadfs) dulu.");
        return;
    }

    if (!LittleFS.exists(path))
    {
        Serial.printf("[AUDIO] File startup tidak ada: %s\n", path);
        Serial.println("[AUDIO] File mungkin hilang setelah format recovery. Jalankan uploadfs.");
        return;
    }

    audio.stopSong();
    isStreamReady = false;
    isSyncwordReady = false;
    audio.connecttoFS(LittleFS, path);

    // Tunggu decoder menemukan syncword agar startup tidak patah di awal.
    const unsigned long warmupStartMs = millis();
    while (!isSyncwordReady && (millis() - warmupStartMs < 2000))
    {
        audio.loop();
        delay(5);
    }

    if (!isSyncwordReady)
    {
        Serial.println("[AUDIO] Warning: syncword belum terdeteksi saat warmup startup.");
    }

    Serial.printf("[AUDIO] Memutar MP3 Lokal: %s\n", path);
}

void setVolumePercent(int percent)
{
    // 1. Kunci angka agar tidak tembus di bawah 0 atau di atas 100
    percent = constrain(percent, 0, 100);

    // 2. Konversi skala 0-100 menjadi 0-21 secara otomatis
    int mappedVolume = map(percent, 0, 100, 0, 21);

    // 3. Kirim angka hasil terjemahan ke library
    audio.setVolume(mappedVolume);

    Serial.printf("[AUDIO] Volume diset ke %d%% (Level Hardware: %d/21)\n", percent, mappedVolume);
}

void audioLoop()
{
    // WAJIB dipanggil terus-menerus di void loop() agar lagu tidak putus-putus
    audio.loop();
}

void playAudioUrl(const String &url)
{
    if (url.isEmpty())
        return;

    Serial.println("[AUDIO] Mengunduh dan memutar: " + url);
    // connecttohost akan otomatis memulai streaming MP3/WAV dari URL
    audio.connecttohost(url.c_str());
}

void stopAudio()
{
    audio.stopSong();
    Serial.println("[AUDIO] Pemutaran dihentikan.");
}

// ==========================================
// 🐛 CALLBACK DEBUGGING DARI LIBRARY AUDIO
// ==========================================
// Fungsi-fungsi di bawah ini akan dipanggil otomatis oleh library
// untuk memberikan informasi status ke Serial Monitor.

void audio_info(const char *info)
{
    if (info != nullptr)
    {
        if (strstr(info, "stream ready") != nullptr)
        {
            isStreamReady = true;
        }

        if (strstr(info, "syncword found") != nullptr)
        {
            isSyncwordReady = true;
        }
    }

    Serial.print("[AUDIO INFO] ");
    Serial.println(info);
}

void audio_showstation(const char *info)
{
    Serial.print("[AUDIO STATION] ");
    Serial.println(info);
}

void audio_eof_mp3(const char *info)
{
    if (info != nullptr)
    {
        if (strstr(info, "typing_1.wav") != nullptr || strstr(info, "typing_2.wav") != nullptr)
        {
            isTypingSfxBusy = false;
        }
    }

    Serial.print("[AUDIO END] Selesai memutar: ");
    Serial.println(info);
}