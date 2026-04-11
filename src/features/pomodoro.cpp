#include "pomodoro.h"
#include "network/websocket.h"
#include "sensor/sensors.h"
#include "ui/display.h"
#include "core/hw_manager.h"
#include "audio/sound_manager.h"
#include <ArduinoJson.h>

namespace
{
    constexpr unsigned long ONE_SECOND_MS = 1000;
    constexpr unsigned long DEFAULT_SENSOR_INTERVAL_MS = 60000;
    constexpr int DEFAULT_FOCUS_DURATION_MIN = 25;
    constexpr int DEFAULT_REST_DURATION_MIN = 5;
    constexpr int DEFAULT_TARGET_CYCLES = 1;
    constexpr int DEFAULT_SENSOR_INTERVAL_SEC = 60;

    struct PomodoroState
    {
        bool isRunning = false;
        String sessionId = "";

        String condition = "normal";
        int focusDurationMin = DEFAULT_FOCUS_DURATION_MIN;
        int restDurationMin = DEFAULT_REST_DURATION_MIN;
        int targetCycles = DEFAULT_TARGET_CYCLES;
        unsigned long sensorIntervalMs = DEFAULT_SENSOR_INTERVAL_MS;

        String mode = "fokus";
        String phase = "awal";
        int currentCycle = 1;

        unsigned long timeRemainingSec = 0;
        unsigned long durationTotalSec = 0;
        unsigned long lastTimerTick = 0;
        unsigned long lastSensorTick = 0;

        bool reportedAwal = false;
        bool reportedTengah = false;
        bool reportedAkhir = false;
    };

    PomodoroState state;

    // ✨ HELPER: Penerjemah String JSON ke Enum Layar
    Emotion parseEmotionString(String emoStr)
    {
        emoStr.toUpperCase(); // Pastikan huruf besar semua untuk pencocokan
        if (emoStr == "HOT")
            return EMOTION_HOT;
        if (emoStr == "COLD")
            return EMOTION_COLD;
        if (emoStr == "NOISY")
            return EMOTION_NOISY;
        if (emoStr == "SLEEPY")
            return EMOTION_SLEEPY;
        if (emoStr == "SURPRISED")
            return EMOTION_SURPRISED;
        if (emoStr == "DARK")
            return EMOTION_DARK;
        if (emoStr == "SAD")
            return EMOTION_SAD;
        if (emoStr == "LISTENING")
            return EMOTION_LISTENING;
        if (emoStr == "UNCOMFORTABLE")
            return EMOTION_UNCOMFORTABLE;

        return EMOTION_IDLE; // Wajah default jika string tidak dikenali
    }

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

        // ✨ TRIGGER SUARA SESI
        if (mode == "fokus")
        {
            playRinchanSound(SND_POMO_START);
        }
        else
        {
            playRinchanSound(SND_POMO_SWITCH);
        }
    }

    void sendPhaseReport(const String &mode, float durationMin, float remainingMin, const String &condition);

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

                playRinchanSound(SND_POMO_STOP); // ✨ Suara Selesai Total

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

                // Kembalikan wajah ke normal setelah selesai
                forceClearDialog();
                drawEmoji(EMOTION_IDLE);
                showDialogWidget("Kerja Bagus, Shimarin!");
            }
            else
            {
                startTimerForMode("fokus", state.focusDurationMin);
            }
        }
    }

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

        state.isRunning = true;
        state.currentCycle = 1;

        // ✨ Kosongkan dialog jika AI masih ngomong, ubah wajah ke mode fokus
        forceClearDialog();
        drawEmoji(EMOTION_LISTENING);

        startTimerForMode("fokus", state.focusDurationMin);
    }
    else if (type == "CMD_STOP_POMODORO")
    {
        Serial.println("\n[⏹️] Perintah STOP diterima! Menghentikan Timer.");
        state.isRunning = false;
        state.sessionId = "";

        // ✨ Eksekusi UI Batal
        playRinchanSound(SND_POMO_CANCEL);
        forceClearDialog();
        drawEmoji(EMOTION_SAD);
        showDialogWidget("Yah, dibatalkan...");
    }
    else if (type == "AI_RESPONSE")
    {
        Serial.println("\n[🤖] Balasan AI (Rin-chan) Masuk!");
        String emotionStr = payload["emotion"].as<String>();
        String text = payload["text"].as<String>();

        Serial.printf("Ekspresi: %s | Pesan: %s\n", emotionStr.c_str(), text.c_str());

        // ✨ EKSEKUSI ANIMASI WAJAH DAN TEKS
        forceClearDialog();                        // Matikan ngetik lama jika ada
        drawEmoji(parseEmotionString(emotionStr)); // Ubah wajah
        showDialogWidget(text);                    // Mulai ngetik baru
    }
    else if (type == "CMD_SET_BRIGHTNESS")
    {
        int newBrightness = payload["value"].as<int>();
        Serial.printf("\n[💡] Perintah ubah Brightness menjadi: %d%%\n", newBrightness);

        // ✨ THE MAGIC: Langsung ubah layar & simpan ke NVS!
        updateBrightness(newBrightness);

        // ✨ Feedback UI: Hentikan ngetik lama, tampilkan info
        forceClearDialog();
        drawEmoji(EMOTION_SURPRISED);
        showDialogWidget("Kecerahan: " + String(newBrightness) + "%");
    }
    else if (type == "CMD_SET_VOLUME")
    {
        int newVolume = payload["value"].as<int>();
        Serial.printf("\n[🔊] Perintah ubah Volume menjadi: %d%%\n", newVolume);

        // ✨ THE MAGIC: Langsung ubah MAX98357A & simpan ke NVS!
        updateVolume(newVolume);

        // ✨ Feedback Audio & UI: Beri suara tes agar user tahu sekeras apa
        forceClearDialog();
        drawEmoji(EMOTION_LISTENING);
        playRinchanSound(SND_AI_NOTIFY); // Bunyi "Ting!" untuk tes
        showDialogWidget("Volume Audio: " + String(newVolume) + "%");
    }
}

void pomodoroLoop()
{
    if (!state.isRunning)
        return;

    unsigned long currentMillis = millis();

    // A. LOGIKA PENGHITUNG WAKTU
    if (currentMillis - state.lastTimerTick >= ONE_SECOND_MS)
    {
        state.lastTimerTick = currentMillis;

        if (state.timeRemainingSec > 0)
        {
            state.timeRemainingSec--;

            // ✨ UPDATE TIMER KE LAYAR SETIAP DETIK
            int minRemaining = state.timeRemainingSec / 60;
            int secRemaining = state.timeRemainingSec % 60;
            updatePomodoroWidget(minRemaining, secRemaining, (state.mode == "istirahat"));

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