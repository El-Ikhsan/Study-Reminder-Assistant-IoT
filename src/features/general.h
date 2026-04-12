#pragma once
#include <Arduino.h>

void voiceChat_startRecording();
void voiceChat_stopRecording();
void voiceChat_feedAudio(int16_t *audio_data, size_t data_size, int vad_state);
bool voiceChat_isRecording();