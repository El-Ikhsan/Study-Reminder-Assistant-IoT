#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>

void initWiFi();
void handleWiFiLoop();
void clearWiFi();
bool isWiFiConnected();

#endif