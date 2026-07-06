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

    // Flag: sensor diblokir sampai backend merespons fase awal pomodoro
    bool waitingForAwalResponse = false;
    unsigned long awalWaitStartTime = 0; // Kapan mulai menunggu (untuk timeout)

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

    // ✨ Status aktif/nonaktif sensor (Kebutuhan Demo Sidang)
    bool isTempEnabled = true;
    bool isLightEnabled = true;
    bool isNoiseEnabled = true;
    bool isForceColdEnabled = false; // ✨ Tambahan: Trigger Dingin Extrem

    // ✨ UPDATE Sesuai CSV Final
    int getTempCategory(int temp)
    {
        if (temp >= 31) return 1; // Panas (buruk)
        if (temp >= 28) return 2; // Hangat
        if (temp >= 22) return 3; // Sejuk (Optimal)
        if (temp >= 16) return 4; // Dingin
        return 5;                 // Dingin Ekstrem (buruk)
    }

    int getNoiseCategory(int noise)
    {
        if (noise >= 80) return 1; // Bising
        if (noise >= 65) return 2; // Ramai
        if (noise >= 50) return 3; // Normal (Optimal)
        return 4;                  // Sunyi (Optimal)
    }

    int getLightCategory(int lux)
    {
        if (lux >= 700) return 1; // Silau
        if (lux >= 150) return 2; // Terang (Optimal)
        if (lux >= 50) return 3;  // Redup
        return 4;                 // Gelap
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
    waitingForAwalResponse = true;
    awalWaitStartTime = millis();
    Serial.println("[AI] Memori & Cooldown di-reset. Sensor menunggu respons fase awal.");
}


// ✨ UPDATE: Fungsi lama tetap ada untuk kompatibilitas
void aiSensor_updateMemory(const String &newCondition)
{
    // Selalu clear flag meski newCondition null/kosong (fase awal mungkin tidak kirim kondisi)
    waitingForAwalResponse = false;
    if (newCondition != "null" && newCondition != "")
    {
        activeConditionFromAI = newCondition;
        Serial.println("\n[AI MEMORI] Diperbarui menjadi: " + activeConditionFromAI);
    }
}

// ✨ NEW: Fungsi baru yang membedakan cooldown interupsi vs pemulihan
void aiSensor_updateMemoryWithCooldown(const String &newCondition, bool isRecovery)
{
    // Selalu clear flag meski newCondition null/kosong
    waitingForAwalResponse = false;
    if (newCondition != "null" && newCondition != "")
    {
        if (isRecovery)
        {
            activeConditionFromAI = "Kondisi Optimal";
            Serial.println("\n[AI MEMORI] PEMULIHAN, lock dilepas.");
        }
        else
        {
            activeConditionFromAI = newCondition;
            Serial.println("\n[AI MEMORI] INTERUPSI, lock aktif: " + activeConditionFromAI);
        }
    }

    if (isRecovery)
    {
        // PEMULIHAN: Beri cooldown sebelum bisa interupsi baru
        lastAiSpokeTime = millis();
        if (DEMO_MODE_SIDANG)
        {
            currentCooldownMs = 5000; // Demo: 5 detik
        }
        else
        {
            currentCooldownMs = 120000; // Realita: 2 menit
        }
        Serial.printf("[⏳] Cooldown PEMULIHAN disetel %lu ms\n\n", currentCooldownMs);
    }
    else
    {
        // INTERUPSI: Tanpa delay! Langsung bisa deteksi pemulihan
        lastAiSpokeTime = 0;
        currentCooldownMs = 0;
        Serial.println("[⚡] Cooldown INTERUPSI = 0ms (siap deteksi pemulihan)\n");
    }
}

