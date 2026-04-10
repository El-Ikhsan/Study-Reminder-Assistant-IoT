#include "wakenet.h"
#include <driver/i2s.h>
#include <math.h>
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_log.h"
#include "model_path.h"
#include "esp_wn_models.h"
#include "display.h"
#include "sound_manager.h"
#include "config.h"
// Fungsi jembatan C
extern "C" afe_config_t get_default_afe_config();

static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;
volatile int currentNoiseLevel = 30;

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

    float smoothedDb = 30.0f;

    while (true)
    {
        size_t bytes_read = 0;
        // Menyedot suara dari I2S
        i2s_read(I2S_NUM_1, i2s_buff, expected_bytes, &bytes_read, portMAX_DELAY);

        if (bytes_read == expected_bytes)
        {
            // Ekstrak data 32-bit menjadi 16-bit
            int64_t sum = 0;
            for (int i = 0; i < audio_chunksize; i++)
            {
                // INMP441 menyimpan data valid di bit atas, kita geser ke bawah
                int16_t s16 = (int16_t)(i2s_buff[i] >> 14);
                mono_feed[i] = s16;
                sum += s16;
            }

            // Hapus DC Offset (Tegangan Listrik Bias)
            float mean = (float)sum / audio_chunksize;
            double sumSquares = 0;
            for (int i = 0; i < audio_chunksize; i++)
            {
                float centered = (float)mono_feed[i] - mean;
                sumSquares += centered * centered;
            }
            double rms = sqrt(sumSquares / audio_chunksize);

            // Konversi ke dB dengan smoothing agar pergerakan bar halus
            if (rms > 2.0)
            {
                float db = (float)(20.0 * log10(rms)) + 40.0f; // +40 adalah kalibrasi gain
                smoothedDb = (smoothedDb * 0.8) + (db * 0.2);
            }
            else
            {
                smoothedDb = (smoothedDb * 0.9) + (30.0f * 0.1);
            }

            currentNoiseLevel = constrain((int)smoothedDb, 30, 120);

            // Suapkan ke AI
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
        if (res && res->wakeup_state == WAKENET_DETECTED)
        {
            Serial.println("\n[🔥] WAKE WORD DETECTED: ALEXA!\n");

            // 1. TRIGGER AUDIO LEBIH DULU
            playRinchanSound(SND_AI_NOTIFY);

            // 2. TUNGGU DECODER AUDIO LOADING (Kompensasi Latency Dinaikkan!)
            // Naikkan ke 150ms agar LittleFS punya waktu mengekstrak WAV dan memompa I2S
            unsigned long waitStart = millis();
            while (millis() - waitStart < 150)
            {
                audioLoop();
                delay(1);
            }

            // 3. BARU MUNCULKAN MIMIK
            // Di titik 150ms ini, suara dipastikan sudah memukul speaker.
            // Mimik akan terasa benar-benar sinkron!
            drawEmoji(EMOTION_LISTENING);

            waitStart = millis();
            while (millis() - waitStart < 300)
            {
                audioLoop();
                delay(1);
            }

            // 5. BERSIHKAN DENGAN HALUS
            // Karena lagu dipastikan sudah selesai, memanggil stopAudio di sini sangat AMAN
            stopAudio();

            // Paksa amplifier memakan "0 Volt" agar senyap total tanpa bunyi 'pop'
            i2s_zero_dma_buffer((i2s_port_t)I2S_NUM_0);

            forceClearDialog();
        }
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