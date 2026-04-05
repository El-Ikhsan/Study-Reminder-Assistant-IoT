# Coding Conventions

Panduan ini menjaga codebase tetap bersih, konsisten, dan mudah dibaca oleh junior maupun senior.

## 1. Struktur Konfigurasi

- Simpan semua konstanta proyek di `include/config.h` dalam namespace domain:
  - `RinchanConfig::Pins`
  - `RinchanConfig::Runtime`
  - `RinchanConfig::WiFi`
  - `RinchanConfig::Backend`
  - `RinchanConfig::Auth`
  - `RinchanConfig::WebSocket`
- Hindari magic number dan string literal berulang di file `.cpp`.

## 2. Batas Scope yang Jelas

- Untuk simbol private file (state, helper function), gunakan unnamed namespace:
  - `namespace { ... }`
- Ekspor hanya API yang benar-benar dibutuhkan di header.

## 3. Aturan String dan Parameter

- Gunakan `const String &` untuk parameter input String agar tidak copy data.
- Gunakan `String::isEmpty()` untuk pengecekan kosong agar intent lebih jelas.

## 4. Gaya Flow Control

- Gunakan early return untuk kondisi guard agar blok utama tetap ringkas.
- Pisahkan helper kecil saat logika mulai berulang (contoh: readiness check, URL builder).

## 5. Logging

- Pertahankan prefix log konsisten per modul:
  - `[WIFI]`, `[AUTH]`, `[WS]`, `[SENSOR]`
- Log harus menjelaskan konteks aksi dan hasil utama.

## 6. Perubahan API

- Jika mengubah signature fungsi di header, sinkronkan semua implementasi dan call site dalam commit/perubahan yang sama.

## 7. Prinsip Umum

- Refactor harus tidak mengubah behavior kecuali memang diminta.
- Fokus pada perubahan kecil, terukur, dan mudah di-review.
