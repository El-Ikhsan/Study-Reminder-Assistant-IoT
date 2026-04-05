#include "pomodoro_ws.h"
#include "websocket.h"
#include "sensors.h"
#include <ArduinoJson.h>

namespace
{
    constexpr unsigned long ONE_SECOND_MS = 1000;
    constexpr unsigned long DEFAULT_SENSOR_INTERVAL_MS = 60000;
    constexpr int DEFAULT_FOCUS_DURATION_MIN = 25;
    constexpr int DEFAULT_REST_DURATION_MIN = 5;
    constexpr int DEFAULT_TARGET_CYCLES = 1;
    constexpr int DEFAULT_SENSOR_INTERVAL_SEC = 60;

    // ==========================================
    // 🧠 STATE MANAGEMENT (TERISOLASI)
    // ==========================================
    // Kita bungkus semua variabel ke dalam satu Object (Struct)
    struct PomodoroState
    {
        bool isRunning = false;
        String sessionId = "";

        // Data Dinamis dari Dashboard
        String condition = "normal";
        int focusDurationMin = DEFAULT_FOCUS_DURATION_MIN;
        int restDurationMin = DEFAULT_REST_DURATION_MIN;
        int targetCycles = DEFAULT_TARGET_CYCLES;
        unsigned long sensorIntervalMs = DEFAULT_SENSOR_INTERVAL_MS;

        // Status Berjalan
        String mode = "fokus";
        String phase = "awal";
        int currentCycle = 1;

        // Variabel Timer
        unsigned long timeRemainingSec = 0;
        unsigned long durationTotalSec = 0;
        unsigned long lastTimerTick = 0;
        unsigned long lastSensorTick = 0;

        // Flag Laporan
        bool reportedAwal = false;
        bool reportedTengah = false;
        bool reportedAkhir = false;
    };

    PomodoroState state;

    // ==========================================
    // 🔄 FUNGSI INTERNAL PENGATUR SIKLUS
    // ==========================================
    // Gunakan 'static' pada fungsi internal agar tidak bentrok dengan file lain
    void startTimerForMode(const String &mode, int durationMin)
    {
        state.mode = mode;
        state.durationTotalSec = durationMin * 60;
        state.timeRemainingSec = state.durationTotalSec;

        state.reportedAwal = false;
        state.reportedTengah = false;
        state.reportedAkhir = false;

        Serial.printf("\n[⏳] Memulai Mode: %s | Durasi: %d menit | Siklus: %d/%d\n",
                      state.mode.c_str(), durationMin, state.currentCycle, state.targetCycles);
    }

    void sendPhaseReport(const String &mode, float durationMin, float remainingMin, const String &condition); // Deklarasi maju

    void switchPomodoroMode()
    {
        if (state.mode == "fokus")
        {
            Serial.println("[✅] Sesi Fokus Selesai!");
            startTimerForMode("istirahat", state.restDurationMin);
        }
        else if (state.mode == "istirahat")
        {
            Serial.println("[✅] Sesi Istirahat Selesai!");
            state.currentCycle++;

            if (state.currentCycle > state.targetCycles)
            {
                Serial.println("[🎉] SEMUA SIKLUS POMODORO SELESAI!");
                state.isRunning = false;

                if (wsConnected && !state.sessionId.isEmpty())
                {
                    JsonDocument doc;
                    doc["type"] = "SESSION_COMPLETED";
                    doc["payload"]["sessionId"] = state.sessionId;
                    String jsonString;
                    serializeJson(doc, jsonString);
                    sendRawWS(jsonString);
                }

                state.sessionId = "";
            }
            else
            {
                startTimerForMode("fokus", state.focusDurationMin);
            }
        }
    }

    // ==========================================
    // 📤 PENGIRIM PESAN KE SERVER
    // ==========================================
    void sendSensorReportForAI(float temp, float lux, int noise)
    {
        if (!wsConnected || !state.isRunning || state.sessionId.isEmpty())
            return;

        JsonDocument doc;
        doc["type"] = "SENSOR_REPORT_FOR_AI";
        JsonObject payload = doc["payload"].to<JsonObject>();

        payload["sessionId"] = state.sessionId;
        payload["currentCycle"] = state.currentCycle;
        payload["mode"] = state.mode;
        payload["phase"] = state.phase;

        payload["temperature"] = temp;
        payload["lightLux"] = lux;
        payload["noiseLevel"] = noise;

        String jsonString;
        serializeJson(doc, jsonString);
        sendRawWS(jsonString);
    }

