#include "wifi.h"
#include "config.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

namespace
{
    Preferences preferences;
    AsyncWebServer server(RinchanConfig::WiFi::AP_SERVER_PORT);
    DNSServer dnsServer;
    bool isAPMode = false;
    unsigned long lastReconnectAttemptMs = 0;

    void startAccessPointMode()
    {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(RinchanConfig::WiFi::AP_SSID, RinchanConfig::WiFi::AP_PASSWORD);
        dnsServer.start(RinchanConfig::WiFi::DNS_PORT, "*", WiFi.softAPIP());
    }
}

// --- DESAIN HTML TEMA RINCHAN (Sembunyi di sini) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Setup Rin-chan</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: 'Segoe UI', sans-serif; background-color: #fce4ec; text-align: center; padding: 20px; color: #333; }
    .card { background: white; padding: 30px; border-radius: 15px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); max-width: 400px; margin: auto; }
    h2 { color: #d81b60; }
    input[type=text], input[type=password] { width: 90%; padding: 12px; margin: 10px 0; border: 1px solid #ccc; border-radius: 8px; box-sizing: border-box; }
    input[type=submit] { background-color: #d81b60; color: white; padding: 14px 20px; border: none; border-radius: 8px; cursor: pointer; width: 90%; font-weight: bold; font-size: 16px; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Rin-chan Setup \u2728</h2>
    <p>Masukkan WiFi Rumahmu</p>
    <form action="/save" method="POST">
      <input type="text" name="ssid" placeholder="Nama WiFi (SSID)" required><br>
      <input type="password" name="password" placeholder="Password WiFi"><br>
      <input type="submit" value="Sambungkan!">
    </form>
  </div>
</body>
</html>
)rawliteral";

void initWiFi()
{
    Serial.println("\n[WIFI] Membaca ingatan NVS...");
    preferences.begin(RinchanConfig::WiFi::NVS_NAMESPACE, false);
    const String ssid = preferences.getString(RinchanConfig::WiFi::KEY_SSID, "");
    const String password = preferences.getString(RinchanConfig::WiFi::KEY_PASSWORD, "");

    if (ssid.isEmpty())
    {
        Serial.println("[WIFI] Ingatan kosong. Masuk ke Mode AP!");
        isAPMode = true;
    }
    else
    {
        Serial.println("[WIFI] Mencoba konek ke: " + ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());

        int retries = 0;
        while (WiFi.status() != WL_CONNECTED && retries < RinchanConfig::WiFi::CONNECT_MAX_RETRIES)
        {
            delay(RinchanConfig::WiFi::CONNECT_RETRY_DELAY_MS);
            Serial.print(".");
            retries++;
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("\n[WIFI] Terhubung! IP: " + WiFi.localIP().toString());
            server.begin();
        }
        else
        {
            Serial.println("\n[WIFI] Gagal konek. Masuk ke Mode AP!");
            isAPMode = true;
        }
    }

    // JALANKAN CAPTIVE PORTAL JIKA MODE AP AKTIF
    if (isAPMode)
    {
        startAccessPointMode();

        Serial.print("[WIFI] Mode AP Aktif. IP Setup: ");
        Serial.println(WiFi.softAPIP());

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", index_html); });

        server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
      String newSSID = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
      String newPass = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
      
      preferences.putString(RinchanConfig::WiFi::KEY_SSID, newSSID);
      preferences.putString(RinchanConfig::WiFi::KEY_PASSWORD, newPass);
      
      request->send(200, "text/html", "<h3>Sip! Tersimpan. Rinchan akan restart...</h3>");
      Serial.println("[WIFI] Data WiFi baru diterima. Restarting...");
      delay(RinchanConfig::Runtime::STARTUP_DELAY_MS);
      ESP.restart(); });

        server.onNotFound([](AsyncWebServerRequest *request)
                          { request->redirect("http://" + WiFi.softAPIP().toString()); });

        server.begin();
    }
}

void handleWiFiLoop()
{
    if (isAPMode)
    {
        dnsServer.processNextRequest(); // Jaga agar DNS redirect tetap jalan
    }
    else if (WiFi.status() != WL_CONNECTED)
    {
        const unsigned long nowMs = millis();
        if (nowMs - lastReconnectAttemptMs >= RinchanConfig::WiFi::RECONNECT_DELAY_MS)
        {
            lastReconnectAttemptMs = nowMs;
            Serial.println("[WIFI] Terputus! Mencoba reconnect...");
            WiFi.reconnect();
        }
    }
}

bool isWiFiConnected()
{
    return (!isAPMode && WiFi.status() == WL_CONNECTED);
}

void clearWiFi()
{
    preferences.begin(RinchanConfig::WiFi::NVS_NAMESPACE, false);
    preferences.remove(RinchanConfig::WiFi::KEY_SSID);
    preferences.remove(RinchanConfig::WiFi::KEY_PASSWORD);
    preferences.end();
    Serial.println("[WIFI] Ingatan WiFi dihapus. Siap kembali ke Mode AP!");
}