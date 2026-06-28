#include <Arduino.h>
#include <time.h> // ✨ FIX: Library Waktu Nyata (NTP)
#include "config.h"
#include "network/wifi.h"
#include "network/auth.h"
#include "core/hw_manager.h"
#include "sensor/sensors.h"
#include "network/websocket.h"
#include "features/pomodoro.h"
#include "features/ai_sensor.h"
#include "core/button_manager.h"
#include "audio/audio.h"
#include "ui/display.h"
#include "audio/sound_manager.h"
#include <TFT_eSPI.h>
#include "voice_recognition/wakenet.h"

String globalSensorAlert = "";
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

    // ✨ FUNGSI PENGAMBIL WAKTU NYATA (JAM:MENIT)
    String getRealTime()
    {
        struct tm timeinfo;
        // Coba ambil waktu dari sistem (Timeout 10ms agar tidak lag)
        if (!getLocalTime(&timeinfo, 10))
        {
            return "--:--"; // Jika gagal / belum sinkron
        }
        char timeStringBuff[10];
        strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
        return String(timeStringBuff);
    }
}

void setup()
{
    Serial.begin(RinchanConfig::Runtime::SERIAL_BAUDRATE);
    delay(500);

#ifdef RGB_BUILTIN
    neopixelWrite(RGB_BUILTIN, 0, 0, 0);
#else
    neopixelWrite(48, 0, 0, 0);
    neopixelWrite(8, 0, 0, 0);
#endif

    Serial.println("\n=== RINCHAN IOT: COLD BOOT ===");

    // 1. PRE-BOOT
    initButton();
    initDisplay();
    setDisplayBrightness(0);

    initHardwareConfig();
    initAudio();
    setVolumePercent(getSavedVolume());

    initSensors();
    aiSensor_init();

    // 2. RENDER VISUAL
    showBootingScreen();

    // 3. FADE IN AUDIO & VISUAL
    unsigned long bootStartTime = millis();
    playRinchanSound(SND_BOOTING);

    int targetBrightness = getSavedBrightness();
    for (int i = 0; i <= targetBrightness; i += 2)
    {
        setDisplayBrightness(i);
        audioLoop();
        delay(15);
    }

    // 4. HOLD THE SCENE
    while (millis() - bootStartTime < 5000)
    {
        audioLoop();
        delay(5);
    }

    // 5. WIFI & CLOUD
    tft.fillScreen(TFT_BLACK);
    drawEmoji(EMOTION_IDLE); // Ganti Sleepy jadi Idle
    showDialogWidget("Mencari WiFi...");

    initWiFi();

    // ✨ SINKRONISASI JAM INTERNET (WIB = GMT+7 = 25200 detik)
    if (isWiFiConnected())
    {
        configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    }

    // 6. AUTENTIKASI
    if (isWiFiConnected())
    {
        showDialogWidget("Mengecek Token...");
        initAuth();
    }

    // 7. RUNTIME READY
    if (isRuntimeReady())
    {
        drawTopBar(true, true, getRealTime(), "");

        drawEmoji(EMOTION_IDLE);
        showDialogWidget("Sistem Siap! Menunggu Perintah.");

        initWebSocket();
        initWakeNet();

        bootMessageTimer = millis();
        clearBootMessage = true;
    }
}

void loop()
{
    handleButtonLoop();
    handleWiFiLoop();

    if (isRuntimeReady())
    {
        audioLoop();
        wsLoop();
        pomodoroLoop();
        aiSensor_loop();

        // ✨ EKSEKUSI DIALOG YANG DIANTREKAN (setelah wsLoop berjalan agar ACK ter-flush duluan)
        processDialogQueue();

        // ✨ MOTOR ANIMASI GIF UTAMA
        playDisplayAnimation();

        // ==========================================
        // ✨ LOGIKA SAKLAR MIC & TOP BAR
        // ==========================================
        bool isFocusMode = (pomodoro_isRunning() && pomodoro_getCurrentMode() == "fokus");
        // Kita tidak bisa langsung akses currentWidget, jadi deteksi dari cancelCurrentDialog
        // atau anggap aman jika tidak ada interupsi audio panjang.
        // Cara paling aman: Cek jika speaker I2S sedang bersuara (SFX ketik / Lagu).
        bool isSpeakerLoud = audio_isPlaying();

        bool shouldWakeNetBeActive = (!isFocusMode && !isSpeakerLoud);

        // WakeNet dimatikan saat fokus atau speaker aktif, tapi mic tetap hidup.
        setWakeNetEnabled(shouldWakeNetBeActive);

        // Mic di-mute hanya saat speaker aktif untuk hindari feedback.
        setMicMuted(isSpeakerLoud);

        // Update Layar Top Bar (Setiap 1 detik)
        unsigned long currentMillis = millis();
        static unsigned long lastTopBarUpdate = 0;
        if (currentMillis - lastTopBarUpdate >= 1000)
        {
            lastTopBarUpdate = currentMillis;

            // ✨ Top Bar Alert: Hanya tampilkan saat Pomodoro aktif
            // globalSensorAlert di-manage oleh websocket.cpp saat menerima AI_RESPONSE
            String alertTxt = "";
            if (pomodoro_isRunning() && globalSensorAlert != "" && globalSensorAlert != "Kondisi Optimal")
            {
                alertTxt = globalSensorAlert;
            }

            // Render ke layar (Wifi, Mic, Jam, Teks Peringatan)
            drawTopBar(isWiFiConnected(), shouldWakeNetBeActive, getRealTime(), alertTxt);
        }

        // ==========================================
        // PEMBERSIH LAYAR AWAL
        // ==========================================
        if (clearBootMessage && (currentMillis - bootMessageTimer >= 4000))
        {
            clearWidget();
            drawEmoji(EMOTION_IDLE);
            clearBootMessage = false;
        }

        // ==========================================
        // TELEMETRY PING
        // ==========================================
        if (currentMillis - lastActionTime >= RinchanConfig::Runtime::ACTION_INTERVAL_MS)
        {
            lastActionTime = currentMillis;
            if (isPingNext)
            {
                // sendPingWS();
            }
            else
            {
                SensorData currentData = readAllSensors();
                sendTelemetryWS(currentData);
            }
            isPingNext = !isPingNext;
        }
    }
    delay(1);
}