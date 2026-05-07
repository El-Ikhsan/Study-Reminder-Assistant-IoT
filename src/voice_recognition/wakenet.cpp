#include "wakenet.h"
#include <driver/i2s.h>
#include <math.h>
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_log.h"
#include "model_path.h"
#include "esp_wn_models.h"
#include "ui/display.h"
#include "audio/sound_manager.h"
#include "audio/audio.h"
#include "features/general.h"
#include "config.h"
// Fungsi jembatan C
extern "C" afe_config_t get_default_afe_config();

static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;
volatile int currentNoiseLevel = 30;

// Flag: true saat speaker sedang memutar audio → mic dikunci agar tidak self-feedback
volatile bool isSpeakerPlaying = false;

void setMicMuted(bool muted)
{
    isSpeakerPlaying = muted;
}

// ==========================================
// 📥 TASK 1: PENYEDOT SUARA & VU METER
// ==========================================
void audio_feed_task(void *arg)
{
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);

    // Kita paksa baca 32-bit (4 bytes per sample) karena INMP441 butuh detak 32-bit
    size_t expected_bytes = audio_chunksize * sizeof(int32_t);
    int32_t *i2s_buff = (int32_t *)malloc(expected_bytes);
    int16_t *mono_feed = (int16_t *)malloc(audio_chunksize * sizeof(int16_t));

    if (i2s_buff == NULL || mono_feed == NULL)
    {
        Serial.println("[ERR] Kehabisan RAM untuk Mikrofon!");
        vTaskDelete(NULL);
        return;
    }

    // ✨ FIX FINAL: Mulai dari angka ambang sunyi natural (45 dB) bukan 30 dB
    float smoothedDb = 45.0f;
    float sumRmsWindow = 0.0f; // Kumpulkan rata-ratanya, BUKAN puncaknya!
    int chunkCounter = 0;
    const int WINDOW_SIZE = 16;

    while (true)
    {
        size_t bytes_read = 0;
        i2s_read(I2S_NUM_1, i2s_buff, expected_bytes, &bytes_read, portMAX_DELAY);

        if (bytes_read == expected_bytes)
        {
            int64_t sum = 0;
            for (int i = 0; i < audio_chunksize; i++)
            {
                // ✨ FIX 1: GESER 16 BIT! Mengembalikan volume ke normal
                int16_t s16 = (int16_t)(i2s_buff[i] >> 16);
                mono_feed[i] = s16;
                sum += s16;
            }

            if (isSpeakerPlaying)
            {
                // ✨ FIX FINAL: Kunci ke batas ambang hening alamiah saat speaker bicara
                smoothedDb = (smoothedDb * 0.6f) + (45.0f * 0.4f);
                currentNoiseLevel = constrain((int)smoothedDb, 45, 120);
                afe_handle->feed(afe_data, mono_feed);
                continue;
            }

            // Hapus DC Offset
            float mean = (float)sum / audio_chunksize;
            double sumSquares = 0;
            for (int i = 0; i < audio_chunksize; i++)
            {
                float centered = (float)mono_feed[i] - mean;
                sumSquares += centered * centered;
            }
            double rms = sqrt(sumSquares / audio_chunksize);

            // ✨ FIX 2: JUMLAHKAN UNTUK RATA-RATA, BUKAN MENCARI PEAK
            sumRmsWindow += (float)rms;
            chunkCounter++;

            if (chunkCounter >= WINDOW_SIZE)
            {
                float avgRms = sumRmsWindow / WINDOW_SIZE;
                chunkCounter = 0;
                sumRmsWindow = 0.0f;

                // =======================================================
                // ⚡ RUMUS SEPUH DSP: dBFS ke SPL (Sound Pressure Level)
                // =======================================================
                // 1. Hitung dBFS (Batas maksimal int16_t adalah 32768)
                float dbfs = 0.0f;
                if (avgRms > 1.0f)
                {
                    dbfs = 20.0f * log10(avgRms / 32768.0f);
                }
                else
                {
                    dbfs = -90.0f; // Sunyi total digital
                }

                // 2. INMP441 Acoustic Overload Point (AOP) adalah ~120 dB SPL.
                // Jadi, 0 dBFS (Max Digital) = 120 dB SPL Alam Nyata.
                float rawSpl = dbfs + 120.0f;

                // 3. NOISE GATE (Pintu Gerbang Derau ESP32)
                // ✨ FIX FINAL: Disesuaikan dengan log kamarmu.
                // Hardware noise floor aslimu mentok di sekitar 56-60 dB.
                const float HARDWARE_NOISE_FLOOR = 58.0f;

                // ✨ FIX FINAL: Angka 45 dB sangat realistis untuk kamar hening di Indonesia
                float finalDb = 45.0f;

                if (rawSpl > HARDWARE_NOISE_FLOOR)
                {
                    finalDb = rawSpl; // Jika di atas noise listrik, ambil suara aslinya
                }

                // 4. LOW-PASS FILTER (Smoothing Natural)
                if (finalDb > smoothedDb)
                {
                    smoothedDb = (smoothedDb * 0.40f) + (finalDb * 0.60f); // Naik cepat
                }
                else
                {
                    // ✨ FIX FINAL: Perlambat sedikit turunnya agar jarum tidak terlalu goyang (jitter)
                    smoothedDb = (smoothedDb * 0.90f) + (finalDb * 0.10f);
                }

                // ✨ FIX FINAL: Batas constrain disesuaikan dengan baseline 45 dB
                currentNoiseLevel = constrain((int)smoothedDb, 45, 120);

                // 🔍 DEBUG KALIBRASI PRO (Komen ini saat hari H sidang)
                // Serial.printf("[MIC PRO] avgRms=%.1f | rawSpl=%.1f | smoothedDb=%.1f\n", avgRms, rawSpl, smoothedDb);
            }

            afe_handle->feed(afe_data, mono_feed);
        }
    }
}
// ==========================================
// 🧠 TASK 2: PENDETEKSI KATA KUNCI
// ==========================================
void audio_detect_task(void *arg)
{
    Serial.println("[🤖] Detect Task: Menunggu panggilan 'Alexa'...");
    while (true)
    {
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);

        if (res)
        {
            // ==========================================
            // 1. JIKA WAKE WORD TERDETEKSI
            // ==========================================
            if (res->wakeup_state == WAKENET_DETECTED)
            {
                Serial.println("\n[🔥] WAKE WORD DETECTED: ALEXA!\n");

                // 1. MASUKKAN KASET (Mulai Pemanasan Mesin MP3)
                playRinchanSound(SND_AI_NOTIFY);

                // 2. ⚡ SMART WAIT DENGAN FUNGSI JEMBATAN ⚡
                unsigned long waitTimeout = millis();

                // Gunakan fungsi getAudioFilePos() yang baru kita buat!
                while (getAudioFilePos() == 0 && (millis() - waitTimeout < 400))
                {
                    delay(1);
                }

                delay(20);

                // 3. RENDER MIMIK
                drawEmoji(EMOTION_LISTENING);

                // 4. JEDA PENYELESAIAN DURASI AUDIO
                // Durasi file ai_notify.mp3 milikmu adalah ~200ms.
                // Karena kita sudah membuang waktu untuk menunggu di atas, kita cukup tunggu sisanya.
                delay(150);

                forceClearDialog();

                // 5. TRIGGER MULAI REKAM SUARA
                voiceChat_startRecording();
            }

            // ==========================================
            // 2. PENGIRIMAN DATA MIC (VOICE CHAT)
            // ==========================================
            if (voiceChat_isRecording())
            {
                voiceChat_feedAudio(res->data, res->data_size, res->vad_state);
            }
        }

        delay(1);
    }
}

