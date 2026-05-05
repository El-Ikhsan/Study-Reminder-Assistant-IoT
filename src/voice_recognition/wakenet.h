#pragma once
#include <Arduino.h>

void initWakeNet();

// Panggil setMicMuted(true) saat speaker mulai play,
// setMicMuted(false) saat speaker selesai agar mic tidak self-feedback.
void setMicMuted(bool muted);