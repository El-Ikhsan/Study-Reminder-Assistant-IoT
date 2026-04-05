#include "button_manager.h"
#include "config.h"
#include "auth_manager.h"
#include "wifi_manager.h"
#include <Arduino.h>
#include "driver/rtc_io.h" // ✨ WAJIB UNTUK MENGUNCI PULL-UP SAAT DEEP SLEEP

namespace
{
    unsigned long buttonPressTime = 0;
    bool isButtonPressed = false;
    bool actionHandled = false;

    void enterDeepSleep()
    {
        // ✨ FIX 1: TAHAN PROSES SAMPAI JARI MAJIKAN BENAR-BENAR DIANGKAT
        while (digitalRead(RinchanConfig::Pins::BUTTON_PIN) == LOW)
        {
            delay(10); // Tunggu sampai tombol dilepas (HIGH)
        }

        Serial.println("[SYSTEM] Masuk ke Deep Sleep. Zzz...");
        delay(100); // Beri waktu Serial print selesai sebelum modar

        // ✨ FIX 2: KUNCI PULL-UP INTERNAL RTC AGAR PIN TIDAK MENGAMBANG
        rtc_gpio_pullup_en((gpio_num_t)RinchanConfig::Pins::BUTTON_PIN);
        rtc_gpio_pulldown_dis((gpio_num_t)RinchanConfig::Pins::BUTTON_PIN);

        // Atur agar GPIO 18 (LOW / Ditekan) bisa membangunkan ESP32-S3
        esp_sleep_enable_ext0_wakeup((gpio_num_t)RinchanConfig::Pins::BUTTON_PIN, 0);
        esp_deep_sleep_start();
    }
}

void initButton()
{
    pinMode(RinchanConfig::Pins::BUTTON_PIN, INPUT_PULLUP);
    pinMode(RinchanConfig::Pins::LED_PIN, OUTPUT);
    digitalWrite(RinchanConfig::Pins::LED_PIN, LOW); // Matikan LED awal

    // CEK ALASAN MENYALA
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    esp_reset_reason_t reset_reason = esp_reset_reason();

    // ✨ FIX 3: LOGIKA LEBIH PINTAR
    // Hanya langsung tidur JIKA benar-benar baru dicolok ke listrik/powerbank
    if (reset_reason == ESP_RST_POWERON)
    {
        Serial.println("\n[SYSTEM] Dicolok Listrik Pertama Kali! Langsung tidur...");
        enterDeepSleep();
    }
    // Jika bangun karena tombol ditekan
    else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0)
    {
        Serial.println("\n[SYSTEM] Bangun dari Deep Sleep! Memulai Rinchan...");
        digitalWrite(RinchanConfig::Pins::LED_PIN, HIGH); // Nyalakan LED
    }
    // Jika nyala karena habis Factory Reset (Tahan 5 Detik) atau ESP.restart()
    else
    {
        Serial.println("\n[SYSTEM] Nyala karena Restart Software. Memulai Rinchan...");
        digitalWrite(RinchanConfig::Pins::LED_PIN, HIGH); // Nyalakan LED
    }
}

void handleButtonLoop()
{
    int reading = digitalRead(RinchanConfig::Pins::BUTTON_PIN);

    if (reading == LOW)
    {
        if (!isButtonPressed)
        {
            isButtonPressed = true;
            buttonPressTime = millis();
            actionHandled = false;
        }
        else
        {
            // TAHAN TOMBOL
            if (!actionHandled && (millis() - buttonPressTime >= RinchanConfig::Button::LONG_PRESS_MS))
            {
                actionHandled = true;
                Serial.println("\n[BUTTON] Tahan 5 Detik -> FACTORY RESET!");

                // Kedip LED 3 Kali sebagai tanda Reset
                for (int i = 0; i < 3; i++)
                {
                    digitalWrite(RinchanConfig::Pins::LED_PIN, HIGH);
                    delay(200);
                    digitalWrite(RinchanConfig::Pins::LED_PIN, LOW);
                    delay(200);
                }

                clearWiFi();
                clearAuth(); // Alat otomatis restart di sini
            }
        }
    }
    else
    {
        if (isButtonPressed)
        {
            unsigned long pressDuration = millis() - buttonPressTime;
            isButtonPressed = false;

            // SHORT CLICK (Matikan Alat)
            if (!actionHandled && pressDuration > RinchanConfig::Button::DEBOUNCE_MS && pressDuration < RinchanConfig::Button::SHORT_PRESS_MAX_MS)
            {
                Serial.println("\n[BUTTON] Short Click -> MATIKAN ALAT (Deep Sleep)!");
                digitalWrite(RinchanConfig::Pins::LED_PIN, LOW);
                enterDeepSleep();
            }
        }
    }
}