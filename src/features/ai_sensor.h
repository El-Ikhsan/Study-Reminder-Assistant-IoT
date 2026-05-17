#pragma once
#include <Arduino.h>

void aiSensor_init();
void aiSensor_loop();

// Dipanggil oleh webSocket.cpp saat menerima balasan dari Backend
void aiSensor_updateMemory(const String &newCondition);
void aiSensor_forceReset();
String aiSensor_getCurrentCondition();