#pragma once
#include <Arduino.h>
#include "audio.h"

// 7 Kunci Sound Effect
enum SoundEvent
{
    SND_BOOTING,
    SND_AI_NOTIFY,
    SND_TEXT_BLIP,
    SND_POMO_START,
    SND_POMO_STOP,
    SND_POMO_SWITCH,
    SND_POMO_CANCEL
};

inline void playRinchanSound(SoundEvent event)
{
    switch (event)
    {
    case SND_BOOTING:
        playAudioSFX("/booting.wav");
        break;
    case SND_AI_NOTIFY:
        playAudioSFX("/ai_notify.mp3");
        break;
    case SND_TEXT_BLIP:
        playTypingCodeClick();
        break;
    case SND_POMO_START:
        playAudioSFX("/pomo_start.wav");
        break;
    case SND_POMO_STOP:
        playAudioSFX("/pomo_stop.wav");
        break;
    case SND_POMO_SWITCH:
        playAudioSFX("/pomo_switch.wav");
        break;
    case SND_POMO_CANCEL:
        playAudioSFX("/pomo_cancel.wav");
        break;
    }
}

// ✨ SFX BLOCKING: Putar dan TUNGGU sampai selesai sebelum lanjut
// Mencegah konflik I2S dengan dialog typing yang datang setelahnya
inline void playRinchanSoundBlocking(SoundEvent event)
{
    playRinchanSound(event);

    unsigned long timeout = millis() + 10000; // Safety: max 10 detik
    while (audio_isPlaying() && millis() < timeout)
    {
        audioLoop(); // Feed I2S decoder agar audio tidak putus
        delay(1);
    }
}

// ✨ ALARM BERULANG: Untuk notifikasi sesi Pomodoro selesai
inline void playRinchanAlarm(int repeats = 1)
{
    for (int i = 0; i < repeats; i++)
    {
        playAudioSFX("/pomo_stop.wav");

        unsigned long timeout = millis() + 10000; // pomo_stop.wav = 7 detik
        while (audio_isPlaying() && millis() < timeout)
        {
            audioLoop();
            delay(1);
        }

        if (i < repeats - 1)
            delay(300); // Jeda antar pengulangan
    }
}