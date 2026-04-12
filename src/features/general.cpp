#include "features/general.h"
#include "network/websocket.h"
#include "ui/display.h"
#include <ArduinoJson.h>

namespace
{
    bool isRecording = false;
    unsigned long lastVoiceTime = 0;
    unsigned long recordingStartTime = 0;

    // Konfigurasi Durasi
    const unsigned long MAX_SILENCE_MS = 1500; // Berhenti jika diam 1.5 detik
    const unsigned long MAX_RECORD_MS = 10000; // Batas maksimal rekaman 10 detik
}

bool voiceChat_isRecording()
{
    return isRecording;
}

void voiceChat_startRecording()
{
    isRecording = true;
    lastVoiceTime = millis();
    recordingStartTime = millis();

    // Beri tahu backend bahwa rentetan audio (Stream) akan dimulai
    JsonDocument doc;
    doc["type"] = "AUDIO_STREAM_START";
    String jsonStr;
    serializeJson(doc, jsonStr);
    sendRawWS(jsonStr);

    Serial.println("[🎤] Mulai Merekam... Silakan bicara!");
}

void voiceChat_stopRecording()
{
    isRecording = false;
    Serial.println("[🎤] Selesai merekam. Menunggu balasan AI...");

    JsonDocument doc;
    doc["type"] = "AUDIO_STREAM_END";
    String jsonStr;
    serializeJson(doc, jsonStr);
    sendRawWS(jsonStr);

    // ✨ FIX UI GLITCH:
    // HAPUS fungsi showDialogWidget("Memproses suara...") dari sini!
    // Cukup gunakan drawEmoji yang sifatnya instan dan tidak memblokir Core.
    forceClearDialog();
    drawEmoji(EMOTION_SURPRISED);
}

void voiceChat_feedAudio(int16_t *audio_data, size_t data_size, int vad_state)
{
    if (!isRecording)
        return;

    unsigned long now = millis();

    // 1. Kirim potongan suara (Chunk) langsung ke WebSocket
    sendAudioChunkWS((uint8_t *)audio_data, data_size);

    // 2. Logika VAD (Voice Activity Detection) dari ESP-SR
    // vad_state = 1 berarti ada suara manusia terdeteksi (Speech)
    if (vad_state == 1)
    {
        lastVoiceTime = now;
    }

    // 3. Cek kondisi Berhenti
    bool isSilenceTimeout = (now - lastVoiceTime > MAX_SILENCE_MS);
    bool isMaxTimeReached = (now - recordingStartTime > MAX_RECORD_MS);

    if (isSilenceTimeout || isMaxTimeReached)
    {
        if (isSilenceTimeout)
            Serial.println("[⏱️] Diam terdeteksi (1.5 detik).");
        if (isMaxTimeReached)
            Serial.println("[⏱️] Batas waktu 10 detik tercapai.");

        voiceChat_stopRecording();
    }
}