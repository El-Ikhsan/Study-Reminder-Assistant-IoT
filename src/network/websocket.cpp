#include "websocket.h"
#include "config.h"
#include "network/auth.h"
#include "features/pomodoro.h"
#include "ui/display.h"
#include "core/hw_manager.h"
#include "audio/sound_manager.h"

#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace
{
    WebSocketsClient webSocket;
    unsigned int wsFailCount = 0;
    SemaphoreHandle_t wsMutex = NULL;
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

        forceClearDialog();
        drawEmoji(parseEmotionString(emotionStr)); // Panggil fungsi dari display.h
        showDialogWidget(text);
    }

    // 3. KIRIM KE DEPARTEMEN HARDWARE (BRIGHTNESS)
    else if (type == "CMD_SET_BRIGHTNESS")
    {
        int newBrightness = payload["value"].as<int>();
        Serial.printf("\n[💡] Perintah ubah Brightness menjadi: %d%%\n", newBrightness);
        updateBrightness(newBrightness);

        forceClearDialog();
        drawEmoji(EMOTION_SURPRISED);
        showDialogWidget("Kecerahan: " + String(newBrightness) + "%");
    }

    // 4. KIRIM KE DEPARTEMEN HARDWARE (VOLUME)
    else if (type == "CMD_SET_VOLUME")
    {
        int newVolume = payload["value"].as<int>();
        Serial.printf("\n[🔊] Perintah ubah Volume menjadi: %d%%\n", newVolume);
        updateVolume(newVolume);

        forceClearDialog();
        drawEmoji(EMOTION_LISTENING);
        playRinchanSound(SND_AI_NOTIFY);
        showDialogWidget("Volume Audio: " + String(newVolume) + "%");
    }
    else
    {
        Serial.printf("[WS] Tipe perintah tidak dikenal: %s\n", type.c_str());
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
    // Coba pasang gembok (tunggu 0ms). Kalau berhasil, jalankan loop.
    if (wsMutex != NULL && xSemaphoreTake(wsMutex, 0) == pdTRUE)
    {
        webSocket.loop();
        xSemaphoreGive(wsMutex); // Lepas gembok
    }
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
    payloadObj["temperature"] = serialized(String(data.temperature, 2));
    payloadObj["lightLux"] = serialized(String(data.lightLux, 2));
    payloadObj["noiseLevel"] = data.noiseLevel;
    String jsonString;
    serializeJson(doc, jsonString);

    if (xSemaphoreTake(wsMutex, portMAX_DELAY) == pdTRUE)
    {
        webSocket.sendTXT(jsonString);
        xSemaphoreGive(wsMutex);
    }
}