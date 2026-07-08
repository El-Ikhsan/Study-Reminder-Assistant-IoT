#include "websocket.h"
#include "config.h"
#include "network/auth.h"
#include "features/pomodoro.h"
#include "features/ai_sensor.h"
#include "ui/display.h"
#include "core/hw_manager.h"
#include "audio/sound_manager.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern String globalSensorAlert;
namespace
{
    WebSocketsClient webSocket;
    unsigned int wsFailCount = 0;
    SemaphoreHandle_t wsMutex = NULL;

    // OUTGOING QUEUE: Pesan yang akan dikirim setelah webSocket.loop() selesai
    // Digunakan agar callback tidak perlu acquire mutex (mencegah deadlock)
    String pendingSendMsg = "";
    bool hasPendingSend = false;

    // Helper: pilih begin() atau beginSSL() sesuai LOCAL_DEV_MODE
    void connectWebSocket(const char *url)
    {
#if LOCAL_DEV_MODE
        Serial.println("[WS] Mode LOKAL — menggunakan ws:// (tanpa SSL)");
        webSocket.begin(
            RinchanConfig::Backend::WS_HOST,
            RinchanConfig::Backend::WS_PORT,
            url);
#else
        Serial.println("[WS] Mode PRODUKSI — menggunakan wss:// (SSL)");
        webSocket.beginSSL(
            RinchanConfig::Backend::WS_HOST,
            RinchanConfig::Backend::WS_PORT,
            url);
#endif
    }
}

bool wsConnected = false;