void aiSensor_loop()
{
    if (!pomodoro_isRunning())
        return;

    // ✨ FIX FINAL UX: GEMBOK SPEAKER (ANTI-TABRAKAN)
    if (audio_isPlaying())
    {
        return;
    }

    unsigned long currentMillis = millis();

    // Tunggu sampai backend merespons fase awal pomodoro
    // Fallback: setelah 60 detik, aktifkan sensor meski belum ada respons backend
    if (waitingForAwalResponse)
    {
        if (currentMillis - awalWaitStartTime < 60000)
            return;
        waitingForAwalResponse = false;
        Serial.println("[SENSOR] Timeout 60s, sensor diaktifkan paksa.");
    }

    // Cooldown setelah pemulihan
    if (currentCooldownMs > 0 && (currentMillis - lastAiSpokeTime < currentCooldownMs))
    {
        return;
    }

    if (currentMillis - lastFastCheckTime < FAST_CHECK_INTERVAL)
        return;
    lastFastCheckTime = currentMillis;

    SensorData currentData = readAllSensors();

    // ✨ Mute sensor (Demo Sidang): Spoof data ke nilai optimal jika sensor dimatikan
    if (!isTempEnabled) currentData.temperature = 25.0f; // 25°C = Sejuk (Optimal)
    if (!isLightEnabled) currentData.lightLux = 250.0f;  // 250 lux = Terang (Optimal)
    if (!isNoiseEnabled) currentData.noiseLevel = 55.0f; // 55 dB = Normal (Optimal)
    if (isForceColdEnabled) currentData.temperature = 10.0f; // 10°C = Dingin Extrem (Buruk)

    if (isnan(currentData.temperature) || currentData.temperature <= 0.0)
        return;
    if (isnan(currentData.lightLux) || currentData.lightLux < 0)
        return;

    int tempInt = (int)round(currentData.temperature);
    int luxInt = (int)round(currentData.lightLux);
    int noiseInt = (int)round(currentData.noiseLevel);

    int currentTempCat = getTempCategory(tempInt);
    int currentNoiseCat = getNoiseCategory(noiseInt);
    int currentLightCat = getLightCategory(luxInt);

    bool isEventConfirmed = false;

    // ====================================================================
    // ✨ LOGIKA KUNCI OTOMATIS ESP32 (EDGE COMPUTING LOCK) — DIPERBAIKI
    // ====================================================================
    bool isLocked = (activeConditionFromAI != "Kondisi Optimal");
    bool lockTemp = isLocked && (activeConditionFromAI.indexOf("Suhu") >= 0);
    bool lockNoise = isLocked && (activeConditionFromAI.indexOf("Suara") >= 0);
    bool lockLight = isLocked && (activeConditionFromAI.indexOf("Cahaya") >= 0);

    if (isLocked)
    {
        // ================================================================
        // 🔒 LOCKED: HANYA pantau sensor yang sedang di-interupsi
        // Sensor lain DIABAIKAN TOTAL (tidak ada debounce yang berjalan)
        // ================================================================

        if (lockLight)
        {
            // Hanya pantau Cahaya
            if (currentLightCat != lastLightCat)
            {
                if (currentLightCat == pendingLightCat)
                {
                    lightConfirmCount++;
                    if (lightConfirmCount >= LIGHT_CONFIRM_NEEDED)
                    {
                        Serial.printf("[KONFIRMASI CAHAYA] cat %d -> %d\n", lastLightCat, currentLightCat);
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
        }
        else if (lockTemp)
        {
            // Hanya pantau Suhu
            if (currentTempCat != lastTempCat)
            {
                if (currentTempCat == pendingTempCat)
                {
                    tempConfirmCount++;
                    if (tempConfirmCount >= TEMP_CONFIRM_NEEDED)
                    {
                        Serial.printf("[✅ KONFIRMASI SUHU] %d → %d\n", lastTempCat, currentTempCat);
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
        }
        else if (lockNoise)
        {
            // Hanya pantau Kebisingan
            if (currentNoiseCat != lastNoiseCat)
            {
                if (currentNoiseCat == pendingNoiseCat)
                {
                    noiseConfirmCount++;
                    if (noiseConfirmCount >= NOISE_CONFIRM_NEEDED)
                    {
                        Serial.printf("[✅ KONFIRMASI NOISE] %d → %d\n", lastNoiseCat, currentNoiseCat);
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
        }
    }
    else
    {
        // ================================================================
        // 🔓 UNLOCKED: Pantau SEMUA sensor, kirim payload lengkap
        // Backend yang menentukan prioritas interupsi
        // ================================================================

        // --- Cahaya ---
        if (currentLightCat != lastLightCat)
        {
            if (currentLightCat == pendingLightCat)
            {
                lightConfirmCount++;
                if (lightConfirmCount >= LIGHT_CONFIRM_NEEDED)
                {
                    Serial.printf("[✅ KONFIRMASI CAHAYA] %d → %d\n", lastLightCat, currentLightCat);
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

        // --- Suhu ---
        if (currentTempCat != lastTempCat)
        {
            if (currentTempCat == pendingTempCat)
            {
                tempConfirmCount++;
                if (tempConfirmCount >= TEMP_CONFIRM_NEEDED)
                {
                    Serial.printf("[✅ KONFIRMASI SUHU] %d → %d\n", lastTempCat, currentTempCat);
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
                    Serial.printf("[✅ KONFIRMASI NOISE] %d → %d\n", lastNoiseCat, currentNoiseCat);
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
    }

    if (!isEventConfirmed)
        return;

    // ====================================================================
    // TRIGGER AI (Selalu kirim payload lengkap, backend yang filter)
    // ====================================================================

    Serial.printf("[⚡ TRIGGER AI] Kirim: Suhu=%d°C, Cahaya=%d lux, Noise=%d dB | lastCondition='%s'\n",
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

    // ✨ FIX: Anti-spam ringan (1 detik) untuk mencegah duplikat JSON
    // Cooldown utama diatur oleh updateMemoryWithCooldown setelah AI merespons
    lastAiSpokeTime = millis();
    currentCooldownMs = 1000;
}

String aiSensor_getCurrentCondition()
{
    if (!pomodoro_isRunning())
    {
        return "";
    }
    return activeConditionFromAI;
}

void aiSensor_setToggle(const String &sensorType, bool enabled)
{
    if (sensorType == "temperature") {
        isTempEnabled = enabled;
        Serial.printf("[SENSOR TOGGLE] Suhu: %s\n", enabled ? "ON" : "OFF");
    } else if (sensorType == "light") {
        isLightEnabled = enabled;
        Serial.printf("[SENSOR TOGGLE] Cahaya: %s\n", enabled ? "ON" : "OFF");
    } else if (sensorType == "noise") {
        isNoiseEnabled = enabled;
        Serial.printf("[SENSOR TOGGLE] Suara: %s\n", enabled ? "ON" : "OFF");
    } else if (sensorType == "force_cold") {
        isForceColdEnabled = enabled;
        Serial.printf("[SENSOR TOGGLE] Force Cold: %s\n", enabled ? "ON" : "OFF");
    }
}