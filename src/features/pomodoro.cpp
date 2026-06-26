#include "pomodoro.h"
#include "network/websocket.h"
#include "ui/display.h"
#include "core/hw_manager.h"
#include "audio/sound_manager.h"
#include "features/ai_sensor.h"
#include <ArduinoJson.h>

extern String globalSensorAlert;
namespace
{
    constexpr unsigned long ONE_SECOND_MS = 1000;
    constexpr int DEFAULT_FOCUS_DURATION_MIN = 25;
    constexpr int DEFAULT_REST_DURATION_MIN = 5;
    constexpr int DEFAULT_TARGET_CYCLES = 1;

    struct PomodoroState
    {
        bool isRunning = false;
        String sessionId = "";

        String media = "Laptop";
        int focusDurationMin = DEFAULT_FOCUS_DURATION_MIN;
        int restDurationMin = DEFAULT_REST_DURATION_MIN;
        int targetCycles = DEFAULT_TARGET_CYCLES;

        String mode = "fokus";
        String phase = "awal";
        int currentCycle = 1;

        unsigned long timeRemainingSec = 0;
        unsigned long durationTotalSec = 0;
        unsigned long lastTimerTick = 0;

        bool reportedAwal = false;
        bool reportedTengah = false;
        bool reportedAkhir = false;
    };

    PomodoroState state;

    // ✨ FLAG DEFERRED START: Timer belum benar-benar dimulai sampai pomodoroLoop()
    // mendeteksinya. Ini memastikan ACK ter-flush SEBELUM timer fisik dimulai,
    // sehingga frontend dan IoT mulai dalam waktu yang hampir bersamaan.
    bool pendingTimerStart = false;

    void sendPhaseReport(const String &mode, float durationMin, float remainingMin);

    void startTimerForMode(const String &mode, int durationMin)
    {
        state.mode = mode;
        state.durationTotalSec = durationMin * 60;
        state.timeRemainingSec = state.durationTotalSec;

        state.reportedAwal = false;
        state.reportedTengah = false;
        state.reportedAkhir = false;

        Serial.printf("\n[⏳] Memulai Mode: %s | Durasi: %d menit | Siklus: %d/%d | Media: %s\n",
                      state.mode.c_str(), durationMin, state.currentCycle, state.targetCycles, state.media.c_str());

        // ✨ FIX: SFX BLOCKING agar tidak dibunuh oleh showDialogWidget
        // saat AI merespons fase awal sesaat setelah timer dimulai
        if (mode == "fokus")
        {
            playRinchanSoundBlocking(SND_POMO_START);
        }
        else
        {
            playRinchanSoundBlocking(SND_POMO_SWITCH);
        }

        // Pengiriman Fase Awal DIHAPUS dari sini agar tidak Stack Overflow!
    }

    void switchPomodoroMode()
    {
        if (state.mode == "fokus")
        {
            Serial.println("[✅] Sesi Fokus Selesai!");

            // ✨ LOGIKA BARU: Cek apakah ini siklus fokus TERAKHIR
            // Jika iya, LANGSUNG selesaikan sesi tanpa fase istirahat
            if (state.currentCycle >= state.targetCycles)
            {
                Serial.println("[🎉] SEMUA SIKLUS POMODORO SELESAI! (Tanpa istirahat akhir)");
                state.isRunning = false;
                globalSensorAlert = "";

                playRinchanAlarm(1);

                clearWidget();
                forceClearDialog();
                drawEmoji(EMOTION_IDLE);

                if (wsConnected && !state.sessionId.isEmpty())
                {
                    JsonDocument doc;
                    doc["type"] = "SESSION_COMPLETED";
                    doc["payload"]["sessionId"] = state.sessionId;
                    doc["payload"]["currentCycle"] = state.targetCycles;
                    doc["payload"]["media"] = state.media;

                    String jsonString;
                    serializeJson(doc, jsonString);
                    sendRawWS(jsonString);
                }

                state.sessionId = "";
            }
            else
            {
                // Masih ada siklus berikutnya, masuk ke fase istirahat
                startTimerForMode("istirahat", state.restDurationMin);
            }
        }
        else if (state.mode == "istirahat")
        {
            Serial.println("[✅] Sesi Istirahat Selesai!");
            state.currentCycle++;

            // Setelah istirahat, SELALU mulai fokus berikutnya
            // (karena istirahat terakhir sudah tidak ada — langsung selesai dari fokus)
            startTimerForMode("fokus", state.focusDurationMin);
        }
    }

