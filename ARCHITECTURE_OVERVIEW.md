# Architecture Overview

Dokumen ini menjelaskan alur utama firmware Rinchan secara ringkas untuk onboarding.

## 1. Boot Flow

1. `setup()` inisialisasi serial dan sensor.
2. `initWiFi()` mencoba konek ke STA dari NVS.
3. Jika WiFi tersambung, `initAuth()` mengecek atau polling token claim.
4. Jika WiFi + token siap, `initWebSocket()` membuka koneksi WSS.

## 2. Main Loop Flow

Di `loop()` urutan tugas utama:

1. `handleWiFiLoop()`:

- Menjaga captive portal saat AP mode.
- Melakukan reconnect STA dengan rate-limit non-blocking saat putus.

2. Jika runtime siap (`WiFi connected` + `device claimed`):

- `wsLoop()` menjaga lifecycle websocket.
- `pomodoroLoop()` menjalankan state machine sesi.
- Pengiriman berkala ping/telemetry berdasarkan `RinchanConfig::Runtime::ACTION_INTERVAL_MS`.

## 3. Module Responsibilities

- `wifi_manager`:
  - Koneksi WiFi STA/AP.
  - Captive portal + penyimpanan credential.
- `auth_manager`:
  - Polling claim status device.
  - Persist token API dan refresh token.
- `websocket`:
  - Koneksi WSS, heartbeat, reconnect, dispatch message.
- `sensors`:
  - Inisialisasi dan pembacaan sensor hardware.
- `pomodoro_ws`:
  - State machine pomodoro + laporan fase/sensor AI.

## 4. Configuration Strategy

Semua konstanta penting dipusatkan di `include/config.h`:

- `RinchanConfig::Pins`
- `RinchanConfig::Runtime`
- `RinchanConfig::WiFi`
- `RinchanConfig::Backend`
- `RinchanConfig::Auth`
- `RinchanConfig::WebSocket`

Tujuan: menghindari magic number/string dan mempermudah tuning.

## 5. Data Persistence

- WiFi credential disimpan di namespace NVS WiFi.
- API token disimpan di namespace NVS Auth.
- Saat auth dihapus, device restart untuk kembali ke flow claim.

## 6. Reliability Notes

- WebSocket heartbeat dan reconnect interval terkonfigurasi.
- Auto-healing token berjalan saat websocket gagal berulang.
- Reconnect WiFi di loop utama sudah non-blocking (tidak `delay` panjang).
