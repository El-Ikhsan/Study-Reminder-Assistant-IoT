#pragma once
#include <Arduino.h>
#include <ArduinoJson.h> // Wajib ada untuk JsonObject

// Fungsi khusus untuk menerima perintah Pomodoro saja
void pomodoro_processCommand(const String &type, JsonObject payload);
void pomodoroLoop();