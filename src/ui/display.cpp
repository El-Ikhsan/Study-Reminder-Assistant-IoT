#include "display.h"
#include "audio/sound_manager.h"
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include "audio/audio.h"
#include <driver/i2s.h>
#include <AnimatedGIF.h>

#define TFT_BL_PIN 39

AnimatedGIF gif;

Emotion parseEmotionString(String emoStr)
{
    emoStr.toUpperCase();
    if (emoStr == "HOT")
        return EMOTION_HOT;
    if (emoStr == "COLD")
        return EMOTION_COLD;
    if (emoStr == "DARK")
        return EMOTION_DARK;
    if (emoStr == "GLARE")
        return EMOTION_GLARE; // Backend kirim "GLARE" untuk silau
    if (emoStr == "NOISY")
        return EMOTION_NOISY;
    if (emoStr == "LISTENING")
        return EMOTION_LISTENING;
    if (emoStr == "RECOVERY")
        return EMOTION_RECOVERY; // Backend kirim "RECOVERY"
    return EMOTION_IDLE;
}

namespace
{
    TFT_eSPI tft = TFT_eSPI();
    Emotion currentEmoji = EMOTION_IDLE;
    WidgetMode currentWidget = WIDGET_NONE;
    uint8_t *gifBuffer = NULL;
    size_t gifBufferSize = 0;
    bool isGifLoaded = false;
    File gifFile;

}

// ==========================================
// 0. TOP BAR: STATUS WIFI & WAKENET (MIC)
// ==========================================
// Panggil ini di main.cpp setiap ada perubahan status WiFi atau WakeNet
static bool lastWifi = false;
static bool lastMic = false;
static String lastTimeStr = "";
static String lastAlertText = "";
static bool firstDrawTopBar = true;

void drawTopBar(bool isWifiConnected, bool isMicActive, String timeStr, String alertText)
{
    // 1. Gambar dasar (HANYA DIEKSEKUSI 1x SAAT BOOTING)
    if (firstDrawTopBar)
    {
        tft.fillRect(0, 0, 320, 20, TFT_BLACK);
        tft.drawLine(0, 19, 320, 19, tft.color565(50, 50, 50));
    }

    // 2. Update Ikon WiFi (HANYA JIKA STATUS BERUBAH)
    if (isWifiConnected != lastWifi || firstDrawTopBar)
    {
        tft.fillRect(5, 2, 16, 16, TFT_BLACK); // Sapu bersih area ikon saja
        TJpgDec.drawFsJpg(5, 2, isWifiConnected ? "/wifi_on.jpg" : "/wifi_off.jpg", LittleFS);
        lastWifi = isWifiConnected;
    }

    // 3. Update Ikon Mic (HANYA JIKA STATUS BERUBAH)
    // if (isMicActive != lastMic || firstDrawTopBar)
    // {
    //     tft.fillRect(25, 2, 16, 16, TFT_BLACK);
    //     TJpgDec.drawFsJpg(25, 2, isMicActive ? "/mic_on.jpg" : "/mic_off.jpg", LittleFS);
    //     lastMic = isMicActive;
    // }

    // 4. Update Jam (HANYA JIKA DETIK/MENIT BERUBAH)
    if (timeStr != lastTimeStr || firstDrawTopBar)
    {
        tft.fillRect(100, 0, 120, 18, TFT_BLACK); // Sapu bersih area teks jam saja
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE);
        tft.drawCentreString(timeStr, 160, 5, 1);
        lastTimeStr = timeStr;
    }

    // 5. Update Teks Alert (HANYA JIKA STATUS SENSOR BERUBAH)
    if (alertText != lastAlertText || firstDrawTopBar)
    {
        tft.fillRect(220, 0, 100, 18, TFT_BLACK); // Sapu bersih area teks alert saja

        // Jangan tampilkan teks jika kosong atau kondisi sedang optimal
        if (alertText != "" && alertText != "Kondisi Optimal")
        {
            tft.setTextColor(TFT_ORANGE);
            tft.drawRightString(alertText, 315, 5, 1);
        }
        lastAlertText = alertText;
    }

    firstDrawTopBar = false; // Kunci gambar dasar
}

void forceUpdateTopBarAlert(String alertText)
{
    tft.fillRect(220, 0, 100, 18, TFT_BLACK);

    // ✨ KEMBALIKAN KE KUAS KECIL SEBELUM MENGGAMBAR!
    tft.setTextSize(1);

    if (alertText != "" && alertText != "Kondisi Optimal")
    {
        tft.setTextColor(TFT_ORANGE);
        tft.drawRightString(alertText, 315, 5, 1);
    }

    lastAlertText = alertText; // Sinkronisasi memori
}

