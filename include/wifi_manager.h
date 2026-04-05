#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

void initWiFi();
void handleWiFiLoop();
void clearWiFi();
bool isWiFiConnected();

#endif