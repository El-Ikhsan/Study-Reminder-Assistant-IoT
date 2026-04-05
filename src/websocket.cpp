#include "websocket.h"
#include "config.h"
#include "auth_manager.h"
#include "pomodoro_ws.h" // <--- IMPORT ROUTER BARU
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

namespace
{
    WebSocketsClient webSocket;
    unsigned int wsFailCount = 0;
}

bool wsConnected = false;

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
            webSocket.beginSSL(RinchanConfig::Backend::WS_HOST, RinchanConfig::Backend::WS_PORT, fullUrl.c_str());
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

        // 🔥 LEMPAR PESAN KE FILE pomodoro_ws.cpp 🔥
        handleIncomingPomodoroMessage(msg);
        break;
    }
    case WStype_ERROR:
        Serial.println("[WS] Terjadi Error pada WebSocket!");
        wsConnected = false;
        break;
    }
}

void initWebSocket()
{
    Serial.println("[WS] Menyiapkan URL dan Token...");
    String token = getApiKey();
    String fullUrl = String(RinchanConfig::Backend::WS_BASE_URL) + "?token=" + token;

    Serial.println("[WS] Menghubungkan ke Backend WSS...");
    webSocket.setExtraHeaders(RinchanConfig::WebSocket::USER_AGENT);
    webSocket.beginSSL(RinchanConfig::Backend::WS_HOST, RinchanConfig::Backend::WS_PORT, fullUrl.c_str());
    webSocket.enableHeartbeat(
        RinchanConfig::WebSocket::HEARTBEAT_INTERVAL_MS,
        RinchanConfig::WebSocket::HEARTBEAT_TIMEOUT_MS,
        RinchanConfig::WebSocket::HEARTBEAT_MAX_MISSED);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(RinchanConfig::WebSocket::RECONNECT_INTERVAL_MS);
}

void wsLoop()
{
    webSocket.loop();
}

void sendPingWS()
{
    if (!wsConnected)
        return;
    Serial.println("[WS] -> Mengirim: ping");
    webSocket.sendTXT("ping");
}

// ✨ HELPER BARU: Dipanggil dari file lain untuk mengirim string JSON
void sendRawWS(const String &msg)
{
    if (!wsConnected)
        return;
    webSocket.sendTXT(msg.c_str());
}

void sendTelemetryWS(const SensorData &data)
{
    if (!wsConnected)
    {
        Serial.println("[WS] Koneksi putus. Menahan pengiriman Telemetri.");
        return;
    }

    JsonDocument doc;
    doc["type"] = "TELEMETRY_UPDATE";
    JsonObject payloadObj = doc["payload"].to<JsonObject>();

    payloadObj["temperature"] = serialized(String(data.temperature, 2));
    payloadObj["lightLux"] = serialized(String(data.lightLux, 2));
    payloadObj["noiseLevel"] = data.noiseLevel;

    String jsonString;
    serializeJson(doc, jsonString);

    webSocket.sendTXT(jsonString);
    // Print di-comment biar terminal tidak terlalu penuh tiap 15 detik
    // Serial.println("[WS] -> Telemetri Terkirim: " + jsonString);
}