// ==========================================
// ✨ FUNGSI CALLBACK: Menggambar GIF frame by frame
// ==========================================
void GIFDraw(GIFDRAW *pDraw)
{
    uint8_t *s;
    uint16_t *usPalette, usTemp[320];
    int x, y, iWidth;

    iWidth = pDraw->iWidth;
    if (iWidth > 320)
        iWidth = 320;
    y = pDraw->iY + pDraw->y;

    if (y < 20)
        return; // Top Bar Aman

    // ✨ ATURAN BARU: Jika dialog aktif, JANGAN GAMBAR GIF SAMA SEKALI (Animasi Berhenti)
    if (currentWidget == WIDGET_DIALOG)
        return;

    // ✨ Jika Pomodoro aktif, potong di 175. Jika tidak ada widget, hajar sampai 240.
    int bottomLimit = (currentWidget == WIDGET_POMODORO) ? 175 : 240;
    if (y >= bottomLimit)
        return;

    usPalette = pDraw->pPalette;
    s = pDraw->pPixels;
    for (x = 0; x < iWidth; x++)
    {
        usTemp[x] = usPalette[*s++];
    }

    tft.pushImage(pDraw->iX, y, iWidth, 1, usTemp);
}

void playDisplayAnimation()
{
    if (isGifLoaded)
    {
        gif.playFrame(true, NULL);
    }
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap)
{
    if (y >= tft.height())
        return 0;
    tft.pushImage(x, y, w, h, bitmap);
    return 1;
}

void setDisplayBrightness(int percent)
{
    percent = constrain(percent, 0, 100);
    analogWrite(TFT_BL_PIN, map(percent, 0, 100, 0, 255));
}

void initDisplay()
{
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    TJpgDec.setCallback(tft_output);
    setDisplayBrightness(80);

    // Gambar Top Bar awal (Asumsi mati semua saat booting)
    drawTopBar(false, false, "--:--", "");
}

void showBootingScreen()
{
    // Inisialisasi LittleFS jika belum
    if (!LittleFS.begin(true))
    {
        tft.setRotation(1); // Pastikan landscape untuk pesan error
        Serial.println("[BOOT] LittleFS mount failed!");
        showDialogWidget("[ERR] LittleFS gagal!");
        return;
    }

    // Bersihkan layar
    tft.fillScreen(TFT_BLACK);

    // Cek apakah file gambar tersedia
    const char *bootImg = "/rinchan-tft.jpg";
    if (LittleFS.exists(bootImg))
    {
        // ✨ GAMBAR ADA: Ubah ke Portrait (Berdiri) KHUSUS untuk render ini
        tft.setRotation(0);

        TJpgDec.setSwapBytes(true);
        TJpgDec.drawFsJpg(0, 0, bootImg, LittleFS);

        // Selesai render, biarkan layarnya tetap Portrait (0) di sini.
        // Nanti akan dikembalikan ke (1) oleh main.cpp di Step 5.
    }
    else
    {
        // ⚠️ GAMBAR TIDAK ADA: Pakai Teks Alternatif
        // ✨ WAJIB ubah ke Landscape (Tidur) agar teks tidak terpotong!
        tft.setRotation(1);

        showDialogWidget("Membangunkan Rinchan...");
    }
}

