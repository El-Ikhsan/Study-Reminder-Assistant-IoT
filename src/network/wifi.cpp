#include "wifi.h"
#include "config.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "ui/display.h"

namespace
{
    Preferences preferences;
    AsyncWebServer server(RinchanConfig::WiFi::AP_SERVER_PORT);
    DNSServer dnsServer;
    bool isAPMode = false;
    unsigned long lastReconnectAttemptMs = 0;

    // ✨ Variabel pending koneksi dari form /save (async → loop utama)
    volatile bool pendingConnect = false;
    String pendingSsid = "";
    String pendingPass = "";

    void startAccessPointMode()
    {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(RinchanConfig::WiFi::AP_SSID, RinchanConfig::WiFi::AP_PASSWORD);
        dnsServer.start(RinchanConfig::WiFi::DNS_PORT, "*", WiFi.softAPIP());
    }
}

// --- DESAIN HTML TEMA RINCHAN ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Setup WiFi</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      font-family: 'Segoe UI', sans-serif; 
      background-color: #e3f2fd; 
      text-align: center; 
      padding: 20px; 
      color: #333; 
    }
    .card { 
      background: white; 
      padding: 30px; 
      border-radius: 15px; 
      box-shadow: 0 4px 8px rgba(0,0,0,0.1); 
      max-width: 400px; 
      margin: auto; 
    }
    h2 { color: #1976d2; }
    input[type=text], input[type=password] { 
      width: 90%; padding: 12px; margin: 10px 0; 
      border: 1px solid #ccc; border-radius: 8px; 
      box-sizing: border-box; outline: none; 
    }
    input[type=text]:focus, input[type=password]:focus { border-color: #1976d2; }
    input[type=submit] { 
      background-color: #1976d2; color: white; padding: 14px 20px; 
      border: none; border-radius: 8px; cursor: pointer; 
      width: 90%; font-weight: bold; font-size: 16px; 
      margin-top: 10px; transition: 0.3s; 
    }
    input[type=submit]:hover { background-color: #1565c0; }
    #error-msg { 
      color: #d32f2f; background-color: #ffebee; padding: 10px; 
      border-radius: 8px; font-size: 14px; margin-bottom: 15px; 
      display: none; font-weight: 500; border: 1px solid #ef9a9a;
    }
  </style>
</head>
<body>
  <div class="card">
    <h2>Setup WiFi</h2>
    <p>Masukkan WiFi Yang Ingin Digunakan</p>
    <div id="error-msg"></div>
    <form action="/save" method="POST" onsubmit="return validateForm()">
      <input type="text" id="ssid" name="ssid" placeholder="Nama WiFi (SSID)"><br>
      <input type="password" id="password" name="password" placeholder="Password WiFi"><br>
      <input type="submit" value="Sambungkan!">
    </form>
  </div>
  <script>
    function validateForm() {
      var ssid = document.getElementById("ssid").value.trim();
      var password = document.getElementById("password").value.trim();
      var errorMsg = document.getElementById("error-msg");
      if (ssid === "" || password === "") {
        errorMsg.style.display = "block"; 
        errorMsg.innerHTML = "SSID dan Password tidak boleh kosong!"; 
        return false; 
      }
      errorMsg.style.display = "none";
      return true;
    }
  </script>
</body>
</html>
)rawliteral";

// Pesan hotspot tanpa \n
static const char AP_WAITING_MSG[] = "Mode Hotspot Aktif! Sambungkan perangkat ke WiFi: Rinchan-Setup";

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

    if (isAPMode)
    {
        startAccessPointMode();
        Serial.print("[WIFI] Mode AP Aktif. IP Setup: ");
        Serial.println(WiFi.softAPIP());

        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", index_html); });

        // Rute /save diperbarui untuk async connect (Tidak restart langsung)
        server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request)
                  {
            String newSSID = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : "";
            String newPass = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";

            if (newSSID.isEmpty()) {
                request->send(400, "text/html", "<h3>SSID tidak boleh kosong!</h3>");
                return;
            }

            // Oper nilai ke loop utama
            pendingSsid = newSSID;
            pendingPass = newPass;
            pendingConnect = true;

            request->send(200, "text/html", "<h3>Mencoba sambung ke: " + newSSID + "</h3><p>Hotspot akan kembali aktif jika gagal.</p>");
            Serial.println("[WIFI] Setelan diterima: " + newSSID + ". Akan coba konek..."); });

        server.onNotFound([](AsyncWebServerRequest *request)
                          { request->redirect("http://" + WiFi.softAPIP().toString()); });

        server.begin();

        // Tampilkan pesan persistent di layar
        showPersistentDialog(AP_WAITING_MSG);
    }
}

void handleWiFiLoop()
{
    if (isAPMode)
    {
        dnsServer.processNextRequest();

        if (pendingConnect)
        {
            pendingConnect = false;
            Serial.println("[WIFI] Memulai percobaan konek ke: " + pendingSsid);

            dnsServer.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            delay(200);

            WiFi.begin(pendingSsid.c_str(), pendingPass.c_str());

            const int totalSec = (int)(RinchanConfig::WiFi::AP_FAILOVER_COUNTDOWN_MS / 1000);

            // Parameter diperbaiki jadi 2 string
            showCountdownWidget("Mencoba Terhubung...", "ke WiFi: " + pendingSsid);
            updateCountdownSeconds(totalSec);

            bool connected = false;
            for (int remaining = totalSec; remaining >= 0; remaining--)
            {
                if (WiFi.status() == WL_CONNECTED)
                {
                    connected = true;
                    break;
                }
                updateCountdownSeconds(remaining);
                Serial.printf("[WIFI] Menunggu koneksi... %ds\n", remaining);
                delay(1000);
            }

            if (connected)
            {
                Serial.println("[WIFI] BERHASIL! IP: " + WiFi.localIP().toString());
                preferences.putString(RinchanConfig::WiFi::KEY_SSID, pendingSsid);
                preferences.putString(RinchanConfig::WiFi::KEY_PASSWORD, pendingPass);

                // Tanpa /n
                showDialogWidget("WiFi Terhubung! Menyimpan setelan... Restart...");
                delay(1500);
                ESP.restart();
            }
            else
            {
                Serial.println("[WIFI] GAGAL konek. Restart ke Mode AP...");

                // Tanpa /n
                showDialogWidget("Gagal Terhubung! Hotspot akan aktif kembali...");
                delay(2000);
                ESP.restart();
            }
        }
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