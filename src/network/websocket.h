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

#endif