#pragma once
#include <Arduino.h>

void aiSensor_init();
void aiSensor_loop();

// Dipanggil oleh webSocket.cpp saat menerima balasan dari Backend
void aiSensor_updateMemory(const String &newCondition);
void aiSensor_updateMemoryWithCooldown(const String &newCondition, bool isRecovery);
void aiSensor_forceReset();
String aiSensor_getCurrentCondition();

// ✨ Fungsi untuk mematikan/menyalakan sensor secara spesifik (Kebutuhan Demo Sidang)
void aiSensor_setToggle(const String &sensorType, bool enabled);