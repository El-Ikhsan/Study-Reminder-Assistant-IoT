#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <Arduino.h>
#include "sensor/sensors.h" // Butuh tipe data SensorData

void initWebSocket();
void wsLoop();
void sendPingWS();
void sendTelemetryWS(const SensorData &data);
extern bool wsConnected;
void sendRawWS(const String &msg);
void sendAudioChunkWS(const uint8_t *payload, size_t length);

// ✨ AMAN dipanggil dari dalam WebSocket callback (tidak acquire mutex)
// Pesan akan di-flush oleh wsLoop() setelah webSocket.loop() selesai
void queueSendWS(const String &msg);

#endif