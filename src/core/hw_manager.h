#pragma once
#include <Arduino.h>

// Fungsi inisialisasi NVS Hardware
void initHardwareConfig();

// Fungsi Getter (Membaca)
int getSavedVolume();
int getSavedBrightness();

// Fungsi Setter (Menyimpan sekaligus mengubah nilai saat ini)
void updateVolume(int newVolume);
void updateBrightness(int newBrightness);