void routeIncomingMessage(const String &msg)
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

    // 1. KIRIM KE DEPARTEMEN POMODORO
    if (type.indexOf("POMODORO") >= 0)
    {
        pomodoro_processCommand(type, payload);
    }

    // 2. KIRIM KE DEPARTEMEN AI & LAYAR
    else if (type == "AI_RESPONSE")
    {
        Serial.println("\n[🤖] Balasan AI (Rin-chan) Masuk!");
        String emotionStr = payload["emotion"].as<String>();
        String text = payload["text"].as<String>();
        Serial.printf("[AI] Emosi: %s | Teks: %s\n", emotionStr.c_str(), text.c_str());

        Emotion aiEmo = parseEmotionString(emotionStr);
        bool isSensorResponse = payload.containsKey("newCondition");
        bool isRecovery = isSensorResponse && (aiEmo == EMOTION_SMILE);

        forceClearDialog();

        if (isSensorResponse)
        {
            // ==========================================================
            // ✨ RESPONS SENSOR: Render emosi & kelola Top Bar + Lock
            // ==========================================================
            drawEmoji(aiEmo);

            String newCond = payload["newCondition"].as<String>();

            if (isRecovery)
            {
                // PEMULIHAN: Tampilkan label di Top Bar SELAMA dialog aktif
                if (newCond != "")
                    globalSensorAlert = newCond;
                forceUpdateTopBarAlert(globalSensorAlert);

                // Jeda animasi 2.5s agar user melihat Rinchan lega
                unsigned long reactionStart = millis();
                while (millis() - reactionStart < 2500)
                {
                    playDisplayAnimation();
                    delay(10);
                }
            }
            else
            {
                // INTERUPSI: Set alert, tampilkan di Top Bar selama belum dipulihkan
                if (newCond != "")
                    globalSensorAlert = newCond;
                forceUpdateTopBarAlert(globalSensorAlert);
            }

            // Tampilkan Dialog
            showDialogWidget(text);

            // Update memori sensor & cooldown
            aiSensor_updateMemoryWithCooldown(newCond, isRecovery);

            // SETELAH DIALOG: Reset UI jika pemulihan
            if (isRecovery)
            {
                drawEmoji(EMOTION_IDLE);
                forceUpdateTopBarAlert("");
                globalSensorAlert = "";
            }
        }
        else
        {
            // ==========================================================
            // ✨ RESPONS POMODORO: HANYA tampilkan dialog teks
            // JANGAN render emosi (agar tidak merusak animasi sensor/lock)
            // ==========================================================
            showDialogWidget(text); // Blocking — selesai = teks sudah ter-render

            // Setelah dialog fase awal selesai, aktifkan sensor interupsi
            // (aiSensor_updateMemory selalu clear waitingForAwalResponse)
            aiSensor_updateMemory("");
        }
    }
    // ✨ FIX: 3. KHUSUS UPDATE MEMORI SENSOR (Jika kondisi SAMA / AI Diam)
    else if (type == "UPDATE_SENSOR_STATE")
    {
        if (payload.containsKey("newCondition"))
        {
            aiSensor_updateMemory(payload["newCondition"].as<String>());
        }
    }

    // 4. KIRIM KE DEPARTEMEN HARDWARE (BRIGHTNESS)
    else if (type == "CMD_SET_BRIGHTNESS")
    {
        int newBrightness = payload["value"].as<int>();
        Serial.printf("\n[💡] Perintah ubah Brightness menjadi: %d%%\n", newBrightness);
        updateBrightness(newBrightness);

        // ✨ 1: Queue ACK (aman dari dalam callback, tidak deadlock mutex)
        JsonDocument ackDoc;
        ackDoc["type"] = "CMD_ACK";
        ackDoc["payload"]["command"] = "CMD_SET_BRIGHTNESS";
        String ackMsg;
        serializeJson(ackDoc, ackMsg);
        queueSendWS(ackMsg); // ← queueSendWS, bukan sendRawWS!

        // ✨ 2: Queue dialog agar wsLoop() tidak terblokir
        forceClearDialog();
        drawEmoji(EMOTION_IDLE);
        queueDialogWidget("Kecerahan: " + String(newBrightness) + "%");
    }

    // 5. KIRIM KE DEPARTEMEN HARDWARE (VOLUME)
    else if (type == "CMD_SET_VOLUME")
    {
        int newVolume = payload["value"].as<int>();
        Serial.printf("\n[🔊] Perintah ubah Volume menjadi: %d%%\n", newVolume);
        updateVolume(newVolume);

        // ✨ 1: Queue ACK (aman dari dalam callback, tidak deadlock mutex)
        JsonDocument ackDoc;
        ackDoc["type"] = "CMD_ACK";
        ackDoc["payload"]["command"] = "CMD_SET_VOLUME";
        String ackMsg;
        serializeJson(ackDoc, ackMsg);
        queueSendWS(ackMsg); // ← queueSendWS, bukan sendRawWS!

        // ✨ 2: Queue dialog agar wsLoop() tidak terblokir
        forceClearDialog();
        drawEmoji(EMOTION_IDLE);
        playRinchanSound(SND_AI_NOTIFY);
        queueDialogWidget("Volume Audio: " + String(newVolume) + "%");
    }

    // 6. KIRIM KE DEPARTEMEN SENSOR (TOGGLE)
    else if (type == "CMD_TOGGLE_SENSOR")
    {
        String sensor = payload["sensor"].as<String>();
        bool enabled = payload["enabled"].as<bool>();
        aiSensor_setToggle(sensor, enabled);

        // Queue ACK
        JsonDocument ackDoc;
        ackDoc["type"] = "CMD_ACK";
        ackDoc["payload"]["command"] = "CMD_TOGGLE_SENSOR";
        String ackMsg;
        serializeJson(ackDoc, ackMsg);
        queueSendWS(ackMsg);
    }
}

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    (void)length;

    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.println("[WS] Terputus dari server!");
        wsConnected = false;
        wsFailCount++;

        if (wsFailCount >= RinchanConfig::WebSocket::MAX_CONSECUTIVE_FAIL_BEFORE_REFRESH)
        {
            Serial.println("[WS] Gagal konek 3x berturut-turut. Memanggil tim medis...");
            refreshToken();
            wsFailCount = 0;

            String newToken = getApiKey();
            String fullUrl = String(RinchanConfig::Backend::WS_BASE_URL) + "?token=" + newToken;
            connectWebSocket(fullUrl.c_str());
        }
        break;

    case WStype_CONNECTED:
        Serial.println("[WS] Berhasil masuk ke DeviceRoom!");
        wsConnected = true;
        wsFailCount = 0;
        break;

    case WStype_TEXT:
    {
        String msg = String((char *)payload);
        if (msg == "pong")
        {
            Serial.println("[WS] <- Menerima: pong (Koneksi Stabil)");
            return;
        }

        // 🔥 LEMPAR KE PUSAT ROUTER 🔥
        routeIncomingMessage(msg);
        break;
    }
    case WStype_ERROR:
        Serial.println("[WS] Terjadi Error pada WebSocket!");
        wsConnected = false;
        break;

    case WStype_BIN:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
    case WStype_PING:
    case WStype_PONG:
        break;

    default:
        break;
    }
}

