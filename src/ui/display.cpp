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
void drawTopBar(bool isWifiConnected, bool isMicActive, String timeStr, String alertText)
{
    // Area Y: 0 sampai 19
    tft.fillRect(0, 0, 320, 20, TFT_BLACK);
    tft.drawLine(0, 19, 320, 19, tft.color565(50, 50, 50)); // Garis pembatas bawah

    // ==========================================
    // KIRI: Ikon Status (Load dari LittleFS)
    // ==========================================
    // Asumsi ukuran ikon adalah 16x16 pixel.
    // Y=2 agar ikon pas berada di tengah bar yang tingginya 20px.

    // 1. Ikon WiFi (Posisi X = 5)
    if (isWifiConnected)
    {
        TJpgDec.drawFsJpg(5, 2, "/wifi_on.jpg", LittleFS);
    }
    else
    {
        TJpgDec.drawFsJpg(5, 2, "/wifi_off.jpg", LittleFS);
    }

    // 2. Ikon Mic (Posisi X = 25)
    // Jarak 25 didapat dari: X awal (5) + Lebar Ikon (16) + Spasi (4)
    if (isMicActive)
    {
        TJpgDec.drawFsJpg(25, 2, "/mic_on.jpg", LittleFS);
    }
    else
    {
        TJpgDec.drawFsJpg(25, 2, "/mic_off.jpg", LittleFS);
    }

    // ==========================================
    // TENGAH: Jam Digital
    // ==========================================
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    // drawCentreString otomatis memposisikan teks di tengah kordinat X (160)
    tft.drawCentreString(timeStr, 160, 5, 1);

    // ==========================================
    // KANAN: Teks Peringatan Sensor (Rata Kanan)
    // ==========================================
    if (alertText != "")
    {
        tft.setTextColor(TFT_ORANGE);
        // drawRightString otomatis meratakan teks ke kiri dari titik X (315)
        tft.drawRightString(alertText, 315, 5, 1);
    }
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
        return;
    int bottomLimit = (currentWidget == WIDGET_NONE) ? 240 : 175;
    if (currentWidget == WIDGET_NONE && (currentEmoji == EMOTION_IDLE || currentEmoji == EMOTION_LISTENING))
    {
        bottomLimit = 240;
    }
    if (y >= bottomLimit)
        return;

    usPalette = pDraw->pPalette;
    s = pDraw->pPixels;
    for (x = 0; x < iWidth; x++)
    {
        // Murni membaca palet warna, tanpa mengecek transparansi!
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

    // 1. Siapkan Kanvas (Sprite)
    dialogSprite.createSprite(320, 65);
    dialogSprite.fillSprite(TFT_BLACK);
    dialogSprite.fillRoundRect(5, 2, 310, 61, 5, tft.color565(20, 20, 30));
    dialogSprite.drawRoundRect(5, 2, 310, 61, 5, TFT_CYAN);
    dialogSprite.setTextColor(TFT_WHITE);
    dialogSprite.setTextSize(2);

    int cX = 15, cY = 10;
    dialogSprite.setCursor(cX, cY);

    int charCount = 0;
    for (int i = 0; i < text.length(); i++)
    {
        if (cancelCurrentDialog)
            break;

        // 2. Logika Word-Wrap (Bungkus Kata)
        if (text[i] != ' ' && (i == 0 || text[i - 1] == ' '))
        {
            int wordWidth = 0;
            for (int j = i; j < text.length() && text[j] != ' '; j++)
                wordWidth += 12;

            if (cX + wordWidth > 300)
            {
                cX = 15;
                cY += 24; // Turun ke baris berikutnya

                // ✨ JIKA TEKS MELEBIHI 2 BARIS (Masuk baris ke-3)
                if (cY > 40)
                {
                    // ❌ Jeda 2.5 Detik (pageWait) DIHAPUS TOTAL!

                    // Langsung format ulang kanvas menjadi hitam seperti Subtitle baru
                    dialogSprite.fillSprite(TFT_BLACK);
                    dialogSprite.fillRoundRect(5, 2, 310, 61, 5, tft.color565(20, 20, 30));
                    dialogSprite.drawRoundRect(5, 2, 310, 61, 5, TFT_CYAN);
                    dialogSprite.setTextColor(TFT_WHITE);

                    cX = 15;
                    cY = 10; // Kembalikan kursor ke atas
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
        dialogSprite.pushSprite(0, 175);

        // 4. Mainkan SFX (Suara Efek Ketikan)
        bool makeSound = (text[i] != ' ' && charCount % 2 == 1);
        playTypingSync(makeSound);
    }

    playTypingSync(false);
    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM_0);

    // ✨ 5. JEDA AKHIR (Dipertahankan agar user bisa membaca)
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
    tft.fillRect(0, 175, 320, 65, TFT_BLACK);
    currentWidget = WIDGET_NONE;

    // Saat widget dibersihkan, trigger ulang drawEmoji agar
    // jika dia Idle/Mendengar, layarnya langsung merender animasi sampai bawah.
    Emotion temp = currentEmoji;
    currentEmoji = EMOTION_IDLE; // Hack memancing refresh
    drawEmoji(temp);
}

// ✨ UPDATE: Tambahan parameter cycle dan media belajar
void updatePomodoroWidget(int min, int sec, bool isBreak, int cycle, String media)
{
    if (currentWidget == WIDGET_DIALOG)
        return;

    if (currentWidget != WIDGET_POMODORO)
    {
        // Blok seluruh area bawah dengan warna gelap (Abu-abu sangat tua untuk bedakan dengan kanvas)
        tft.fillRect(0, 175, 320, 65, tft.color565(15, 15, 15));

        // Buat garis pemisah atas widget
        tft.drawLine(0, 175, 320, 175, TFT_DARKGREY);
        currentWidget = WIDGET_POMODORO;
    }

    // A. AREA KIRI: INFO DETAIL (Membersihkan blok kiri saja)
    tft.fillRect(5, 178, 175, 60, tft.color565(15, 15, 15));

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
        tft.drawString("FOKUS KERJA", 10, 182);
    }

    // 2. Teks Siklus & Media Belajar (Font lebih kecil)
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Siklus: " + String(cycle) + "/4", 10, 205);

    // Batasi panjang string media agar tidak tabrakan dengan timer
    if (media.length() > 15)
        media = media.substring(0, 12) + "...";
    tft.drawString("Media: " + media, 10, 220);

    // B. AREA KANAN: TIMER RAKSASA (Membersihkan blok kanan saja)
    tft.fillRect(180, 178, 135, 60, tft.color565(15, 15, 15));

    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", min, sec);
    tft.setTextColor(TFT_WHITE);

    // Menggunakan font besar (Size 4 atau 5) ditempatkan rata kanan
    tft.drawCentreString(timeStr, 250, 195, 4);
}