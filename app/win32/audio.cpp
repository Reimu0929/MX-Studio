#include "audio.h"

#include <mmsystem.h>
#include <process.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "msx/wav.h"
#include "win_util.h"

// This file deliberately uses Win32 threads and critical sections instead of
// std::thread / std::mutex.  The MSVC C++ runtime implements those with
// Vista-era primitives (SRW locks, condition variables), which would break the
// Windows XP target.  CREATE_THREAD, CRITICAL_SECTION, events and Interlocked*
// are available on Windows XP SP3.

namespace {

const unsigned kSampleRate = 48000;
const int kInBuffers = 8;
const int kInFrames = 4096;
const int kOutBuffers = 4;
const int kOutFrames = 4096;

double peakToDbfs(int peak) {
    if (peak <= 0) return -999.0;
    return 20.0 * std::log10(static_cast<double>(peak) / 32768.0);
}

int peakOf(const int16_t* data, std::size_t count) {
    int peak = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const int v = data[i] < 0 ? -static_cast<int>(data[i]) : static_cast<int>(data[i]);
        if (v > peak) peak = v;
    }
    return peak;
}

}  // namespace

struct AudioEngine::Impl {
    explicit Impl(HWND hwnd) : notify(hwnd) {
        InitializeCriticalSection(&recCs);
        InitializeCriticalSection(&playCs);
        recWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        playWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    ~Impl() {
        if (playThread) {
            WaitForSingleObject(playThread, 1000);
            CloseHandle(playThread);
        }
        if (recThread) {
            WaitForSingleObject(recThread, 1000);
            CloseHandle(recThread);
        }
        DeleteCriticalSection(&recCs);
        DeleteCriticalSection(&playCs);
        if (recWake) CloseHandle(recWake);
        if (playWake) CloseHandle(playWake);
        delete recordFile;
    }

    HWND notify;
    CRITICAL_SECTION recCs;
    CRITICAL_SECTION playCs;

    // recording
    HWAVEIN hIn = nullptr;
    WAVEHDR inHeaders[kInBuffers];
    std::vector<std::vector<int16_t>> inData;
    std::vector<bool> inWritten;
    int channels = 1;
    unsigned inputDevice = WAVE_MAPPER;
    HANDLE recWake = nullptr;
    HANDLE recThread = nullptr;
    std::deque<int> recReady;
    bool recStopping = false;
    bool recFailed = false;
    bool recOverrun = false;
    bool recording = false;
    bool monitoring = false;
    std::wstring recordPath;
    msx::WavWriter wav;
    Win32FileWriter* recordFile = nullptr;
    msx::Diagnostics diags;
    std::string error;
    volatile LONG inPeak = 0;
    volatile LONG inClip = 0;
    volatile LONG inFrames = 0;

    // playback
    HWAVEOUT hOut = nullptr;
    WAVEHDR outHeaders[kOutBuffers];
    std::vector<int16_t> pcm;
    std::vector<int16_t> outChunks[kOutBuffers];
    msx::WavFormat playFormat;
    HANDLE playWake = nullptr;
    HANDLE playThread = nullptr;
    std::deque<int> outFree;
    int outInFlight = 0;
    std::size_t outPos = 0;
    bool playStopping = false;
    bool playError = false;
    volatile LONG playing = 0;
    volatile LONG outPeak = 0;
    volatile LONG outFrames = 0;
    volatile LONG outTotal = 0;

    static void CALLBACK inProc(HWAVEIN, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR) {
        if (msg != WIM_DATA) return;
        Impl* self = reinterpret_cast<Impl*>(instance);
        WAVEHDR* header = reinterpret_cast<WAVEHDR*>(param1);
        self->onInput(static_cast<int>(header->dwUser));
    }

    static void CALLBACK outProc(HWAVEOUT, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR) {
        if (msg != WOM_DONE) return;
        Impl* self = reinterpret_cast<Impl*>(instance);
        WAVEHDR* header = reinterpret_cast<WAVEHDR*>(param1);
        self->onOutputDone(static_cast<int>(header->dwUser));
    }

    static unsigned __stdcall recThreadProc(void* param) {
        static_cast<Impl*>(param)->recordLoop();
        return 0;
    }

    static unsigned __stdcall playThreadProc(void* param) {
        static_cast<Impl*>(param)->playLoop();
        return 0;
    }

    void setError(const std::string& text) {
        EnterCriticalSection(&recCs);
        recFailed = true;
        LeaveCriticalSection(&recCs);
        EnterCriticalSection(&playCs);
        error = text;
        LeaveCriticalSection(&playCs);
    }

    void onInput(int index) {
        EnterCriticalSection(&recCs);
        if (!recStopping) {
            recReady.push_back(index);
            if (static_cast<int>(recReady.size()) >= kInBuffers) recOverrun = true;
        }
        LeaveCriticalSection(&recCs);
        SetEvent(recWake);
    }

    void onOutputDone(int index) {
        EnterCriticalSection(&playCs);
        if (outInFlight > 0) --outInFlight;
        outFree.push_back(index);
        LeaveCriticalSection(&playCs);
        SetEvent(playWake);
    }

    void writeRecordBuffer(int index) {
        std::vector<int16_t>& buffer = inData[static_cast<std::size_t>(index)];
        const std::size_t frames = buffer.size() / channels;
        const int peak = peakOf(&buffer[0], frames * channels);
        InterlockedExchange(&inPeak, peak);
        if (peak >= 32000) InterlockedExchange(&inClip, 1);
        InterlockedExchangeAdd(&inFrames, static_cast<LONG>(frames));
        if (channels == 2) {
            for (std::size_t i = 0; i < frames; ++i) buffer[i] = buffer[i * 2];
        }
        if (!monitoring && !wav.writeFrames(&buffer[0], frames, diags)) {
            setError("録音データの書き込みに失敗しました（ディスク容量または書き込みエラー）");
        }
        inWritten[static_cast<std::size_t>(index)] = true;
    }

    bool pushRecordBuffer(int index) {
        WAVEHDR& h = inHeaders[index];
        std::memset(&h, 0, sizeof(h));
        h.lpData = reinterpret_cast<LPSTR>(&inData[static_cast<std::size_t>(index)][0]);
        h.dwBufferLength = static_cast<DWORD>(inData[static_cast<std::size_t>(index)].size() * sizeof(int16_t));
        h.dwUser = static_cast<DWORD_PTR>(index);
        if (waveInPrepareHeader(hIn, &h, sizeof(h)) != MMSYSERR_NOERROR) return false;
        return waveInAddBuffer(hIn, &h, sizeof(h)) == MMSYSERR_NOERROR;
    }

    void recordLoop() {
        for (;;) {
            WaitForSingleObject(recWake, 250);
            int index = -1;
            bool stopNow = false;
            EnterCriticalSection(&recCs);
            if (!recReady.empty()) {
                index = recReady.front();
                recReady.pop_front();
            }
            if (index < 0) {
                stopNow = recStopping;
                LeaveCriticalSection(&recCs);
                if (stopNow) break;
                continue;
            }
            const bool failed = recFailed;
            LeaveCriticalSection(&recCs);
            if (failed) {
                inWritten[static_cast<std::size_t>(index)] = true;
                continue;
            }
            writeRecordBuffer(index);
            bool rearm = false;
            EnterCriticalSection(&recCs);
            rearm = !recStopping && !recFailed;
            LeaveCriticalSection(&recCs);
            if (rearm && !pushRecordBuffer(index)) {
                setError("録音バッファをドライバーへ戻せませんでした");
            }
        }
    }

    bool openInput(std::string& err) {
        WAVEFORMATEX fmt;
        std::memset(&fmt, 0, sizeof(fmt));
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = kSampleRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = static_cast<WORD>(fmt.nChannels * fmt.wBitsPerSample / 8);
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        MMRESULT result = waveInOpen(&hIn, inputDevice, &fmt, reinterpret_cast<DWORD_PTR>(&inProc),
                                     reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
        if (result != MMSYSERR_NOERROR) {
            fmt.nChannels = 2;
            fmt.nBlockAlign = 4;
            fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
            result = waveInOpen(&hIn, inputDevice, &fmt, reinterpret_cast<DWORD_PTR>(&inProc),
                                reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
            if (result != MMSYSERR_NOERROR) {
                err = "48 kHz / 16 bit の録音デバイスを開けません（モノラル・ステレオとも失敗）";
                return false;
            }
        }
        channels = fmt.nChannels;
        return true;
    }

    bool startInput(const std::wstring& path, bool monitor, std::string& err) {
        EnterCriticalSection(&recCs);
        if (recording) {
            LeaveCriticalSection(&recCs);
            err = "すでに録音中です";
            return false;
        }
        recStopping = false;
        recFailed = false;
        recOverrun = false;
        recReady.clear();
        error.clear();
        LeaveCriticalSection(&recCs);
        InterlockedExchange(&inPeak, 0);
        InterlockedExchange(&inClip, 0);
        InterlockedExchange(&inFrames, 0);
        diags = msx::Diagnostics();
        recordPath = path;
        if (!openInput(err)) return false;

        if (!monitor) {
            recordFile = new Win32FileWriter(path);
            if (!recordFile->isOpen()) {
                delete recordFile;
                recordFile = nullptr;
                waveInClose(hIn);
                hIn = nullptr;
                err = "録音ファイルを作成できません";
                return false;
            }
            msx::WavFormat wfmt;
            wfmt.sampleRate = kSampleRate;
            wfmt.channels = 1;
            wfmt.bitsPerSample = 16;
            if (!wav.open(*recordFile, wfmt, diags)) {
                delete recordFile;
                recordFile = nullptr;
                waveInClose(hIn);
                hIn = nullptr;
                err = "WAV ヘッダーを作成できません";
                return false;
            }
        }

        inData.assign(static_cast<std::size_t>(kInBuffers), std::vector<int16_t>(kInFrames * channels, 0));
        inWritten.assign(static_cast<std::size_t>(kInBuffers), false);
        for (int i = 0; i < kInBuffers; ++i) {
            if (!pushRecordBuffer(i)) {
                if (!monitor) {
                    wav.close(diags);
                    delete recordFile;
                    recordFile = nullptr;
                }
                waveInClose(hIn);
                hIn = nullptr;
                err = "録音バッファを準備できません";
                return false;
            }
        }
        monitoring = monitor;
        recording = true;
        recThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &Impl::recThreadProc, this, 0, nullptr));
        if (!recThread) {
            recording = false;
            monitoring = false;
            if (!monitor) {
                wav.close(diags);
                delete recordFile;
                recordFile = nullptr;
            }
            waveInClose(hIn);
            hIn = nullptr;
            err = "録音スレッドを作成できません";
            return false;
        }
        if (waveInStart(hIn) != MMSYSERR_NOERROR) {
            EnterCriticalSection(&recCs);
            recStopping = true;
            LeaveCriticalSection(&recCs);
            SetEvent(recWake);
            WaitForSingleObject(recThread, 5000);
            CloseHandle(recThread);
            recThread = nullptr;
            monitoring = false;
            if (!monitor) {
                wav.close(diags);
                delete recordFile;
                recordFile = nullptr;
            }
            waveInClose(hIn);
            hIn = nullptr;
            recording = false;
            err = "録音を開始できません";
            return false;
        }
        return true;
    }

    void stopRecord() {
        const bool wasMonitoring = monitoring;
        if (hIn) {
            waveInStop(hIn);
            waveInReset(hIn);
        }
        EnterCriticalSection(&recCs);
        recStopping = true;
        const bool failed = recFailed;
        LeaveCriticalSection(&recCs);
        SetEvent(recWake);
        if (recThread) {
            WaitForSingleObject(recThread, 5000);
            CloseHandle(recThread);
            recThread = nullptr;
        }
        Sleep(30);
        EnterCriticalSection(&recCs);
        while (!recReady.empty()) {
            const int index = recReady.front();
            recReady.pop_front();
            if (inWritten[static_cast<std::size_t>(index)]) continue;
            writeRecordBuffer(index);
        }
        LeaveCriticalSection(&recCs);
        if (hIn) {
            for (int i = 0; i < kInBuffers; ++i) waveInUnprepareHeader(hIn, &inHeaders[i], sizeof(WAVEHDR));
            waveInClose(hIn);
            hIn = nullptr;
        }
        bool finalFailed = failed;
        if (!wasMonitoring) {
            if (!wav.close(diags)) finalFailed = true;
            if (wav.sizeLimitReached()) {
                finalFailed = true;
                EnterCriticalSection(&playCs);
                error = "WAV が 2 GiB の上限に達したため、録音を打ち切りました";
                LeaveCriticalSection(&playCs);
            }
            if (diags.hasErrors()) finalFailed = true;
            if (recordFile) {
                recordFile->flush();
                delete recordFile;
                recordFile = nullptr;
            }
        }
        if (recOverrun) {
            finalFailed = true;
            EnterCriticalSection(&playCs);
            error = "録音中にバッファ欠落が発生しました。この WAV は不完全です";
            LeaveCriticalSection(&playCs);
        }
        recording = false;
        monitoring = false;
        if (finalFailed) {
            PostMessageW(notify, WM_APP_AUDIO_ERROR, 0, 0);
        } else if (!wasMonitoring) {
            PostMessageW(notify, WM_APP_AUDIO_DONE, static_cast<WPARAM>(AudioOutcome::RecordDone), 0);
        }
    }

    void setPlayError(const std::string& text) {
        EnterCriticalSection(&playCs);
        playError = true;
        error = text;
        LeaveCriticalSection(&playCs);
    }

    void playLoop() {
        WAVEFORMATEX fmt;
        std::memset(&fmt, 0, sizeof(fmt));
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = static_cast<WORD>(playFormat.channels);
        fmt.nSamplesPerSec = playFormat.sampleRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = static_cast<WORD>(fmt.nChannels * 2);
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        if (waveOutOpen(&hOut, WAVE_MAPPER, &fmt, reinterpret_cast<DWORD_PTR>(&outProc),
                        reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
            setPlayError("音声出力デバイスを開けません（48 kHz / 16 bit）");
            InterlockedExchange(&playing, 0);
            PostMessageW(notify, WM_APP_AUDIO_ERROR, 0, 0);
            return;
        }
        EnterCriticalSection(&playCs);
        outFree.clear();
        for (int i = 0; i < kOutBuffers; ++i) outFree.push_back(i);
        outInFlight = 0;
        LeaveCriticalSection(&playCs);

        const std::size_t totalFrames = pcm.size() / playFormat.channels;
        while (!playStopping && outPos < totalFrames) {
            int index = -1;
            EnterCriticalSection(&playCs);
            if (!outFree.empty()) {
                index = outFree.front();
                outFree.pop_front();
            }
            LeaveCriticalSection(&playCs);
            if (index < 0) {
                WaitForSingleObject(playWake, 100);
                continue;
            }
            const std::size_t frames = std::min<std::size_t>(kOutFrames, totalFrames - outPos);
            const std::size_t samples = frames * playFormat.channels;
            std::vector<int16_t>& chunk = outChunks[static_cast<std::size_t>(index)];
            chunk.resize(samples);
            std::memcpy(&chunk[0], &pcm[outPos * playFormat.channels], samples * sizeof(int16_t));
            InterlockedExchange(&outPeak, peakOf(&chunk[0], samples));

            WAVEHDR& h = outHeaders[index];
            std::memset(&h, 0, sizeof(h));
            h.lpData = reinterpret_cast<LPSTR>(&chunk[0]);
            h.dwBufferLength = static_cast<DWORD>(samples * sizeof(int16_t));
            h.dwUser = static_cast<DWORD_PTR>(index);
            if (waveOutPrepareHeader(hOut, &h, sizeof(h)) != MMSYSERR_NOERROR) {
                setPlayError("再生ヘッダーを準備できません");
                break;
            }
            if (waveOutWrite(hOut, &h, sizeof(h)) != MMSYSERR_NOERROR) {
                setPlayError("再生を開始できません");
                break;
            }
            EnterCriticalSection(&playCs);
            ++outInFlight;
            LeaveCriticalSection(&playCs);
            outPos += frames;
            InterlockedExchange(&outFrames, static_cast<LONG>(outPos));
        }

        for (int i = 0; i < 200 && !playStopping; ++i) {
            EnterCriticalSection(&playCs);
            const int inFlight = outInFlight;
            LeaveCriticalSection(&playCs);
            if (inFlight <= 0) break;
            WaitForSingleObject(playWake, 50);
        }
        if (hOut) {
            if (playStopping) waveOutReset(hOut);
            for (int i = 0; i < kOutBuffers; ++i) waveOutUnprepareHeader(hOut, &outHeaders[i], sizeof(WAVEHDR));
            waveOutClose(hOut);
            hOut = nullptr;
        }
        EnterCriticalSection(&playCs);
        const bool playFailed = playError;
        InterlockedExchange(&playing, 0);
        LeaveCriticalSection(&playCs);
        if (playStopping) {
            // silent stop
        } else if (playFailed) {
            PostMessageW(notify, WM_APP_AUDIO_ERROR, 0, 0);
        } else {
            PostMessageW(notify, WM_APP_AUDIO_DONE, static_cast<WPARAM>(AudioOutcome::PlayDone), 0);
        }
    }

    bool startPlay(const std::vector<uint8_t>& wavBytes, std::string& err) {
        if (InterlockedCompareExchange(&playing, 0, 0) != 0) {
            err = "すでに再生中です";
            return false;
        }
        if (playThread) {
            WaitForSingleObject(playThread, 1000);
            CloseHandle(playThread);
            playThread = nullptr;
        }
        msx::MemoryReader reader(wavBytes);
        msx::WavReader wavRead;
        msx::Diagnostics readDiags;
        if (!wavRead.open(reader, readDiags)) {
            err = "生成した WAV を読めません";
            return false;
        }
        playFormat = wavRead.format();
        const std::size_t frames = static_cast<std::size_t>(wavRead.dataFrames());
        pcm.assign(frames * playFormat.channels, 0);
        std::size_t done = 0;
        while (done < frames) {
            const std::size_t n = wavRead.readFrames(&pcm[done * playFormat.channels], frames - done, readDiags);
            if (n == 0) break;
            done += n;
        }
        pcm.resize(done * playFormat.channels);
        if (pcm.empty()) {
            err = "生成した WAV に音声がありません";
            return false;
        }
        outPos = 0;
        EnterCriticalSection(&playCs);
        outFree.clear();
        outInFlight = 0;
        playStopping = false;
        playError = false;
        InterlockedExchange(&playing, 1);
        LeaveCriticalSection(&playCs);
        InterlockedExchange(&outPeak, 0);
        InterlockedExchange(&outFrames, 0);
        InterlockedExchange(&outTotal, static_cast<LONG>(done));
        playThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &Impl::playThreadProc, this, 0, nullptr));
        if (!playThread) {
            InterlockedExchange(&playing, 0);
            err = "再生スレッドを作成できません";
            return false;
        }
        return true;
    }