    void sendPhaseReport(const String &mode, float durationMin, float remainingMin)
    {
        if (!wsConnected || state.sessionId.isEmpty())
            return;

        JsonDocument doc;
        doc["type"] = "PHASE_REPORT";
        JsonObject payload = doc["payload"].to<JsonObject>();

        payload["sessionId"] = state.sessionId;
        payload["currentCycle"] = state.currentCycle;
        payload["mode"] = mode;
        payload["phase"] = state.phase;
        payload["media"] = state.media;
        payload["durationMin"] = durationMin;
        payload["remainingMin"] = remainingMin;

        String jsonString;
        serializeJson(doc, jsonString);
        sendRawWS(jsonString);
    }

} // end namespace

void pomodoro_processCommand(const String &type, JsonObject payload)
{
    if (type == "CMD_START_POMODORO")
    {
        Serial.println("\n[\u25b6\ufe0f] Perintah START diterima dari Dashboard!");

        state.sessionId = payload["sessionId"].as<String>();
        state.focusDurationMin = payload["focusDuration"] | DEFAULT_FOCUS_DURATION_MIN;

        int restDur = payload["restDuration"];
        int breakDur = payload["breakDuration"];
        if (restDur > 0)
            state.restDurationMin = restDur;
        else if (breakDur > 0)
            state.restDurationMin = breakDur;
        else
            state.restDurationMin = DEFAULT_REST_DURATION_MIN;

        state.targetCycles = payload["cycles"] | DEFAULT_TARGET_CYCLES;
        state.media = payload["media"] | "Laptop";
        state.currentCycle = 1;

        // ✨ TIDAK langsung jalankan timer di sini.
        // Set flag agar pomodoroLoop() yang menjalankan setelah ACK ter-flush.
        pendingTimerStart = true;
        state.isRunning = false; // Belum running, timer mulai di pomodoroLoop

        // Queue ACK segera (tidak blocking, akan di-flush wsLoop iterasi ini)
        JsonDocument ackDoc;
        ackDoc["type"] = "CMD_ACK";
        ackDoc["payload"]["command"] = "CMD_START_POMODORO";
        
        // Ambil waktu persis di device (NTP ms)
        struct timeval tv;
        gettimeofday(&tv, NULL);
        long long exactTimeMs = (long long)tv.tv_sec * 1000LL + (tv.tv_usec / 1000LL);
        ackDoc["payload"]["startedAt"] = exactTimeMs;

        String ackMsg;
        serializeJson(ackDoc, ackMsg);
        queueSendWS(ackMsg);

        forceClearDialog();
        drawEmoji(EMOTION_IDLE);
        // RETURN dari callback. wsLoop() akan flush ACK, lalu pomodoroLoop() mulai timer.
    }
    else if (type == "CMD_STOP_POMODORO")
    {
        Serial.println("\n[\u23f9\ufe0f] Perintah STOP diterima! Menghentikan Timer.");
        state.isRunning = false;
        state.sessionId = "";
        pendingTimerStart = false; // Batalkan pending start jika ada

        // Queue ACK segera (flush oleh wsLoop)
        JsonDocument ackDoc;
        ackDoc["type"] = "CMD_ACK";
        ackDoc["payload"]["command"] = "CMD_STOP_POMODORO";
        String ackMsg;
        serializeJson(ackDoc, ackMsg);
        queueSendWS(ackMsg);

        clearWidget();
        forceClearDialog();
        drawEmoji(EMOTION_IDLE);

        // Non-blocking sound agar ACK bisa ter-flush dulu
        // Dialog di-queue agar wsLoop() tidak terblokir
        playRinchanSound(SND_POMO_CANCEL);
        queueDialogWidget("Yah, dibatalkan...");
    }
}

