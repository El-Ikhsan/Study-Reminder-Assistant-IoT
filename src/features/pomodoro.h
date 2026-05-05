#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void pomodoro_processCommand(const String &type, JsonObject payload);
void pomodoroLoop();

// Getter untuk Modul Lain (khususnya ai_sensor)
bool pomodoro_isRunning();
String pomodoro_getSessionId();
int pomodoro_getCurrentCycle();
String pomodoro_getCurrentMode();
String pomodoro_getCurrentPhase();
String pomodoro_getMedia();