#include "auth.h"
#include "config.h"
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

namespace
{
    Preferences authPrefs;
    String apiKey = "";

    String getDeviceId()
    {
        // Gunakan static agar MAC Address tidak dihitung ulang setiap kali dipanggil
        static String cachedDeviceId = "";

        if (cachedDeviceId.isEmpty())
        {
            // Ambil MAC Address asli (Contoh: "24:6F:28:A3:B9:1C")
            String mac = WiFi.macAddress();

            // Hapus titik duanya (Menjadi: "246F28A3B91C")
            mac.replace(":", "");

            // Ambil 4 karakter terakhir ("B91C")
            String tail = mac.substring(mac.length() - 4);

            // Jahit menjadi format industrial
            cachedDeviceId = "RC-v1-" + tail;

            Serial.println("[SYSTEM] Device ID Generate: " + cachedDeviceId);
        }

        return cachedDeviceId;
    }

    String buildPollUrl()
    {
        // ✨ GANTI Pemanggilan Config menjadi pemanggilan Fungsi
        return String(RinchanConfig::Backend::BASE_URL) + "/api/device/poll/" + getDeviceId();
    }
}

String getApiKey()
{
    return apiKey;
}

bool isDeviceClaimed()
{
    return !apiKey.isEmpty();
}

void initAuth()
{
    Serial.println("\n[AUTH] Memeriksa status token di memori...");

    // Buka NVS khusus untuk token
    authPrefs.begin(RinchanConfig::Auth::NVS_NAMESPACE, false);
    if (authPrefs.isKey(RinchanConfig::Auth::KEY_API))
    {
        apiKey = authPrefs.getString(RinchanConfig::Auth::KEY_API, "");
    }
    else
    {
        apiKey = "";
    }

    if (!apiKey.isEmpty())
    {
        Serial.println("[AUTH] Token ditemukan! Alat sudah di-claim.");
        authPrefs.end();
        return; // Langsung keluar fungsi, siap lanjut ke WebSocket
    }

    Serial.println("[AUTH] Token KOSONG. Memulai mode Polling...");

    const String pollUrl = buildPollUrl();

    // LOOP POLLING: Akan tertahan di sini sampai user klik "Claim" di Dashboard
    while (apiKey.isEmpty())
    {
        Serial.println("[AUTH] Menghubungi server: " + pollUrl);

        // --- KODE BARU UNTUK MENEMBUS HTTPS CLOUDFLARE ---
        WiFiClientSecure client;
        client.setInsecure(); // Abaikan cek sertifikat SSL agar praktis

        HTTPClient http;
        http.begin(client, pollUrl); // Masukkan client ke dalam begin()
        // --------------------------------------------------

        int httpCode = http.GET();

        if (httpCode > 0)
        {
            String payload = http.getString();

            // Parsing JSON dari Backend
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);

            if (!error)
            {
                // PERHATIKAN STRUKTUR JSON DARI ENDPOINT GET /poll KAMU
                String status = doc["data"]["status"].as<String>();

                if (status == "claimed")
                {
                    // YAY! Majikan sudah nge-klik claim!
                    apiKey = doc["data"]["apiKey"].as<String>();

                    authPrefs.putString(RinchanConfig::Auth::KEY_API, apiKey);
                    Serial.println("[AUTH] KLAIM BERHASIL! Token tersimpan permanen.");
                }
                else
                {
                    Serial.println("[AUTH] Belum di-claim. Menunggu majikan...");
                }
            }
            else
            {
                Serial.println("[AUTH] Error parsing JSON: " + String(error.c_str()));
                Serial.println("[AUTH] Payload yang didapat: " + payload); // Biar ketahuan kalau Hono ngirim error text
            }
        }
        else
        {
            Serial.printf("[AUTH] HTTP Request gagal, error: %s\n", http.errorToString(httpCode).c_str());
        }

        http.end();

        if (apiKey.isEmpty())
        {
            delay(RinchanConfig::Auth::POLL_INTERVAL_MS);
        }
    }

    authPrefs.end();
}

void clearAuth()
{
    authPrefs.begin(RinchanConfig::Auth::NVS_NAMESPACE, false);
    authPrefs.remove(RinchanConfig::Auth::KEY_API);
    authPrefs.end();
    apiKey = "";
    Serial.println("[AUTH] Ingatan Token dihapus. Alat kembali ke setelan pabrik.");
    delay(RinchanConfig::Auth::CLEAR_AUTH_RESTART_DELAY_MS);
    ESP.restart(); // Restart alat biar masuk ke mode Polling lagi
}

// Fungsi Auto-Healing
void refreshToken()
{
    Serial.println("[AUTH] Mencurigai token basi. Mengecek server...");

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    const String pollUrl = buildPollUrl();
    http.begin(client, pollUrl);

    int httpCode = http.GET();
    if (httpCode > 0)
    {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error)
        {
            String status = doc["data"]["status"].as<String>();

            if (status == "claimed")
            {
                String newKey = doc["data"]["apiKey"].as<String>();
                // Jika token dari server BERBEDA dengan yang kita punya, berarti versi naik!
                if (newKey != apiKey)
                {
                    apiKey = newKey;
                    authPrefs.begin(RinchanConfig::Auth::NVS_NAMESPACE, false);
                    authPrefs.putString(RinchanConfig::Auth::KEY_API, apiKey);
                    authPrefs.end();
                    Serial.println("[AUTH] AUTO-HEALING SUKSES! Token versi baru berhasil didapat.");
                }
                else
                {
                    Serial.println("[AUTH] Token masih sama, sepertinya cuma gangguan jaringan.");
                }
            }
            else if (status == "waiting")
            {
                // Bahaya! User ternyata menghapus alat ini dari Dashboard!
                Serial.println("[AUTH] ALARM! Alat ini telah dihapus oleh majikan!");
                clearAuth(); // Hapus ingatan dan restart
            }
        }
    }
    http.end();
}