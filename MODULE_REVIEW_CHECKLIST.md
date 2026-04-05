# Module Review Checklist

Checklist ini dipakai saat review perubahan agar kualitas antar modul tetap konsisten.

## WiFi (`wifi_manager`)

- Semua key NVS memakai konstanta dari `RinchanConfig::WiFi`.
- Tidak ada magic number untuk retry, delay, port.
- Alur fallback ke AP mode tetap aman saat STA gagal.
- Endpoint captive portal (`/`, `/save`) tetap responsif.
- `handleWiFiLoop()` tidak melakukan operasi blocking berlebihan.

## Auth (`auth_manager`)

- URL polling dibangun dari konfigurasi backend terpusat.
- Namespace/key penyimpanan token konsisten dengan `RinchanConfig::Auth`.
- Polling tetap berhenti saat token valid ditemukan.
- `refreshToken()` tidak menghapus token valid secara tidak sengaja.
- `clearAuth()` menghapus state memori dan memicu restart terkontrol.

## WebSocket (`websocket`)

- URL WS/WSS hanya berasal dari `RinchanConfig::Backend`.
- Parameter heartbeat dan reconnect hanya dari `RinchanConfig::WebSocket`.
- Semua send path melakukan guard saat koneksi putus.
- Handler event memisahkan jalur `pong`, command, dan error dengan jelas.
- Tidak ada copy String tidak perlu di API internal/public.

## Sensor (`sensors`)

- Pin I2C hanya berasal dari `RinchanConfig::Pins`.
- Inisialisasi sensor menulis log sukses/gagal yang jelas.
- `readAllSensors()` tidak menyembunyikan failure fatal.

## Pomodoro (`pomodoro_ws`)

- Fungsi public harus punya external linkage sesuai header.
- State machine transisi fokus/istirahat/selesai tetap valid.
- Pengiriman phase report tidak duplikat fase untuk satu siklus.
- Interval sensor AI mengikuti payload dengan fallback default.

## General

- Simbol private file berada di unnamed namespace.
- Tidak ada magic number/string berulang di `.cpp`.
- Parameter input String yang read-only menggunakan `const String &`.
- Perubahan API header disinkronkan dengan implementasi dan call-site.
