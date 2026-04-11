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
        // Blip sintetis dari kode (tanpa file), lebih stabil untuk efek typing.
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