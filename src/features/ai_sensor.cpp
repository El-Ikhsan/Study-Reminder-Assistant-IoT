#include "features/ai_sensor.h"
#include "sensor/sensors.h"
#include "network/websocket.h"
#include "audio/audio.h"
#include "features/pomodoro.h"
#include <ArduinoJson.h>
#include <math.h> // Wajib untuk fungsi isnan()

namespace
{
    // ✨ FIX 1: SAKELAR AJAIB UNTUK SIDANG SKRIPSI
    const bool DEMO_MODE_SIDANG = true; // 🔴 Set ke 'false' saat dipakai belajar beneran!

    // ✨ FIX 2: VARIABEL COOLDOWN AI
    unsigned long lastAiSpokeTime = 0;
    unsigned long currentCooldownMs = 0;

    unsigned long lastFastCheckTime = 0;
    const int FAST_CHECK_INTERVAL = 1000; // Cek tiap 1 detik

    int lastTempCat = 3;
    int lastNoiseCat = 3;
    int lastLightCat = 2;

    int pendingTempCat = 3;
    int pendingNoiseCat = 3;
    int pendingLightCat = 2;

    int tempConfirmCount = 0;
    int noiseConfirmCount = 0;
    int lightConfirmCount = 0;

    const int TEMP_CONFIRM_NEEDED = 2;
    const int NOISE_CONFIRM_NEEDED = 5;
    const int LIGHT_CONFIRM_NEEDED = 2;

    String activeConditionFromAI = "Kondisi Optimal";

    // ✨ UPDATE Sesuai CSV Final
    int getTempCategory(float temp)
    {
        if (temp >= 31.0f)
            return 1; // Panas
        if (temp >= 28.0f)
            return 2; // Hangat
        if (temp >= 22.0f)
            return 3; // Sejuk (Optimal)
        return 4;     // Dingin
    }

    int getNoiseCategory(float noise)
    {
        if (noise >= 80.0f)
            return 1; // Bising
        if (noise >= 65.0f)
            return 2; // Ramai
        if (noise >= 50.0f)
            return 3; // Normal (Optimal)
        return 4;     // Sunyi (Optimal)
    }

    int getLightCategory(float lux)
    {
        if (lux >= 700.0f)
            return 1; // Silau
        if (lux >= 150.0f)
            return 2; // Terang (Optimal)
        if (lux >= 50.0f)
            return 3; // Redup
        return 4;     // Gelap
    }
}

void aiSensor_init()
{
    lastTempCat = 3;
    pendingTempCat = 3;
    tempConfirmCount = 0;
    lastNoiseCat = 3;
    pendingNoiseCat = 3;
    noiseConfirmCount = 0;
    lastLightCat = 2;
    pendingLightCat = 2;
    lightConfirmCount = 0;
    activeConditionFromAI = "Kondisi Optimal";
    lastAiSpokeTime = 0;
    currentCooldownMs = 0;
}

void aiSensor_forceReset()
{
    lastTempCat = 3;
    pendingTempCat = 3;
    tempConfirmCount = 0;
    lastNoiseCat = 3;
    pendingNoiseCat = 3;
    noiseConfirmCount = 0;
    lastLightCat = 2;
    pendingLightCat = 2;
    lightConfirmCount = 0;
    activeConditionFromAI = "Kondisi Optimal";

    // ✨ FIX 3: Reset masa tenang setiap mulai Pomodoro baru
    lastAiSpokeTime = 0;
    currentCooldownMs = 0;
    Serial.println("[🧠] Memori & Cooldown AI Sensor di-reset untuk sesi baru!");
}

void aiSensor_updateMemory(const String &newCondition)
{
    if (newCondition != "null" && newCondition != "")
    {
        activeConditionFromAI = newCondition;
        Serial.println("\n[🧠 MEMORI AI] Diperbarui menjadi: " + activeConditionFromAI);

        // ✨ FIX 4: ATUR COOLDOWN SAAT AI SELESAI BICARA
        lastAiSpokeTime = millis();
        if (DEMO_MODE_SIDANG)
        {
            currentCooldownMs = 15000; // Demo: Jeda cuma 15 detik
        }
        else
        {
            // Realita: Beri napas 2 menit kalau habis pemulihan, 1 menit kalau diinterupsi
            if (activeConditionFromAI == "Kondisi Optimal")
                currentCooldownMs = 120000;
            else
                currentCooldownMs = 60000;
        }
        Serial.printf("[⏳] Asisten menahan diri. Cooldown disetel %lu ms\n\n", currentCooldownMs);
    }
}

