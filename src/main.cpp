#include <Arduino.h>
#include "config.h"
#include "network/wifi.h"
#include "network/auth.h"
#include "core/hw_manager.h"
#include "sensor/sensors.h"
#include "network/websocket.h"
#include "features/pomodoro.h"
#include "core/button_manager.h"
#include "audio/audio.h"
#include "ui/display.h"
#include "audio/sound_manager.h"
#include <TFT_eSPI.h>
#include "voice_recognition/wakenet.h"

namespace
{
    unsigned long lastActionTime = 0;
    unsigned long bootMessageTimer = 0;
    bool isPingNext = true;
    bool clearBootMessage = false;
    TFT_eSPI tft = TFT_eSPI();

    bool isRuntimeReady()
    {
        return isWiFiConnected() && isDeviceClaimed();
    }
}

void setup()
{
    Serial.begin(RinchanConfig::Runtime::SERIAL_BAUDRATE);
    delay(500); // Jangan terlalu lama agar tidak terasa lag saat dinyalakan
#ifdef RGB_BUILTIN
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
#else
    neopixelWrite(48, 0, 0, 0); // Jika boardmu pakai pin 48
    neopixelWrite(8, 0, 0, 0);  // Jika boardmu pakai pin 8
#endif

    Serial.println("\n=== RINCHAN IOT: COLD BOOT ===");

    // ==========================================
    // 1. PRE-BOOT: LOAD CONFIG & HARDWARE AWAL
    // ==========================================
    initDisplay();
    setDisplayBrightness(0); // LAYAR WAJIB MATI DULU

    // ✨ Ambil Volume dan Brightness dari NVS Memory
    initHardwareConfig();

    initAudio();
    setVolumePercent(getSavedVolume()); // Set volume speaker dari hasil memori
                                        // Inisialisasi Telinga AI (WakeNet9)
    initSensors();

    // ==========================================
    // 2. RENDER VISUAL DI BALIK LAYAR
    // ==========================================
    showBootingScreen();

    // ==========================================
    // 3. THE PERFECT SYNC (AUDIO + FADE IN)
    // ==========================================
    unsigned long bootStartTime = millis();

    // Tembakkan suara booting
    playRinchanSound(SND_BOOTING);

    // ✨ Ambil target brightness dari memori
    int targetBrightness = getSavedBrightness();

    // Efek Fade-In dari 0 menuju nilai Brightness memori
    for (int i = 0; i <= targetBrightness; i += 2)
    {
        setDisplayBrightness(i);
        audioLoop(); // Pompa I2S
        delay(15);
    }

    // ==========================================
    // 4. HOLD THE SCENE (Tahan 5 Detik)
    // ==========================================
    // Tahan visual booting selama sisa waktu 5 detik
    while (millis() - bootStartTime < 5000)
    {
        audioLoop();
        delay(5);
    }

    // ==========================================
    // 5. TUGAS BERAT DIMULAI (WIFI & CLOUD)
    // ==========================================
    tft.fillScreen(TFT_BLACK);
    drawEmoji(EMOTION_SLEEPY);
    showDialogWidget("Mencari WiFi...");

    initWiFi();

    // ==========================================
    // 4. AUTENTIKASI / CLAIMING
    // ==========================================
    if (isWiFiConnected())
    {
        showDialogWidget("Mengecek Token...");
        initAuth();
    }

    // ==========================================
    // 5. RUNTIME READY (SISTEM SIAP)
    // ==========================================
    if (isRuntimeReady())
    {
        // Ubah mimik jadi standby
        drawEmoji(EMOTION_IDLE);
        showDialogWidget("Rinchan Siap! Rinchan Siap! Rinchan Siap! Rinchan Siap! Rinchan Siap!");

        // Opsional: Mainkan suara notifikasi "Ting!" kalau siap
        // playRinchanSound(SND_AI_NOTIFY);

        // Mulai WS paling akhir
        initWebSocket();
        initWakeNet();
        // Aktifkan timer pembersih layar (hilang setelah 3 detik)
        bootMessageTimer = millis();
        clearBootMessage = true;
    }
    else
    {
        // Masuk Captive Portal
        drawEmoji(EMOTION_UNCOMFORTABLE); // Ganti mimik canggung/bingung
        showDialogWidget("Mode Setup: Buka WiFi Rinchan");
    }
}

void loop()
{
    // 1. Jaga Captive Portal atau Auto-Reconnect WiFi
    handleWiFiLoop();

    // 2. Eksekusi tugas utama HANYA jika internet terhubung DAN token sudah ada
    if (isRuntimeReady())
    {
        // handleButtonLoop();
        audioLoop();    // Jaga aliran I2S MP3
        wsLoop();       // Jaga koneksi WebSocket
        pomodoroLoop(); // Jaga logika timer Pomodoro

        // ==========================================
        // 3. PEMBERSIH LAYAR OTOMATIS (NON-BLOCKING)
        // ==========================================
        if (clearBootMessage && (millis() - bootMessageTimer >= 4000))
        {
            clearWidget();           // Hapus kotak dialog
            drawEmoji(EMOTION_IDLE); // Kembalikan wajah ke normal
            clearBootMessage = false;
        }

        // ==========================================
        // 4. TELEMETRY & PING (INTERVAL)
        // ==========================================
        unsigned long currentMillis = millis();
        if (currentMillis - lastActionTime >= RinchanConfig::Runtime::ACTION_INTERVAL_MS)
        {
            lastActionTime = currentMillis;

            if (isPingNext)
            {
                sendPingWS();
            }
            else
            {
                SensorData currentData = readAllSensors();
                (void)currentData;
                sendTelemetryWS(currentData);

                // Opsional: Bikin Rinchan berkedip setiap kali ngirim data sensor!
                // Ini bikin alatnya terasa hidup tanpa harus memanggil layar terlalu sering.
                // drawEmoji(EMOJI_HAPPY);
            }

            isPingNext = !isPingNext;
        }
    }
    delay(1);
}