void pomodoroLoop()
{
    // ✨ DEFERRED START: Jalankan timer SETELAH ACK ter-flush oleh wsLoop()
    // Ini dipanggil di iterasi loop() berikutnya setelah CMD_START_POMODORO
    if (pendingTimerStart)
    {
        pendingTimerStart = false;
        aiSensor_forceReset();

        // Mulai timer — lastTimerTick di-set SEKARANG, setelah ACK sudah dikirim
        state.isRunning = true;
        state.currentCycle = 1;
        state.lastTimerTick = millis();

        Serial.println("[⏱️] Timer dimulai (setelah ACK ter-flush ke backend)");
        startTimerForMode("fokus", state.focusDurationMin);
        return; // startTimerForMode sudah setup state, loop berikutnya akan jalan normal
    }

    if (!state.isRunning)
        return;

    // ✨ FIX 2: TRIGGER FASE AWAL SECARA INSTAN! (Ditaruh di luar timer 1 detik)
    // AI akan diberitahu detik itu juga tanpa menyebabkan tumpukan memori di WS.
    if (!state.reportedAwal)
    {
        float durationMinFloat = state.durationTotalSec / 60.0;
        float remainingMinFloat = state.timeRemainingSec / 60.0;

        state.phase = "awal";
        sendPhaseReport(state.mode, durationMinFloat, remainingMinFloat);
        state.reportedAwal = true;
    }

    unsigned long currentMillis = millis();
    unsigned long elapsed = currentMillis - state.lastTimerTick;

    if (elapsed >= ONE_SECOND_MS)
    {
        int missedSeconds = elapsed / ONE_SECOND_MS;
        state.lastTimerTick += (missedSeconds * ONE_SECOND_MS);

        if (state.timeRemainingSec > 0)
        {
            if (state.timeRemainingSec >= missedSeconds)
            {
                state.timeRemainingSec -= missedSeconds;
            }
            else
            {
                state.timeRemainingSec = 0;
            }

            int minRemaining = state.timeRemainingSec / 60;
            int secRemaining = state.timeRemainingSec % 60;
            updatePomodoroWidget(minRemaining, secRemaining, (state.mode == "istirahat"), state.currentCycle, state.media);

            float ratio = (float)state.timeRemainingSec / (float)state.durationTotalSec;
            float durationMinFloat = state.durationTotalSec / 60.0;
            float remainingMinFloat = state.timeRemainingSec / 60.0;

            // FASE TENGAH (50%) -> Hanya untuk mode Fokus
            if (state.mode == "fokus" && ratio <= 0.50 && !state.reportedTengah)
            {
                state.phase = "tengah";
                sendPhaseReport(state.mode, durationMinFloat, remainingMinFloat);
                state.reportedTengah = true;
            }

            // FASE AKHIR (10%) -> Fokus & Istirahat
            if (ratio <= 0.10 && !state.reportedAkhir)
            {
                state.phase = "akhir";
                sendPhaseReport(state.mode, durationMinFloat, remainingMinFloat);
                state.reportedAkhir = true;
            }
        }
        else
        {
            switchPomodoroMode();
        }
    }
}

// ==========================================
// ✨ GETTERS UNTUK MODUL AI SENSOR
// ==========================================
bool pomodoro_isRunning() { return state.isRunning; }
String pomodoro_getSessionId() { return state.sessionId; }
int pomodoro_getCurrentCycle() { return state.currentCycle; }
String pomodoro_getCurrentMode() { return state.mode; }
String pomodoro_getCurrentPhase() { return state.phase; }
String pomodoro_getMedia() { return state.media; }