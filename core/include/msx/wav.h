#pragma once

#include <cstddef>
#include <cstdint>

#include "msx/common.h"

namespace msx {

struct WavFormat {
    unsigned sampleRate = 48000;
    unsigned channels = 1;
    unsigned bitsPerSample = 16;

    unsigned blockAlign() const { return channels * (bitsPerSample / 8); }
    unsigned byteRate() const { return sampleRate * blockAlign(); }
};

class WavWriter {
public:
    // 2 GiB minus a safety margin: a single WAV file is cut off cleanly before
    // the RIFF size fields become ambiguous.
    static const uint64_t kDefaultMaxDataBytes = 2147483648ULL - 65536ULL;

    bool open(IByteWriter& out, const WavFormat& format, Diagnostics& diags,
              uint64_t maxDataBytes = kDefaultMaxDataBytes);
    bool writeFrames(const int16_t* interleaved, std::size_t frames, Diagnostics& diags);
    bool close(Diagnostics& diags);

    bool isOpen() const { return open_; }
    bool sizeLimitReached() const { return sizeLimitReached_; }
    uint64_t framesWritten() const { return framesWritten_; }
    const WavFormat& format() const { return format_; }

private:
    IByteWriter* out_ = nullptr;
    WavFormat format_;
    bool open_ = false;
    bool sealed_ = false;
    bool sizeLimitReached_ = false;
    uint64_t dataBytes_ = 0;
    uint64_t framesWritten_ = 0;
    uint64_t maxDataBytes_ = kDefaultMaxDataBytes;
};

class WavReader {
public:
    bool open(IByteReader& in, Diagnostics& diags);
    std::size_t readFrames(int16_t* interleaved, std::size_t frames, Diagnostics& diags);

    const WavFormat& format() const { return format_; }
    bool isOpen() const { return open_; }
    bool truncated() const { return truncated_; }
    bool finished() const { return finished_; }
    uint64_t dataBytes() const { return dataBytes_; }
    uint64_t dataFrames() const { return dataFrames_; }
    uint64_t framesRead() const { return framesRead_; }

private:
    IByteReader* in_ = nullptr;
    WavFormat format_;
    bool open_ = false;
    bool truncated_ = false;
    bool finished_ = false;
    uint64_t dataOffset_ = 0;
    uint64_t dataBytes_ = 0;
    uint64_t dataFrames_ = 0;
    uint64_t framesRead_ = 0;
    uint64_t fileSize_ = 0;
};

// Reads a complete WAV file into memory (tests and small files).
bool decodeWavToSamples(const Bytes& wav, WavFormat& format, Bytes& pcm, Diagnostics& diags);

// Peak normalization for a separate analysis/decoding copy.  The input file
// itself is never modified; the caller writes a new WAV.  16 bit PCM only.
struct NormalizeOptions {
    double targetDbfs = -6.0;
    double maxGainDb = 12.0;
};

struct NormalizeStats {
    int inputPeak = 0;
    int outputPeak = 0;
    double gainDb = 0.0;
    bool gainCapped = false;
    uint64_t frames = 0;
};

bool normalizeWav(IByteReader& in, IByteWriter& out, const NormalizeOptions& options,
                  Diagnostics& diags, NormalizeStats* stats = nullptr);

}  // namespace msx