    void stopPlay() {
        EnterCriticalSection(&playCs);
        playStopping = true;
        LeaveCriticalSection(&playCs);
        SetEvent(playWake);
        if (playThread) {
            WaitForSingleObject(playThread, 10000);
            CloseHandle(playThread);
            playThread = nullptr;
        }
        InterlockedExchange(&playing, 0);
    }

    void stopAll() {
        if (recording) stopRecord();
        if (InterlockedCompareExchange(&playing, 0, 0) != 0 || playThread) stopPlay();
    }

    double inputDbfs() const {
        return peakToDbfs(static_cast<int>(InterlockedCompareExchange(const_cast<LONG*>(&inPeak), 0, 0)));
    }
    double outputDbfs() const {
        return peakToDbfs(static_cast<int>(InterlockedCompareExchange(const_cast<LONG*>(&outPeak), 0, 0)));
    }
    bool clip() const { return InterlockedCompareExchange(const_cast<LONG*>(&inClip), 0, 0) != 0; }
    void resetClip() {
        InterlockedExchange(const_cast<LONG*>(&inClip), 0);
        InterlockedExchange(const_cast<LONG*>(&inPeak), 0);
    }
    unsigned seconds() const {
        return static_cast<unsigned>(InterlockedCompareExchange(const_cast<LONG*>(&inFrames), 0, 0) / kSampleRate);
    }
    double progress() const {
        const LONG total = InterlockedCompareExchange(const_cast<LONG*>(&outTotal), 0, 0);
        const LONG done = InterlockedCompareExchange(const_cast<LONG*>(&outFrames), 0, 0);
        return total <= 0 ? 0.0 : static_cast<double>(done) / static_cast<double>(total);
    }
    std::string lastError() const {
        EnterCriticalSection(const_cast<CRITICAL_SECTION*>(&playCs));
        std::string copy = error;
        LeaveCriticalSection(const_cast<CRITICAL_SECTION*>(&playCs));
        return copy;
    }
};

