#include "ai_sensor.h"
#include "sensor/sensors.h"
#include "network/websocket.h"
#include "features/pomodoro.h"
#include <ArduinoJson.h>
#include <math.h> // ✨ FIX: Wajib untuk fungsi isnan()

namespace
{
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

    int getTempCategory(float temp)
    {
        if (temp >= 30.0f)
            return 1;
        if (temp >= 24.0f)
            return 2;
        if (temp >= 18.0f)
            return 3;
        if (temp >= 10.0f)
            return 4;
        return 5;
    }

    int getNoiseCategory(float noise)
    {
        if (noise >= 70.0f)
            return 1;
        if (noise >= 55.0f)
            return 2;
        if (noise >= 30.0f)
            return 3;
        return 4;
    }

    int getLightCategory(float lux)
    {
        if (lux >= 700.0f)
            return 1;
        if (lux >= 300.0f)
            return 2;
        if (lux >= 100.0f)
            return 3;
        if (lux >= 50.0f)
            return 4;
        return 5;
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
}

// ✨ FIX: FUNGSI BARU UNTUK MERESET MEMORI SAAT SESI DIMULAI
void aiSensor_forceReset()
{
    lastTempCat  = 3; pendingTempCat  = 3; tempConfirmCount  = 0;
    lastNoiseCat = 3; pendingNoiseCat = 3; noiseConfirmCount = 0;
    lastLightCat = 2; pendingLightCat = 2; lightConfirmCount = 0;
    // Reset memori AI agar sesi baru tidak membawa kondisi lama
    activeConditionFromAI = "Kondisi Optimal";
    Serial.println("[🧠] Memori Fisik AI Sensor di-reset untuk sesi baru!");
}

void aiSensor_updateMemory(const String &newCondition)
{
    if (newCondition != "null" && newCondition != "")
    {
        activeConditionFromAI = newCondition;
        Serial.println("[🧠 MEMORI AI] Diperbarui menjadi: " + activeConditionFromAI);
    }
}

void aiSensor_loop()
{
    if (!pomodoro_isRunning())
        return;

    unsigned long currentMillis = millis();
    if (currentMillis - lastFastCheckTime < FAST_CHECK_INTERVAL)
        return;
    lastFastCheckTime = currentMillis;

    SensorData currentData = readAllSensors();

    // ✨ FIX: Filter Anti-Bouncing I2C. Jika sensor error sesaat, abaikan!
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
                Serial.printf("[\u2705 KONFIRMASI SUHU] %d → %d (setelah %d check)\n", lastTempCat, currentTempCat, tempConfirmCount);
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
        if (tempConfirmCount > 0)
            Serial.printf("[RESET SUHU] Kategori kembali ke %d, abaikan false alarm\n", lastTempCat);
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
                Serial.printf("[\u2705 KONFIRMASI NOISE] %d → %d (setelah %d check)\n", lastNoiseCat, currentNoiseCat, noiseConfirmCount);
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
        if (noiseConfirmCount > 0)
            Serial.printf("[RESET NOISE] Kategori kembali ke %d, abaikan false alarm\n", lastNoiseCat);
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
                Serial.printf("[\u2705 KONFIRMASI CAHAYA] %d → %d (setelah %d check)\n", lastLightCat, currentLightCat, lightConfirmCount);
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
        if (lightConfirmCount > 0)
            Serial.printf("[RESET CAHAYA] Kategori kembali ke %d, abaikan false alarm\n", lastLightCat);
        pendingLightCat = lastLightCat;
        lightConfirmCount = 0;
    }

    if (!isEventConfirmed)
        return;

    // ====================================================================
    // TRIGGER AI
    // ====================================================================
    // Nilai dibulatkan ke integer sebelum dikirim agar sesuai
    // dengan format data training model AI (tidak dilatih dengan float).
    int tempInt  = (int)round(currentData.temperature);
    int luxInt   = (int)round(currentData.lightLux);
    int noiseInt = (int)round((float)currentData.noiseLevel);

    Serial.printf("[\u26a1 TRIGGER AI] Kirim: Suhu=%d\u00b0C, Cahaya=%d lux, Noise=%d dB | lastCondition='%s'\n",
                  tempInt, luxInt, noiseInt, activeConditionFromAI.c_str());

    JsonDocument doc;
    doc["type"] = "SENSOR_REPORT_FOR_AI";
    JsonObject payload = doc["payload"].to<JsonObject>();

    payload["sessionId"]    = pomodoro_getSessionId();
    payload["currentCycle"] = pomodoro_getCurrentCycle();
    payload["mode"]         = pomodoro_getCurrentMode();
    payload["phase"]        = pomodoro_getCurrentPhase();
    payload["media"]        = pomodoro_getMedia();

    payload["temperature"]  = tempInt;
    payload["lightLux"]     = luxInt;
    payload["noiseLevel"]   = noiseInt;
    payload["lastCondition"] = activeConditionFromAI;

    String jsonString;
    serializeJson(doc, jsonString);
    sendRawWS(jsonString);
}