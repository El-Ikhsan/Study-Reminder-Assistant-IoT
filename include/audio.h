#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

void initAudio();
void audioLoop();
void playAudioLocal(const char *path);
void playAudioUrl(const String &url);
void stopAudio();
void setVolumePercent(int percent);

#endif