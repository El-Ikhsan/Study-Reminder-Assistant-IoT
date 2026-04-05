#include "sensors.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>

namespace
{
    Adafruit_BMP280 bmp;
    BH1750 lightMeter;
}

void initSensors()
{
    Wire.begin(RinchanConfig::Pins::I2C_SDA, RinchanConfig::Pins::I2C_SCL);

    Serial.println("[SENSOR] Menginisialisasi hardware...");

    // Setup Suhu
    if (!bmp.begin(0x76))
    {
        Serial.println("[ERROR] BMP280 Gagal!");
    }
    else
    {
        Serial.println("[OK] BMP280 Siap.");
    }

    // Setup Cahaya
    if (!lightMeter.begin())
    {
        Serial.println("[ERROR] BH1750 Gagal!");
    }
    else
    {
        Serial.println("[OK] BH1750 Siap.");
    }
}

SensorData readAllSensors()
{
    SensorData data;

    // Baca sensor asli
    data.temperature = bmp.readTemperature();
    data.lightLux = lightMeter.readLightLevel();

    // MOCK: Noise Level (Belum pakai I2S Mic, jadi pakai random 30-80)
    // Nanti logika I2S RMS ditaruh di sini
    data.noiseLevel = random(30, 80);

    return data;
}