AudioEngine::AudioEngine(HWND notify) : impl_(new Impl(notify)) {}

AudioEngine::~AudioEngine() {
    stopAll();
    delete impl_;
}

void AudioEngine::setInputDevice(unsigned deviceId) { impl_->inputDevice = deviceId; }
unsigned AudioEngine::inputDevice() const { return impl_->inputDevice; }

bool AudioEngine::startRecording(const std::wstring& path, std::string& error) {
    return impl_->startInput(path, false, error);
}

bool AudioEngine::startMonitoring(std::string& error) {
    return impl_->startInput(std::wstring(), true, error);
}

void AudioEngine::stopRecording() { impl_->stopRecord(); }

bool AudioEngine::startPlayback(const std::vector<uint8_t>& wav, std::string& error) {
    return impl_->startPlay(wav, error);
}

void AudioEngine::stopPlayback() { impl_->stopPlay(); }

void AudioEngine::stopAll() { impl_->stopAll(); }

bool AudioEngine::isRecording() const { return impl_->recording; }
bool AudioEngine::isMonitoring() const { return impl_->monitoring; }
bool AudioEngine::isPlaying() const {
    return InterlockedCompareExchange(const_cast<volatile LONG*>(&impl_->playing), 0, 0) != 0;
}
bool AudioEngine::recordOverrun() const { return impl_->recOverrun; }
double AudioEngine::inputDbfs() const { return impl_->inputDbfs(); }
double AudioEngine::outputDbfs() const { return impl_->outputDbfs(); }
bool AudioEngine::clipLatched() const { return impl_->clip(); }
void AudioEngine::resetClip() { impl_->resetClip(); }
unsigned AudioEngine::recordSeconds() const { return impl_->seconds(); }
double AudioEngine::playbackProgress() const { return impl_->progress(); }
std::string AudioEngine::lastError() const { return impl_->lastError(); }