void aiSensor_loop()
{
    if (!pomodoro_isRunning())
        return;

    // ✨ FIX FINAL UX: GEMBOK SPEAKER (ANTI-TABRAKAN)
    // Jika Rinchan sedang berbicara, BEKUKAN seluruh proses sensor.
    // Timer debouncing akan berhenti, mencegah data baru menimpa render UI yang sedang berjalan.
    if (audio_isPlaying())
    {
        return;
    }

    unsigned long currentMillis = millis();

    // ✨ FIX 5: BLOKIR SENSOR JIKA AI SEDANG COOLDOWN
    // Asisten "tutup mata" sementara agar pengguna punya waktu memperbaiki ruangan
    if (currentCooldownMs > 0 && (currentMillis - lastAiSpokeTime < currentCooldownMs))
    {
        return;
    }

    if (currentMillis - lastFastCheckTime < FAST_CHECK_INTERVAL)
        return;
    lastFastCheckTime = currentMillis;

    SensorData currentData = readAllSensors();

    if (isnan(currentData.temperature) || currentData.temperature <= 0.0)
        return;
    if (isnan(currentData.lightLux) || currentData.lightLux < 0)
        return;

    int currentTempCat = getTempCategory(currentData.temperature);
    int currentNoiseCat = getNoiseCategory(currentData.noiseLevel);
    int currentLightCat = getLightCategory(currentData.lightLux);

    bool isEventConfirmed = false;

    // --- Suhu ---
    if (currentTempCat != lastTempCat)
    {
        if (currentTempCat == pendingTempCat)
        {
            tempConfirmCount++;
            if (tempConfirmCount >= TEMP_CONFIRM_NEEDED)
            {
                Serial.printf("[\u2705 KONFIRMASI SUHU] %d → %d\n", lastTempCat, currentTempCat);
                lastTempCat = currentTempCat;
                tempConfirmCount = 0;
                isEventConfirmed = true;
            }
        }
        else
        {
            pendingTempCat = currentTempCat;
            tempConfirmCount = 1;
        }
    }
    else
    {
        pendingTempCat = lastTempCat;
        tempConfirmCount = 0;
    }

    // --- Kebisingan ---
    if (currentNoiseCat != lastNoiseCat)
    {
        if (currentNoiseCat == pendingNoiseCat)
        {
            noiseConfirmCount++;
            if (noiseConfirmCount >= NOISE_CONFIRM_NEEDED)
            {
                Serial.printf("[\u2705 KONFIRMASI NOISE] %d → %d\n", lastNoiseCat, currentNoiseCat);
                lastNoiseCat = currentNoiseCat;
                noiseConfirmCount = 0;
                isEventConfirmed = true;
            }
        }
        else
        {
            pendingNoiseCat = currentNoiseCat;
            noiseConfirmCount = 1;
        }
    }
    else
    {
        pendingNoiseCat = lastNoiseCat;
        noiseConfirmCount = 0;
    }

    // --- Cahaya ---
    if (currentLightCat != lastLightCat)
    {
        if (currentLightCat == pendingLightCat)
        {
            lightConfirmCount++;
            if (lightConfirmCount >= LIGHT_CONFIRM_NEEDED)
            {
                Serial.printf("[\u2705 KONFIRMASI CAHAYA] %d → %d\n", lastLightCat, currentLightCat);
                lastLightCat = currentLightCat;
                lightConfirmCount = 0;
                isEventConfirmed = true;
            }
        }
        else
        {
            pendingLightCat = currentLightCat;
            lightConfirmCount = 1;
        }
    }
    else
    {
        pendingLightCat = lastLightCat;
        lightConfirmCount = 0;
    }

    if (!isEventConfirmed)
        return;

    // ====================================================================
    // TRIGGER AI
    // ====================================================================
    int tempInt = (int)round(currentData.temperature);
    int luxInt = (int)round(currentData.lightLux);
    int noiseInt = (int)round((float)currentData.noiseLevel);

    Serial.printf("[\u26a1 TRIGGER AI] Kirim: Suhu=%d\u00b0C, Cahaya=%d lux, Noise=%d dB | lastCondition='%s'\n",
                  tempInt, luxInt, noiseInt, activeConditionFromAI.c_str());

    JsonDocument doc;
    doc["type"] = "SENSOR_REPORT_FOR_AI";
    JsonObject payload = doc["payload"].to<JsonObject>();

    payload["sessionId"] = pomodoro_getSessionId();
    payload["currentCycle"] = pomodoro_getCurrentCycle();
    payload["mode"] = pomodoro_getCurrentMode();
    payload["phase"] = pomodoro_getCurrentPhase();
    payload["media"] = pomodoro_getMedia();
    payload["temperature"] = tempInt;
    payload["lightLux"] = luxInt;
    payload["noiseLevel"] = noiseInt;
    payload["lastCondition"] = activeConditionFromAI;

    String jsonString;
    serializeJson(doc, jsonString);
    sendRawWS(jsonString);

    // ✨ FIX 6: Pasang Cooldown sementara (5 detik) untuk mencegah spam JSON beruntun
    // karena delay jaringan, sebelum memori di-update oleh Backend.
    lastAiSpokeTime = millis();
    currentCooldownMs = 5000;
}