// ==========================================
// ⚙️ INISIALISASI
// ==========================================
void initWakeNet()
{
    esp_log_level_set("AFE_SR", ESP_LOG_ERROR);

    Serial.println("[⚙️] Memulai Inisialisasi Telinga Rinchan (WakeNet9)...");

    // Konfigurasi Standar INMP441 (32-Bit I2S)
    i2s_config_t i2s_config = {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    i2s_config.sample_rate = 16000;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 8;
    i2s_config.dma_buf_len = 256;

    i2s_pin_config_t pin_config = {};
    pin_config.bck_io_num = RinchanConfig::Audio::MIC_BCLK;
    pin_config.ws_io_num = RinchanConfig::Audio::MIC_LRC;
    pin_config.data_out_num = I2S_PIN_NO_CHANGE;
    pin_config.data_in_num = RinchanConfig::Audio::MIC_DIN;
    pin_config.mck_io_num = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_1, &pin_config);

    srmodel_list_t *models = esp_srmodel_init("model");
    if (models == NULL)
    {
        Serial.println("[ERR] Partisi 'model' kosong!");
        return;
    }

    Serial.printf("[MODEL] Jumlah model terdeteksi: %d\n", models->num);
    if (models->num > 0)
    {
        for (int i = 0; i < models->num; i++)
        {
            char *name = models->model_name[i];
            char *wake_words = esp_srmodel_get_wake_words(models, name);
            Serial.printf("[MODEL] %02d: %s | wake words: %s\n", i + 1, name, wake_words ? wake_words : "-");
        }
    }
    else
    {
        Serial.println("[ERR] Daftar model tidak valid (partisi model kemungkinan kosong / belum ter-flash).\n"
                       "[HINT] Upload ulang firmware agar script auto-flash srmodels.bin dijalankan.");
    }

    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    afe_config_t afe_config = get_default_afe_config();

    // Pencarian benar: keyword1 = prefix model WakeNet, keyword2 = kata kunci wake word.
    char *model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "alexa");

    // Beberapa paket menamai wake word dengan kapitalisasi berbeda.
    if (model_name == NULL)
    {
        model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "Alexa");
    }

    // Fallback agar sistem tetap jalan walau model Alexa belum ada di partisi model.
    if (model_name == NULL)
    {
        model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
        if (model_name)
        {
            Serial.printf("[WARN] Model Alexa tidak ada. Fallback ke model WakeNet default: %s\n", model_name);
        }
    }

    if (model_name == NULL)
    {
        Serial.println("[ERR] Model WakeNet tidak ditemukan sama sekali di partisi 'model'!");
        Serial.println("[HINT] Pastikan partisi model sudah terisi srmodels.bin (bukan hanya ada di partition table).");
        return;
    }

    Serial.printf("[MODEL] WakeNet aktif: %s\n", model_name);

    // Setingan khusus 1 Mikrofon (PENTING!)
    afe_config.aec_init = false;
    afe_config.se_init = false;
    afe_config.vad_init = true;
    afe_config.pcm_config.total_ch_num = 1;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 0;

    afe_config.wakenet_model_name = model_name;
    afe_config.wakenet_init = true;

    afe_data = afe_handle->create_from_config(&afe_config);
    if (afe_data == NULL)
    {
        Serial.println("[ERR] Gagal membuat AFE Engine!");
        return;
    }

    // Alokasi stack memory dinaikkan menjadi 8192 agar tidak crash
    xTaskCreatePinnedToCore(audio_feed_task, "Feed_Task", 8192, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(audio_detect_task, "Detect_Task", 8192, NULL, 5, NULL, 1);

    Serial.println("[✅] Telinga Rinchan siap dan Online!");
}