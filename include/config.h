#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <stdint.h>

// Centralized project-wide constants to avoid scattered magic numbers/strings.
namespace RinchanConfig {
namespace Pins {
// ESP32-S3 I2C pins
constexpr int I2C_SDA = 4;
constexpr int I2C_SCL = 5;

// Button and LED pins
constexpr int BUTTON_PIN = 18;
constexpr int LED_PIN = 8;
} // namespace Pins

namespace Runtime {
constexpr int SERIAL_BAUDRATE = 115200;
constexpr unsigned long STARTUP_DELAY_MS = 2000;
constexpr unsigned long ACTION_INTERVAL_MS = 5000;
} // namespace Runtime

namespace WiFi {
constexpr unsigned int AP_SERVER_PORT = 80;
constexpr unsigned int DNS_PORT = 53;

constexpr const char *AP_SSID = "Rinchan-Setup";
constexpr const char *AP_PASSWORD = "";

constexpr const char *NVS_NAMESPACE = "wifi_data";
constexpr const char *KEY_SSID = "ssid";
constexpr const char *KEY_PASSWORD = "password";
constexpr const char *KEY_CONN_FAILED =
    "conn_fail"; // Flag: skip countdown saat restart

constexpr int CONNECT_MAX_RETRIES = 20;
constexpr unsigned long CONNECT_RETRY_DELAY_MS = 500;
constexpr unsigned long RECONNECT_DELAY_MS = 5000;

// Countdown (ms) yang ditampilkan di layar sebelum hotspot aktif setelah gagal
// konek
constexpr unsigned long AP_FAILOVER_COUNTDOWN_MS = 60000;
} // namespace WiFi

// Ubah ke: #define LOCAL_DEV_MODE 1  → pakai wrangler lokal
// Ubah ke: #define LOCAL_DEV_MODE 0  → pakai Cloudflare (produksi)

#define LOCAL_DEV_MODE 0

namespace Backend {
#if LOCAL_DEV_MODE
// ─── LOKAL (wrangler dev --ip 0.0.0.0) ───

constexpr const char *WS_HOST = "[IP_ADDRESS]";
constexpr int WS_PORT = 8787;
constexpr const char *WS_BASE_URL = "/api/ws/iot";
constexpr const char *BASE_URL = "http://[IP_ADDRESS]";
constexpr bool WS_USE_SSL = false;
#else
// ─── PRODUKSI (Cloudflare Workers) ───
constexpr const char *WS_HOST = "your-workers-url.workers.dev";
constexpr int WS_PORT = 443;
constexpr const char *WS_BASE_URL = "/api/ws/iot";
constexpr const char *BASE_URL = "https://your-workers-url.workers.dev";
constexpr bool WS_USE_SSL = true;
#endif
} // namespace Backend

namespace Auth {
constexpr const char *NVS_NAMESPACE = "auth_data";
constexpr const char *KEY_API = "api_key";

constexpr unsigned long POLL_INTERVAL_MS = 5000;
constexpr unsigned long CLEAR_AUTH_RESTART_DELAY_MS = 1000;
} // namespace Auth

namespace WebSocket {
constexpr unsigned int MAX_CONSECUTIVE_FAIL_BEFORE_REFRESH = 3;

constexpr unsigned long HEARTBEAT_INTERVAL_MS = 15000;
constexpr unsigned long HEARTBEAT_TIMEOUT_MS = 3000;
constexpr unsigned int HEARTBEAT_MAX_MISSED = 2;
constexpr unsigned long RECONNECT_INTERVAL_MS = 5000;

constexpr const char *USER_AGENT = "User-Agent: ESP32-Rinchan";
} // namespace WebSocket

namespace Button {
constexpr unsigned long LONG_PRESS_MS = 5000;
constexpr unsigned long SHORT_PRESS_MAX_MS = 1000;
constexpr unsigned long DEBOUNCE_MS = 50;
} // namespace Button

namespace Audio {
// --- SPEAKER (MAX98357A) ---
constexpr int I2S_BCLK = 15;
constexpr int I2S_LRC = 16;
constexpr int I2S_DOUT = 17;

// --- MIKROFON (INMP441) BARU ---
constexpr int MIC_BCLK = 11; // SCK
constexpr int MIC_LRC = 12;  // WS
constexpr int MIC_DIN = 13;  // SD
} // namespace Audio

namespace Hardware {
// Prefix untuk device ID
constexpr const char *DEVICE_PREFIX = "RC-V2-";
constexpr const char *NVS_NAMESPACE = "hw_data";
constexpr const char *KEY_VOLUME = "volume";
constexpr const char *KEY_BRIGHTNESS = "brightness";

constexpr int DEFAULT_VOLUME = 50;     // Skala 0 - 100%
constexpr int DEFAULT_BRIGHTNESS = 50; // Skala 0 - 100%
} // namespace Hardware
} // namespace RinchanConfig

#endif