// ==========================================
// 1. MAIN CANVAS: RENDER EMOSI (Y: 20 - 239)
// ==========================================
void drawEmoji(Emotion emoji)
{
    if (currentEmoji == emoji && isGifLoaded)
        return;

    tft.setRotation(1);

    tft.fillRect(0, 20, 320, 155, TFT_BLACK);
    if (currentWidget == WIDGET_NONE)
    {
        tft.fillRect(0, 175, 320, 65, TFT_BLACK);
    }

    currentEmoji = emoji;
    isGifLoaded = false;

    // 1. Pilih jalur file-nya
    String filePath = "";
    switch (emoji)
    {
    case EMOTION_HOT:
        filePath = "/panas.gif";
        break;
    case EMOTION_COLD:
        filePath = "/dingin_extrem.gif";
        break;
    case EMOTION_DARK:
        filePath = "/gelap.gif";
        break;
    case EMOTION_GLARE:
        filePath = "/silau.gif";
        break;
    case EMOTION_NOISY:
        filePath = "/bising.gif";
        break;
    case EMOTION_LISTENING:
        filePath = "/mendengar.gif";
        break;
    case EMOTION_RECOVERY:
        filePath = "/pemulihan.gif";
        break;
    case EMOTION_IDLE:
    default:
        filePath = "/idle.gif";
        break;
    }

    // ==========================================
    // 🚀 THE PSRAM MAGIC (ANTI LAG & STUTTERING)
    // ==========================================
    File file = LittleFS.open(filePath, "r");
    if (file)
    {
        size_t fileSize = file.size();

        // Buat memori PSRAM sebesar ukuran file (jika belum ada)
        if (gifBuffer == NULL || gifBufferSize < fileSize)
        {
            if (gifBuffer != NULL)
                free(gifBuffer);
            gifBuffer = (uint8_t *)ps_malloc(fileSize); // Sedot RAM Eksternal!
            gifBufferSize = fileSize;
        }

        if (gifBuffer != NULL)
        {
            // Sedot seluruh file ke dalam RAM secara kilat (hanya memakan waktu ~100ms)
            file.read(gifBuffer, fileSize);
            file.close();

            // Setingan Warna Akurat (Mencegah muka jadi hijau/biru)
            gif.begin(BIG_ENDIAN_PIXELS);

            // Putar GIF langsung dari RAM Eksternal!
            if (gif.open(gifBuffer, fileSize, GIFDraw))
            {
                isGifLoaded = true;
            }
        }
        else
        {
            Serial.println("[ERR] PSRAM Penuh atau Gagal Alokasi!");
            file.close();
        }
    }
}

// ==========================================
// 2. AREA BAWAH: KOTAK DIALOG TEKS
// ==========================================
volatile bool cancelCurrentDialog = false;
TFT_eSprite dialogSprite = TFT_eSprite(&tft);

void forceClearDialog() { cancelCurrentDialog = true; }

void showDialogWidget(String text)
{
    stopAudio();
    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM_0);
    delay(10);

    cancelCurrentDialog = false;
    currentWidget = WIDGET_DIALOG;

    // 1. Siapkan Kanvas (Sprite) RAKSASA: Lebar 320, Tinggi 220
    dialogSprite.createSprite(320, 220);
    dialogSprite.fillSprite(TFT_BLACK);

    // Kotak dialog besar menutupi area animasi
    dialogSprite.fillRoundRect(5, 5, 310, 210, 5, tft.color565(20, 20, 30));
    dialogSprite.drawRoundRect(5, 5, 310, 210, 5, TFT_CYAN);
    dialogSprite.setTextColor(TFT_WHITE);
    dialogSprite.setTextSize(2);

    int cX = 15, cY = 15; // Kursor mulai dari atas kotak
    dialogSprite.setCursor(cX, cY);

    int charCount = 0;
    for (int i = 0; i < text.length(); i++)
    {
        if (cancelCurrentDialog)
            break;

        // 2. Logika Word-Wrap
        if (text[i] != ' ' && (i == 0 || text[i - 1] == ' '))
        {
            int wordWidth = 0;
            for (int j = i; j < text.length() && text[j] != ' '; j++)
                wordWidth += 12;

            if (cX + wordWidth > 300)
            {
                cX = 15;
                cY += 24; // Turun ke baris berikutnya

                // ✨ JIKA TEKS MELEBIHI KOTAK (Masuk melebihi Y = 190)
                if (cY > 190)
                {
                    // Bersihkan kanvas, buat halaman baru
                    dialogSprite.fillSprite(TFT_BLACK);
                    dialogSprite.fillRoundRect(5, 5, 310, 210, 5, tft.color565(20, 20, 30));
                    dialogSprite.drawRoundRect(5, 5, 310, 210, 5, TFT_CYAN);
                    dialogSprite.setTextColor(TFT_WHITE);

                    cX = 15;
                    cY = 15; // Kembalikan kursor ke atas
                }
                dialogSprite.setCursor(cX, cY);
            }
        }

        if (text[i] == ' ' && cX == 15)
            continue;

        // 3. Tulis Huruf & Tembak ke Layar
        dialogSprite.print(text[i]);
        cX += 12;
        charCount++;

        // ✨ TEMBAKKAN KE LAYAR DIMULAI DARI BAWAH TOP BAR (Y = 20)
        dialogSprite.pushSprite(0, 20);

        // 4. Mainkan SFX
        bool makeSound = (text[i] != ' ' && charCount % 2 == 1);
        playTypingSync(makeSound);
    }

    playTypingSync(false);
    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM_0);

    // ✨ 5. JEDA AKHIR
    if (!cancelCurrentDialog)
    {
        unsigned long readStart = millis();
        while (millis() - readStart < 3000)
        {
            if (cancelCurrentDialog)
                break;
            delay(10);
        }
    }

    // 6. Bersihkan memori kanvas setelah selesai dibaca
    dialogSprite.deleteSprite();
    if (!cancelCurrentDialog)
        clearWidget();
}

