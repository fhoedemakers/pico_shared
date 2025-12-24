#pragma once
#include <cstdint>
#include <cstddef>
#define SOUNDRECORDERFILE "/soundrecorder.wav"
namespace SoundRecorder {
    bool isRecording();
    void startRecording();
    void recordFrame(const int16_t* pcmData, size_t numSamples);
}   // namespace SoundRecorder