void initWebSocket()
{
    wsMutex = xSemaphoreCreateMutex();
    Serial.println("[WS] Menyiapkan URL dan Token...");
    String token = getApiKey();
    String fullUrl = String(RinchanConfig::Backend::WS_BASE_URL) + "?token=" + token;

    Serial.println("[WS] Menghubungkan ke Backend...");
    webSocket.setExtraHeaders(RinchanConfig::WebSocket::USER_AGENT);
    connectWebSocket(fullUrl.c_str());
    webSocket.enableHeartbeat(
        RinchanConfig::WebSocket::HEARTBEAT_INTERVAL_MS,
        RinchanConfig::WebSocket::HEARTBEAT_TIMEOUT_MS,
        RinchanConfig::WebSocket::HEARTBEAT_MAX_MISSED);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(RinchanConfig::WebSocket::RECONNECT_INTERVAL_MS);
}

void wsLoop()
{
    // Coba pasang gembok (tunggu 0ms). Kalau berhasil, jalankan loop.
    if (wsMutex != NULL && xSemaphoreTake(wsMutex, 0) == pdTRUE)
    {
        webSocket.loop();

        // ✨ Flush outgoing queue (dikirim dari dalam callback tanpa mutex)
        // Ini AMAN karena kita masih memegang mutex dan loop() sudah selesai
        if (hasPendingSend)
        {
            webSocket.sendTXT(pendingSendMsg.c_str());
            hasPendingSend = false;
            pendingSendMsg = "";
        }

        xSemaphoreGive(wsMutex); // Lepas gembok
    }
}

// ✨ Kirim pesan dari dalam WebSocket callback (TANPA acquire mutex)
// Pesan disimpan ke queue, di-flush oleh wsLoop() di iterasi yang sama
void queueSendWS(const String &msg)
{
    if (!wsConnected) return;
    pendingSendMsg = msg;
    hasPendingSend = true;
}

void sendPingWS()
{
    if (!wsConnected || wsMutex == NULL)
        return;

    // Tunggu sampai gembok terbuka (portMAX_DELAY), baru kirim data
    if (xSemaphoreTake(wsMutex, portMAX_DELAY) == pdTRUE)
    {
        Serial.println("[WS] -> Mengirim: ping");
        webSocket.sendTXT("ping");
        xSemaphoreGive(wsMutex);
    }
}

void sendRawWS(const String &msg)
{
    if (!wsConnected || wsMutex == NULL)
        return;

    if (xSemaphoreTake(wsMutex, portMAX_DELAY) == pdTRUE)
    {
        webSocket.sendTXT(msg.c_str());
        xSemaphoreGive(wsMutex);
    }
}

void sendAudioChunkWS(const uint8_t *payload, size_t length)
{
    if (!wsConnected || wsMutex == NULL)
        return;

    // Saat mengirim suara mic, pastikan Main Loop tidak sedang menyela
    if (xSemaphoreTake(wsMutex, portMAX_DELAY) == pdTRUE)
    {
        webSocket.sendBIN(payload, length);
        xSemaphoreGive(wsMutex);
    }
}

// (Jangan lupa bungkus juga sendTelemetryWS dengan logika xSemaphoreTake yang sama!)
void sendTelemetryWS(const SensorData &data)
{
    if (!wsConnected || wsMutex == NULL)
        return;

    JsonDocument doc;
    doc["type"] = "TELEMETRY_UPDATE";
    JsonObject payloadObj = doc["payload"].to<JsonObject>();
    // Kirim sebagai number agar frontend langsung bisa pakai tanpa parse string
    payloadObj["temperature"] = round(data.temperature * 100.0f) / 100.0f; // 2 desimal
    payloadObj["lightLux"] = (int)data.lightLux;
    payloadObj["noiseLevel"] = data.noiseLevel;
    String jsonString;
    serializeJson(doc, jsonString);

    if (xSemaphoreTake(wsMutex, portMAX_DELAY) == pdTRUE)
    {
        webSocket.sendTXT(jsonString);
        xSemaphoreGive(wsMutex);
    }
}