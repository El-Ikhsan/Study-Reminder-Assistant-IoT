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

extern volatile int currentNoiseLevel;

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
    data.temperature = bmp.readTemperature() - 5.0f;
    data.lightLux = lightMeter.readLightLevel();
    data.noiseLevel = currentNoiseLevel; // Ambil nilai asli dari kalkulasi Mikrofon INMP441
    return data;
}