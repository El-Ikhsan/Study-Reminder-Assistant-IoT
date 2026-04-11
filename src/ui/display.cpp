#include "display.h"
#include "audio/sound_manager.h"
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <TJpg_Decoder.h>
#include "audio/audio.h"
#include <driver/i2s.h>

// Gunakan pin PWM untuk backlight layarmu yang terhubung ke Transistor
#define TFT_BL_PIN 39

namespace
{
    TFT_eSPI tft = TFT_eSPI();
    Emotion currentEmoji = EMOTION_IDLE;
    WidgetMode currentWidget = WIDGET_NONE;
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
    tft.setRotation(1); // Mode Landscape
    tft.fillScreen(TFT_BLACK);

    TJpgDec.setCallback(tft_output);
    setDisplayBrightness(80);
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

        drawEmoji(EMOTION_SLEEPY);
        showDialogWidget("Membangunkan Rinchan...");
    }
}

// ==========================================
// 1. AREA ATAS: 10 EMOJI WAJAH
// ==========================================
void drawEmoji(Emotion emoji)
{
    tft.setRotation(1);
    // Hanya hapus area wajah (Y: 0 sampai 149) agar widget bawah tidak kedip
    tft.fillRect(0, 0, 320, 150, TFT_BLACK);
    currentEmoji = emoji;

    // Siapkan teks di tengah area atas
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(4); // Font besar untuk Kaomoji

    String faceStr = "";

    switch (emoji)
    {
    case EMOTION_HOT:
        faceStr = "(;-;)~";
        break; // Keringetan
    case EMOTION_COLD:
        faceStr = "(>_<)*";
        break; // Menggigil
    case EMOTION_NOISY:
        faceStr = "(x_x)";
        break; // Pusing berisik
    case EMOTION_SLEEPY:
        faceStr = "(-_-)zZ";
        break; // Ngantuk
    case EMOTION_IDLE:
        faceStr = "(^o^)";
        break; // Default / Netral
    case EMOTION_SURPRISED:
        faceStr = "(O_O)!";
        break; // Kaget
    case EMOTION_DARK:
        faceStr = "(._.)";
        break; // Gelap / Takut
    case EMOTION_SAD:
        faceStr = "(T_T)";
        break; // Nangis
    case EMOTION_LISTENING:
        faceStr = "(O_o)";
        break; // Kuping naik satu
    case EMOTION_UNCOMFORTABLE:
        faceStr = "(~_~;)";
        break; // Canggung
    }

    // Tampilkan wajah teks persis di tengah atas
    tft.drawCentreString(faceStr, 160, 60, 4);
}

// ==========================================
// 2. AREA BAWAH: KOTAK DIALOG TEKS
// ==========================================
volatile bool cancelCurrentDialog = false;
TFT_eSprite dialogSprite = TFT_eSprite(&tft);

void forceClearDialog()
{
    cancelCurrentDialog = true;
}

void showDialogWidget(String text)
{
    // ✨ AWAL: Matikan library MP3 secara resmi
    stopAudio();
    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM_0);
    delay(10);

    cancelCurrentDialog = false;
    currentWidget = WIDGET_DIALOG;

    dialogSprite.createSprite(320, 90);
    dialogSprite.fillSprite(TFT_BLACK);
    dialogSprite.fillRoundRect(5, 5, 310, 80, 5, tft.color565(20, 20, 30));
    dialogSprite.drawRoundRect(5, 5, 310, 80, 5, TFT_CYAN);

    dialogSprite.setTextColor(TFT_WHITE);
    dialogSprite.setTextSize(2);
    int cX = 15, cY = 15;
    dialogSprite.setCursor(cX, cY);

    int charCount = 0;
    for (int i = 0; i < text.length(); i++)
    {
        if (cancelCurrentDialog)
            break;

        if (cX > 290 && text[i] == ' ')
        {
            cX = 15;
            cY += 25;
            dialogSprite.setCursor(cX, cY);
        }
        else
        {
            dialogSprite.print(text[i]);
            cX += 12;
        }

        charCount++;
        dialogSprite.pushSprite(0, 150);

        bool makeSound = (text[i] != ' ' && charCount % 2 == 1);
        playTypingSync(makeSound);
    }

    // ✨ THE SILENT FLUSHER (PEMBUNUH NOISE) ✨
    // Tembakkan 2 blok suara "Hening" (0 Volt) secara manual agar amplifier rileks
    playTypingSync(false);
    playTypingSync(false);

    // Setelah amplifier tenang, baru kita bersihkan buffer secara paksa
    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM_0);

    dialogSprite.deleteSprite();

    // Jeda baca teks
    if (!cancelCurrentDialog)
    {
        unsigned long readStart = millis();
        while (millis() - readStart < 2000)
        {
            if (cancelCurrentDialog)
                break;
            delay(10);
        }
    }
}
// ==========================================
// 3. AREA BAWAH: TIMER POMODORO
// ==========================================
void updatePomodoroWidget(int min, int sec, bool isBreak)
{
    if (currentWidget != WIDGET_POMODORO)
    {
        tft.fillRect(0, 150, 320, 90, TFT_BLACK);
        currentWidget = WIDGET_POMODORO;
    }

    // Bersihkan hanya blok angka
    tft.fillRect(10, 150, 300, 90, TFT_BLACK);

    // Label Mode Pomodoro
    tft.setTextSize(2);
    if (isBreak)
    {
        tft.setTextColor(TFT_GREEN);
        tft.drawCentreString("ISTIRAHAT", 160, 155, 2);
    }
    else
    {
        tft.setTextColor(TFT_ORANGE);
        tft.drawCentreString("FOKUS KERJA", 160, 155, 2);
    }

    // Angka Timer
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", min, sec);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString(timeStr, 160, 180, 6);
}

void clearWidget()
{
    tft.fillRect(0, 150, 320, 90, TFT_BLACK);
    currentWidget = WIDGET_NONE;
}