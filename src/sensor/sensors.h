#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

// Struct yang 100% cocok dengan payload Hono backend
struct SensorData
{
    float temperature;
    float lightLux;
    int noiseLevel;
};

// Inisialisasi semua sensor hardware (BMP280, BH1750, INMP441)
void initSensors();

// Baca seluruh data sensor sekaligus
SensorData readAllSensors();

#endif