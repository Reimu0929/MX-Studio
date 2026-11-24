#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "msx/common.h"
#include "msx/wav.h"

#define WM_APP_AUDIO_DONE (WM_APP + 10)
#define WM_APP_AUDIO_ERROR (WM_APP + 11)

enum class AudioOutcome {
    RecordDone = 1,
    PlayDone = 2,
};

class AudioEngine {
public:
    explicit AudioEngine(HWND notify);
    ~AudioEngine();

    void setInputDevice(unsigned deviceId);
    unsigned inputDevice() const;
    bool startRecording(const std::wstring& path, std::string& error);
    bool startMonitoring(std::string& error);
    void stopRecording();

    bool startPlayback(const std::vector<uint8_t>& wav, std::string& error);
    void stopPlayback();
    void stopAll();

    bool isRecording() const;
    bool isMonitoring() const;
    bool isPlaying() const;
    bool recordOverrun() const;

    double inputDbfs() const;
    double outputDbfs() const;
    bool clipLatched() const;
    void resetClip();
    unsigned recordSeconds() const;
    double playbackProgress() const;

    std::string lastError() const;

private:
    struct Impl;
    Impl* impl_;
};
