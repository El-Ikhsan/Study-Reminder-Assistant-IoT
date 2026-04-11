#ifndef POMODORO_H
#define POMODORO_H
#include <Arduino.h>

// Hanya 2 fungsi ini yang diizinkan diakses oleh file lain (Public)
void handleIncomingPomodoroMessage(const String &msg);
void pomodoroLoop();

#endif