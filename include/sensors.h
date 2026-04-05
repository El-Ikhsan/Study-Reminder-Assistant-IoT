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

// Fungsi yang bisa diakses dari luar
void initSensors();
SensorData readAllSensors();

#endif