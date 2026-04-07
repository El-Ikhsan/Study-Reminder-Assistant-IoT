#include <Arduino.h>
#include "config.h"
#include "wifi_manager.h"
#include "auth_manager.h"
#include "sensors.h"
#include "websocket.h"
#include "pomodoro_ws.h"
#include "button_manager.h"
#include "audio.h"

namespace
{
    unsigned long lastActionTime = 0;
    bool isPingNext = true;

    bool isRuntimeReady()
    {
        return isWiFiConnected() && isDeviceClaimed();
    }
}

void setup()
{
    Serial.begin(RinchanConfig::Runtime::SERIAL_BAUDRATE);
    delay(RinchanConfig::Runtime::STARTUP_DELAY_MS);
    Serial.println("\n=== RINCHAN IOT: FINAL FIRMWARE ===");

    initButton();
    // 1. Inisialisasi Hardware Sensor
    initSensors();
    // 2. Setup Koneksi (Cek NVS / Buka Captive Portal)
    initWiFi();

    // 3. Proses Claiming (Hanya jalan kalau WiFi sukses konek)
    if (isWiFiConnected())
    {
        initAuth();
    }

    // 4. Inisialisasi WebSocket (Hanya jalan kalau WiFi konek DAN sudah di-claim)
    if (isRuntimeReady())
    {
        // initAudio();
        // playAudioLocal("/hitaini.mp3");

        // Mulai audio startup dulu agar frame awal tidak berebut resource dengan handshake WS.
        initWebSocket();
    }
}

void loop()
{
    // 1. Jaga Captive Portal atau Auto-Reconnect WiFi
    handleWiFiLoop();

    // 2. Eksekusi tugas utama HANYA jika internet terhubung DAN token sudah ada
    if (isRuntimeReady())
    {
        handleButtonLoop();
        audioLoop();
        wsLoop();
        pomodoroLoop();

        // Timer Non-Blocking untuk interval 15 detik (Flip-flop Ping / Telemetry)
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
                sendTelemetryWS(currentData);
            }

            isPingNext = !isPingNext;
        }
    }
}