    void sendPhaseReport(const String &mode, float durationMin, float remainingMin, const String &condition)
    {
        if (!wsConnected || state.sessionId.isEmpty())
            return;

        JsonDocument doc;
        doc["type"] = "PHASE_REPORT";
        JsonObject payload = doc["payload"].to<JsonObject>();

        payload["sessionId"] = state.sessionId;
        payload["currentCycle"] = state.currentCycle;

        payload["mode"] = mode;
        payload["durationMin"] = durationMin;
        payload["remainingMin"] = remainingMin;
        payload["condition"] = condition;

        String jsonString;
        serializeJson(doc, jsonString);
        sendRawWS(jsonString);
    }

} // namespace

// ==========================================
// 📥 1. ROUTER PESAN MASUK DARI SERVER (Public)
// ==========================================
void handleIncomingPomodoroMessage(const String &msg)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);
    if (error)
        return;

    String type = doc["type"].as<String>();
    if (type == "null" || type == "")
    {
        type = doc["command"].as<String>();
    }

    JsonObject payload = doc["payload"];

    if (type == "CMD_START_POMODORO")
    {
        Serial.println("\n[▶️] Perintah START diterima dari Dashboard!");

        state.sessionId = payload["sessionId"].as<String>();
        state.focusDurationMin = payload["focusDuration"] | DEFAULT_FOCUS_DURATION_MIN;
        state.restDurationMin = payload["breakDuration"] | DEFAULT_REST_DURATION_MIN;
        state.targetCycles = payload["cycles"] | DEFAULT_TARGET_CYCLES;
        state.condition = payload["mode"] | "normal";

        int intervalSec = payload["sensorIntervalSec"] | DEFAULT_SENSOR_INTERVAL_SEC;
        state.sensorIntervalMs = intervalSec * 1000;

        Serial.printf("Setup: Kondisi %s, Interval AI %d detik.\n", state.condition.c_str(), intervalSec);

        state.isRunning = true;
        state.currentCycle = 1;

        startTimerForMode("fokus", state.focusDurationMin);
    }
    else if (type == "CMD_STOP_POMODORO")
    {
        Serial.println("\n[⏹️] Perintah STOP diterima! Menghentikan Timer.");
        state.isRunning = false;
        state.sessionId = "";
    }
    else if (type == "AI_RESPONSE")
    {
        Serial.println("\n[🤖] Balasan AI (Rin-chan) Masuk!");
        String emotion = payload["emotion"].as<String>();
        String text = payload["text"].as<String>();
        Serial.println("Ekspresi: " + emotion);
        Serial.println("Pesan: " + text);
    }
}

// ==========================================
// ⚙️ 2. MESIN TIMER UTAMA (Public)
// ==========================================
void pomodoroLoop()
{
    if (!state.isRunning)
        return;

    unsigned long currentMillis = millis();

    // A. LOGIKA PENGHITUNG WAKTU (Jalan Setiap 1 Detik)
    if (currentMillis - state.lastTimerTick >= ONE_SECOND_MS)
    {
        state.lastTimerTick = currentMillis;

        if (state.timeRemainingSec > 0)
        {
            state.timeRemainingSec--;

            float ratio = (float)state.timeRemainingSec / (float)state.durationTotalSec;
            float durationMinFloat = state.durationTotalSec / 60.0;
            float remainingMinFloat = state.timeRemainingSec / 60.0;

            if (!state.reportedAwal)
            {
                state.phase = "awal";
                sendPhaseReport(state.mode, durationMinFloat, remainingMinFloat, state.condition);
                state.reportedAwal = true;
            }
            else if (ratio <= 0.50 && !state.reportedTengah)
            {
                state.phase = "tengah";
                sendPhaseReport(state.mode, durationMinFloat, remainingMinFloat, state.condition);
                state.reportedTengah = true;
            }
            else if (ratio <= 0.10 && !state.reportedAkhir)
            {
                state.phase = "akhir";
                sendPhaseReport(state.mode, durationMinFloat, remainingMinFloat, state.condition);
                state.reportedAkhir = true;
            }
        }
        else
        {
            switchPomodoroMode();
        }
    }

    // B. LOGIKA SENSOR AI
    if (currentMillis - state.lastSensorTick >= state.sensorIntervalMs)
    {
        state.lastSensorTick = currentMillis;
        SensorData currentData = readAllSensors();
        sendSensorReportForAI(currentData.temperature, currentData.lightLux, currentData.noiseLevel);
    }
}