#pragma once
#include <Arduino.h>

void initWakeNet();

// Panggil setMicMuted(true) saat speaker mulai play,
// setMicMuted(false) saat speaker selesai agar mic tidak self-feedback.
void setMicMuted(bool muted);

// Nyalakan/matikan WakeNet (wake word) tanpa mematikan mic/sensor.
void setWakeNetEnabled(bool enabled);