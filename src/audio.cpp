#include "audio.h"
#include "config.h"
#include <Audio.h> // Library ESP32-audioI2S
#include <LittleFS.h>

namespace
{
    // Buat object audio di dalam anonymous namespace agar terisolasi
    Audio audio;
    bool isLittleFsReady = false;
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

    audio.setVolume(21);

    Serial.println("[AUDIO] MAX98357A siap.");
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
    audio.connecttoFS(LittleFS, path);
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
    Serial.print("[AUDIO END] Selesai memutar: ");
    Serial.println(info);
}