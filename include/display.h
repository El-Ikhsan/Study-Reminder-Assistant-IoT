#pragma once
#include <Arduino.h>

// 10 Kunci Emosi Wajah (Mimik)
enum Emotion
{
    EMOTION_HOT,          // Panas
    EMOTION_COLD,         // Dingin
    EMOTION_NOISY,        // Berisik
    EMOTION_SLEEPY,       // Ngantuk
    EMOTION_IDLE,         // Relax / Netral
    EMOTION_SURPRISED,    // Kaget / Terburu-buru
    EMOTION_DARK,         // Gelap
    EMOTION_SAD,          // Buruk / Stress / Menangis
    EMOTION_LISTENING,    // Mendengarkan (Wakenet aktif)
    EMOTION_UNCOMFORTABLE // Tidak nyaman
};

enum WidgetMode
{
    WIDGET_NONE,
    WIDGET_DIALOG,
    WIDGET_POMODORO
};

// Fungsi UI
void initDisplay();
void setDisplayBrightness(int percent);
void showBootingScreen();
void drawEmoji(Emotion emoji);
void showDialogWidget(String text);
void updatePomodoroWidget(int min, int sec, bool isBreak);
void clearWidget();
