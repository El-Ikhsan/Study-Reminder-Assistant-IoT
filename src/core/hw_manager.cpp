#include "hw_manager.h"
#include "config.h"
#include "audio/audio.h" // Untuk fungsi setVolumePercent()
#include "ui/display.h"  // Untuk fungsi setDisplayBrightness()
#include <Preferences.h>

namespace
{
    Preferences hwPrefs;
    int currentVolume = 60;
    int currentBrightness = 80;
}

void initHardwareConfig()
{
    Serial.println("[HW] Mengambil pengaturan NVS Hardware...");

    // Buka brankas khusus hardware (hw_data) dengan mode Read-Only (false)
    hwPrefs.begin(RinchanConfig::Hardware::NVS_NAMESPACE, false);

    // Tarik data dari NVS, jika kosong gunakan DEFAULT
    currentVolume = hwPrefs.getInt(RinchanConfig::Hardware::KEY_VOLUME, RinchanConfig::Hardware::DEFAULT_VOLUME);
    currentBrightness = hwPrefs.getInt(RinchanConfig::Hardware::KEY_BRIGHTNESS, RinchanConfig::Hardware::DEFAULT_BRIGHTNESS);

    hwPrefs.end();

    Serial.printf("[HW] Volume Tersimpan: %d%%\n", currentVolume);
    Serial.printf("[HW] Brightness Tersimpan: %d%%\n", currentBrightness);
}

int getSavedVolume()
{
    return currentVolume;
}

int getSavedBrightness()
{
    return currentBrightness;
}

// Fungsi ini akan dipanggil oleh WebSocket saat ada perintah ubah volume
void updateVolume(int newVolume)
{
    // 1. Batasi angka dari 0 - 100
    currentVolume = constrain(newVolume, 0, 100);

    // 2. Terapkan langsung ke speaker
    setVolumePercent(currentVolume);

    // 3. Simpan permanen ke NVS
    hwPrefs.begin(RinchanConfig::Hardware::NVS_NAMESPACE, false);
    hwPrefs.putInt(RinchanConfig::Hardware::KEY_VOLUME, currentVolume);
    hwPrefs.end();

    Serial.printf("[HW] Volume disetel dan disimpan: %d%%\n", currentVolume);
}

// Fungsi ini akan dipanggil oleh WebSocket saat ada perintah ubah brightness
void updateBrightness(int newBrightness)
{
    currentBrightness = constrain(newBrightness, 0, 100);

    // Terapkan langsung ke layar
    setDisplayBrightness(currentBrightness);

    // Simpan permanen ke NVS
    hwPrefs.begin(RinchanConfig::Hardware::NVS_NAMESPACE, false);
    hwPrefs.putInt(RinchanConfig::Hardware::KEY_BRIGHTNESS, currentBrightness);
    hwPrefs.end();

    Serial.printf("[HW] Brightness disetel dan disimpan: %d%%\n", currentBrightness);
}