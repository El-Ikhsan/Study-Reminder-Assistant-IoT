#include "sensors.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>
#include <driver/i2s.h>
#include <math.h>

// ============================================================
// SENSOR HARDWARE: BMP280 (Suhu) & BH1750 (Cahaya)
// ============================================================
namespace
{
    Adafruit_BMP280 bmp;
    BH1750 lightMeter;
}

// ============================================================
// MIKROFON INMP441 — Pengukur Kebisingan (dB SPL)
// Berjalan sebagai FreeRTOS task di Core 1.
//
// Algoritma DSP:
//   1. Baca sampel 32-bit dari I2S (INMP441 butuh clock 32-bit).
//   2. Geser >> 16 untuk mendapat int16_t yang bermakna.
//   3. Hitung RMS dari WINDOW_SIZE chunk, konversi ke dBFS → dB SPL.
//   4. Terapkan noise gate + low-pass filter untuk pembacaan stabil.
// ============================================================
namespace
{
    // Ukuran satu chunk baca I2S (sampel per panggilan i2s_read)
    constexpr int MIC_CHUNK_SIZE = 256;

    // Jumlah chunk yang dirata-rata sebelum menghitung dB
    constexpr int MIC_WINDOW_SIZE = 16;

    // INMP441 Acoustic Overload Point = ~120 dB SPL → 0 dBFS = 120 dB SPL
    constexpr float MIC_AOP_DB = 120.0f;

    // Noise floor hardware ESP32 (derau listrik papan), dalam dB SPL.
    // Sesuaikan jika pembacaan terlalu tinggi di ruangan hening.
    constexpr float MIC_HARDWARE_NOISE_FLOOR = 58.0f;

    // Baseline sunyi yang ditampilkan saat tidak ada suara bermakna (dB SPL)
    constexpr float MIC_SILENCE_BASELINE = 45.0f;
}

// Nilai kebisingan terkini, diperbarui oleh task mikrofon.
// Diakses oleh readAllSensors() di bawah.
static volatile int currentNoiseLevel = (int)MIC_SILENCE_BASELINE;

static void mic_read_task(void *arg)
{
    // Alokasi buffer di heap agar tidak menghabiskan stack task
    int32_t *i2s_buff = (int32_t *)malloc(MIC_CHUNK_SIZE * sizeof(int32_t));
    if (i2s_buff == NULL)
    {
        Serial.println("[MIC] ERROR: Gagal alokasi buffer I2S!");
        vTaskDelete(NULL);
        return;
    }

    float smoothedDb   = MIC_SILENCE_BASELINE;
    float sumRmsWindow = 0.0f;
    int chunkCounter   = 0;

    while (true)
    {
        size_t bytes_read = 0;
        size_t expected   = MIC_CHUNK_SIZE * sizeof(int32_t);

        i2s_read(I2S_NUM_1, i2s_buff, expected, &bytes_read, portMAX_DELAY);
        if (bytes_read != expected)
            continue;

        // 1. Hapus DC offset: hitung mean lalu sum of squares
        int64_t sum = 0;
        for (int i = 0; i < MIC_CHUNK_SIZE; i++)
            sum += (int16_t)(i2s_buff[i] >> 16);

        float mean  = (float)sum / MIC_CHUNK_SIZE;
        double sumSq = 0;
        for (int i = 0; i < MIC_CHUNK_SIZE; i++)
        {
            float centered = (float)((int16_t)(i2s_buff[i] >> 16)) - mean;
            sumSq += centered * centered;
        }
        float rms = (float)sqrt(sumSq / MIC_CHUNK_SIZE);

        // 2. Kumpulkan ke jendela rata-rata
        sumRmsWindow += rms;
        chunkCounter++;

        if (chunkCounter >= MIC_WINDOW_SIZE)
        {
            float avgRms  = sumRmsWindow / MIC_WINDOW_SIZE;
            chunkCounter  = 0;
            sumRmsWindow  = 0.0f;

            // 3. RMS → dBFS → dB SPL
            float dbfs   = (avgRms > 1.0f)
                               ? 20.0f * log10f(avgRms / 32768.0f)
                               : -90.0f;
            float rawSpl = dbfs + MIC_AOP_DB;

            // 4. Noise gate
            float finalDb = MIC_SILENCE_BASELINE;
            if (rawSpl > MIC_HARDWARE_NOISE_FLOOR)
                finalDb = rawSpl;

            // 5. Low-pass filter asimetris: naik cepat, turun lambat
            if (finalDb > smoothedDb)
                smoothedDb = (smoothedDb * 0.40f) + (finalDb * 0.60f);
            else
                smoothedDb = (smoothedDb * 0.90f) + (finalDb * 0.10f);

            currentNoiseLevel = constrain((int)smoothedDb, (int)MIC_SILENCE_BASELINE, 120);

            // Debug kalibrasi (uncomment saat perlu):
            // Serial.printf("[MIC] avgRms=%.1f rawSpl=%.1f smooth=%.1f\n", avgRms, rawSpl, smoothedDb);
        }
    }

    free(i2s_buff); // tidak pernah tercapai, tapi aman
}

// ============================================================
// INISIALISASI SEMUA SENSOR
// ============================================================
void initSensors()
{
    Wire.begin(RinchanConfig::Pins::I2C_SDA, RinchanConfig::Pins::I2C_SCL);
    Serial.println("[SENSOR] Menginisialisasi hardware...");

    // ── Suhu: BMP280 ──
    if (!bmp.begin(0x76))
        Serial.println("[ERROR] BMP280 Gagal!");
    else
        Serial.println("[OK] BMP280 Siap.");

    // ── Cahaya: BH1750 ──
    if (!lightMeter.begin())
        Serial.println("[ERROR] BH1750 Gagal!");
    else
        Serial.println("[OK] BH1750 Siap.");

    // ── Kebisingan: INMP441 via I2S ──
    Serial.println("[MIC] Inisialisasi INMP441 (I2S_NUM_1)...");

    i2s_config_t cfg         = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate          = 16000;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 256;

    i2s_pin_config_t pins = {};
    pins.bck_io_num       = RinchanConfig::Audio::MIC_BCLK;
    pins.ws_io_num        = RinchanConfig::Audio::MIC_LRC;
    pins.data_out_num     = I2S_PIN_NO_CHANGE;
    pins.data_in_num      = RinchanConfig::Audio::MIC_DIN;
    pins.mck_io_num       = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_NUM_1, &cfg, 0, NULL);
    if (err != ESP_OK)
    {
        Serial.printf("[MIC] ERROR: i2s_driver_install gagal (%d)\n", err);
        return;
    }
    i2s_set_pin(I2S_NUM_1, &pins);

    // Task di Core 1, prioritas 5, stack 4096 byte
    xTaskCreatePinnedToCore(mic_read_task, "MicTask", 4096, NULL, 5, NULL, 1);
    Serial.println("[MIC] ✅ INMP441 siap. Mengukur kebisingan...");
}

// ============================================================
// BACA SEMUA SENSOR SEKALIGUS
// ============================================================
SensorData readAllSensors()
{
    SensorData data;
    data.temperature = bmp.readTemperature() - 2.0f;
    data.lightLux    = lightMeter.readLightLevel();
    data.noiseLevel  = currentNoiseLevel; // Dari task mikrofon INMP441
    return data;
}