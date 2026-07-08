#pragma once
#include <Arduino.h>

// ✨ HANYA 8 EMOSI (1-to-1 dengan file GIF)
enum Emotion
{
    EMOTION_HOT,       // panas
    EMOTION_COLD,      // dingin_extrem
    EMOTION_DARK,      // gelap
    EMOTION_GLARE,     // silau
    EMOTION_NOISY,     // bising
    EMOTION_LISTENING, // mendengar suara
    EMOTION_SMILE,     // pemulihan / sensor membaik
    EMOTION_IDLE       // idle / standby
};
Emotion parseEmotionString(String emoStr);

enum WidgetMode
{
    WIDGET_NONE,
    WIDGET_DIALOG,
    WIDGET_POMODORO
};

extern volatile bool cancelCurrentDialog;

void initDisplay();
void setDisplayBrightness(int percent);
void showBootingScreen();
void drawTopBar(bool isWifiConnected, bool isMicActive, String timeStr, String alertText);
void playDisplayAnimation();
void drawEmoji(Emotion emoji);
void showDialogWidget(String text);
void showPersistentDialog(String text); // ✨ Teks tetap di layar, tidak auto-clear
void updatePomodoroWidget(int min, int sec, bool isBreak, int cycle, int totalCycles, String media);
void clearWidget();
void forceUpdateTopBarAlert(String alertText);
void forceClearDialog();

// ✨ COUNTDOWN WIDGET (untuk proses konek WiFi):
// - showCountdownWidget : render teks statis SEKALI (tanpa animasi ketik)
// - updateCountdownSeconds: hanya update angka detik, dipanggil tiap detik
void showCountdownWidget(String line1, String line2);
void updateCountdownSeconds(int seconds);

// Antre pesan dialog agar ditampilkan dari main loop (tidak blocking wsLoop)
void queueDialogWidget(String text);
void processDialogQueue();