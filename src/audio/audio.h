#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

void initAudio();
void audioLoop();
void playAudioLocal(const char *path);
void playAudioSFX(const char *path);
void playAudioUrl(const String &url);
void stopAudio();
void setVolumePercent(int percent);
void playTypingCodeClick();
void playTypingSync(bool withSound);
uint32_t getAudioFilePos();
bool audio_isPlaying();

#endif