// ==========================================
// 3. AREA BAWAH: TIMER POMODORO LENGKAP
// ==========================================
void clearWidget()
{
    // Sapu bersih seluruh area bawah Top Bar
    tft.fillRect(0, 20, 320, 220, TFT_BLACK);
    currentWidget = WIDGET_NONE;

    // Pancing ulang drawEmoji agar GIF dipanggil kembali dari awal (Frame 1)
    Emotion temp = currentEmoji;
    currentEmoji = EMOTION_IDLE;
    drawEmoji(temp);
}

// ✨ UPDATE: Tambahan parameter cycle dan media belajar
void updatePomodoroWidget(int min, int sec, bool isBreak, int cycle, String media)
{
    if (currentWidget == WIDGET_DIALOG)
        return;

    // ==========================================
    // 🧠 VARIABEL INGATAN (State Tracking)
    // ==========================================
    static bool lastIsBreak = false;
    static int lastCycle = -1;
    static String lastMedia = "";
    static int lastMin = -1;
    static int lastSec = -1;
    static bool firstDrawPomo = true;

    // Jika widget baru saja muncul (sebelumnya layar bersih/mode lain)
    if (currentWidget != WIDGET_POMODORO)
    {
        // Blok seluruh area bawah HANYA 1X SAAT MUNCUL
        tft.fillRect(0, 175, 320, 65, tft.color565(15, 15, 15));
        tft.drawLine(0, 175, 320, 175, TFT_DARKGREY);

        currentWidget = WIDGET_POMODORO;
        firstDrawPomo = true; // Paksa render ulang seluruh komponen
    }

    // ==========================================
    // A. AREA KIRI: INFO DETAIL
    // Update HANYA jika fase, siklus, media berubah, atau baru pertama muncul!
    // ==========================================
    if (isBreak != lastIsBreak || cycle != lastCycle || media != lastMedia || firstDrawPomo)
    {
        tft.fillRect(5, 178, 175, 60, tft.color565(15, 15, 15)); // Sapu area kiri

        // 1. Teks Mode Fokus/Istirahat
        tft.setTextSize(2);
        if (isBreak)
        {
            tft.setTextColor(TFT_GREEN);
            tft.drawString("ISTIRAHAT", 10, 182);
        }
        else
        {
            tft.setTextColor(TFT_ORANGE);
            tft.drawString("FOKUS BELAJAR", 10, 182);
        }

        // 2. Teks Siklus & Media Belajar
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE);
        tft.drawString("Siklus: " + String(cycle) + "/4", 10, 205);

        String displayMedia = media;
        if (displayMedia.length() > 15)
            displayMedia = displayMedia.substring(0, 12) + "...";
        tft.drawString("Media: " + displayMedia, 10, 220);

        // Simpan ke ingatan
        lastIsBreak = isBreak;
        lastCycle = cycle;
        lastMedia = media;
    }

    // ==========================================
    // B. AREA KANAN: TIMER RAKSASA
    // Update HANYA setiap kali detik atau menit berubah!
    // ==========================================
    if (min != lastMin || sec != lastSec || firstDrawPomo)
    {
        char timeStr[6];
        sprintf(timeStr, "%02d:%02d", min, sec);

        // ✨ THE MAGIC: Teks dengan background warna abu-abu gelap
        // Ini akan menimpa angka lama TANPA perlu dibersihkan pakai fillRect!
        tft.setTextColor(TFT_WHITE, tft.color565(15, 15, 15));

        // ✨ THE MAGIC 2: Beri "Bantalan" pada teks sebesar 120 piksel.
        // Ini memastikan sisa-sisa piksel dari angka sebelumnya tersapu bersih!
        tft.setTextPadding(120);

        tft.drawCentreString(timeStr, 250, 195, 4);

        // Kembalikan padding ke 0 agar tidak merusak teks lain di fungsi berbeda
        tft.setTextPadding(0);

        // Simpan ke ingatan
        lastMin = min;
        lastSec = sec;
    }

    firstDrawPomo = false; // Kunci render dasar
}