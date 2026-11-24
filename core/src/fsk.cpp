#include "msx/tape.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "msx/wav.h"

namespace msx {

namespace {

const unsigned kSampleRate = 48000;
const int kLongLeaderSamples = 320000;   // 6.667 s
const int kShortLeaderSamples = 80000;   // 1.667 s
const std::size_t kBufferFrames = 4096;

class SampleBuffer {
public:
    SampleBuffer(IByteWriter& out, Diagnostics& diags)
        : out_(out), diags_(diags) {}

    bool begin(int amplitude) {
        amplitude_ = amplitude;
        WavFormat fmt;
        fmt.sampleRate = kSampleRate;
        fmt.channels = 1;
        fmt.bitsPerSample = 16;
        if (!wav_.open(out_, fmt, diags_)) return false;
        buf_.reserve(kBufferFrames * 2);
        return true;
    }

    bool high(std::uint64_t samples) { return level(samples, amplitude_); }
    bool low(std::uint64_t samples) { return level(samples, -amplitude_); }
    bool silence(std::uint64_t samples) { return level(samples, 0); }

    bool finish() {
        if (!flush()) return false;
        return wav_.close(diags_);
    }

    std::uint64_t frames() const { return frames_; }

private:
    bool level(std::uint64_t samples, int value) {
        for (std::uint64_t i = 0; i < samples; ++i) {
            buf_.push_back(static_cast<int16_t>(value));
            ++frames_;
            if (buf_.size() >= kBufferFrames * 8) {
                if (!flush()) return false;
            }
        }
        return true;
    }

    bool flush() {
        if (buf_.empty()) return true;
        const std::size_t n = buf_.size();  // mono frames
        const bool ok = wav_.writeFrames(&buf_[0], n, diags_);
        buf_.clear();
        return ok;
    }

    IByteWriter& out_;
    Diagnostics& diags_;
    WavWriter wav_;
    int amplitude_ = 14745;
    std::vector<int16_t> buf_;
    std::uint64_t frames_ = 0;
};

bool writeTone(SampleBuffer& sink, std::uint64_t samples, unsigned frequency) {
    const unsigned half = kSampleRate / (frequency * 2);
    if (half == 0) return false;
    std::uint64_t pos = 0;
    bool up = true;
    while (pos < samples) {
        const std::uint64_t n = std::min<std::uint64_t>(half, samples - pos);
        if (up) {
            if (!sink.high(n)) return false;
        } else {
            if (!sink.low(n)) return false;
        }
        up = !up;
        pos += n;
    }
    return true;
}

bool writeByte(SampleBuffer& sink, int baud, std::uint8_t value) {
    const unsigned bit = kSampleRate / static_cast<unsigned>(baud);
    const unsigned quarter = bit / 4;
    const unsigned halfBit = bit / 2;
    // start bit 0
    if (!sink.high(halfBit) || !sink.low(halfBit)) return false;
    for (int i = 0; i < 8; ++i) {
        if ((value >> i) & 1) {
            if (!sink.high(quarter) || !sink.low(quarter) || !sink.high(quarter) || !sink.low(quarter)) {
                return false;
            }
        } else {
            if (!sink.high(halfBit) || !sink.low(halfBit)) return false;
        }
    }
    for (int stop = 0; stop < 2; ++stop) {  // two 1 stop bits (2 cycles each)
        if (!sink.high(quarter) || !sink.low(quarter) || !sink.high(quarter) || !sink.low(quarter)) {
            return false;
        }
    }
    return true;
}

bool similarHalf(std::uint64_t a, std::uint64_t b) {
    if (a == 0 || b == 0) return false;
    const std::uint64_t diff = a > b ? a - b : b - a;
    const std::uint64_t base = std::max<std::uint64_t>(a, b);
    return diff * 100 <= base * 22;  // within 22%
}

class Demodulator {
public:
    Demodulator(const DecodeOptions& options, Diagnostics& diags) : options_(options), diags_(diags) {}

    void begin(unsigned channels, unsigned sampleRate) {
        channels_ = channels;
        sampleRate_ = sampleRate;
        channel_ = options_.channel;
        if (channel_ < 0) channel_ = 0;
        if (channel_ >= static_cast<int>(channels_)) channel_ = static_cast<int>(channels_) - 1;
        gapSamples_ = static_cast<std::uint64_t>(options_.gapSeconds * sampleRate_);
        if (gapSamples_ < 200) gapSamples_ = 200;
        if (options_.baudOverride != 0 && options_.baudOverride != 1200 && options_.baudOverride != 2400) {
            diags_.error("手動ボーレートは 1200 または 2400 を指定してください");
            manualInvalid_ = true;
        }
    }

    void feed(const int16_t* frames, std::size_t count) {
        for (std::size_t f = 0; f < count; ++f) {
            const int16_t v = frames[f * channels_ + static_cast<std::size_t>(channel_)];
            processSample(v, sampleIndex_);
            ++sampleIndex_;
        }
    }

    void finish() {
        if (decoding_) {
            completeTail();
            flushBlock(sampleIndex_);
        }
        if (result_.blocks.empty()) {
            if (!manualInvalid_) {
                diags_.error("1200/2400 ボーのリーダーを検出できませんでした（MSX カセット信号ではない可能性）");
            }
        } else {
            std::map<int, int> tally;
            for (std::size_t i = 0; i < result_.blocks.size(); ++i) ++tally[result_.blocks[i].baud];
            int best = 0;
            int bestCount = 0;
            for (std::map<int, int>::const_iterator it = tally.begin(); it != tally.end(); ++it) {
                if (it->second > bestCount) {
                    best = it->first;
                    bestCount = it->second;
                }
            }
            result_.baud = best;
        }
    }

    const DecodeResult& result() const { return result_; }

private:
    void processSample(int16_t raw, std::uint64_t index) {
        const double x = static_cast<double>(raw) / 32768.0;
        const double y = x - prevX_ + 0.995 * prevY_;
        prevX_ = x;
        prevY_ = y;
        const double mag = std::fabs(y);
        env_ = std::max(mag, env_ * 0.9995);
        const double threshold = std::max(env_ * 0.20, 0.0035);
        if (sign_ == 0) {
            if (y > threshold) {
                sign_ = 1;
                lastEdge_ = index;
                haveEdge_ = true;
            } else if (y < -threshold) {
                sign_ = -1;
                lastEdge_ = index;
                haveEdge_ = true;
            }
            return;
        }
        if (sign_ < 0 && y >= threshold) {
            sign_ = 1;
            onEdge(index);
        } else if (sign_ > 0 && y <= -threshold) {
            sign_ = -1;
            onEdge(index);
        }
    }

    void onEdge(std::uint64_t index) {
        if (!haveEdge_) {
            haveEdge_ = true;
            lastEdge_ = index;
            return;
        }
        const std::uint64_t interval = index - lastEdge_;
        lastEdge_ = index;
        if (interval < 2) return;  // contact bounce / noise
        onInterval(interval, index);
    }

    void onInterval(std::uint64_t interval, std::uint64_t edgeIndex) {

        if (interval > gapSamples_) {
            if (decoding_) {
                completeTail();
                flushBlock(edgeIndex - interval);
            }
            runValid_ = false;
            inLeader_ = false;
            decoding_ = false;
            sign_ = 0;
            env_ *= 0.5;
            return;
        }

        if (options_.baudOverride != 0 && !manualInvalid_) {
            if (!decoding_) {
                startData(static_cast<double>(kSampleRate) / options_.baudOverride / 2.0, edgeIndex, true);
                feedHalf(interval);
            } else {
                feedHalf(interval);
            }
            return;
        }

        bool matched = false;
        if (!runValid_ || similarHalf(interval, runHalf_)) {
            if (!runValid_) {
                runHalf_ = interval;
                runSum_ = interval;
                runCount_ = 1;
                runValid_ = true;
            } else {
                runSum_ += interval;
                ++runCount_;
            }
            matched = true;
        } else {
            prevRunHalf_ = runCount_ != 0 ? static_cast<double>(runSum_) / runCount_ : static_cast<double>(runHalf_);
            prevRunCount_ = runCount_;
            runHalf_ = interval;
            runSum_ = interval;
            runCount_ = 1;
        }

        if (inLeader_) {
            if (!matched) {
                startData(prevRunHalf_, edgeIndex, false);
                feedHalf(interval);
            }
            return;
        }

        if (!decoding_) {
            if (runValid_ && runCount_ >= 60 && runHalf_ >= 3 && runHalf_ <= 40) {
                inLeader_ = true;
                leaderHalf_ = static_cast<double>(runSum_) / runCount_;
                leaderStart_ = edgeIndex - runSum_;
                return;
            }
            return;
        }

        // A fresh leader after data (short header of the next block) starts a
        // new block.  Data cannot contain 80 identical intervals in a row.
        if (runCount_ >= 80 && matched) {
            flushBlock(edgeIndex - runSum_);
            inLeader_ = true;
            leaderHalf_ = static_cast<double>(runSum_) / runCount_;
            leaderStart_ = edgeIndex - runSum_;
            return;
        }
        feedHalf(interval);
    }

    void startData(double leaderHalf, std::uint64_t edgeIndex, bool manual) {
        leaderHalf_ = std::max(1.0, leaderHalf);
        int baud = options_.baudOverride;
        if (baud == 0) {
            baud = leaderHalf <= 7.5 ? 2400 : 1200;
        }
        bitPeriod_ = manual ? (static_cast<double>(kSampleRate) / baud)
                            : (leaderHalf * 4.0);
        unit_ = bitPeriod_ / 4.0;
        if (unit_ < 1.0) unit_ = 1.0;
        decoding_ = true;
        inLeader_ = false;
        blockBaud_ = baud;
        blockStart_ = manual ? (edgeIndex - static_cast<std::uint64_t>(bitPeriod_)) : leaderStart_;
        dataStart_ = edgeIndex;
        leaderSeconds_ = static_cast<double>(dataStart_ - blockStart_) / kSampleRate;
        bits_.clear();
        current_.clear();
        acc_ = 0;
        hadLong_ = false;
        frameErrors_ = 0;
        garbageBits_ = 0;
    }

    void feedHalf(std::uint64_t interval) {
        if (!decoding_) return;
        int q = static_cast<int>(std::lround(static_cast<double>(interval) / unit_));
        if (q < 1) q = 1;
        if (q > 8) q = 8;
        if (q >= 3) ++frameErrors_;
        if (q >= 2) hadLong_ = true;
        acc_ += q;
        while (acc_ >= 4) {
            const int bit = hadLong_ ? 0 : 1;
            if (acc_ > 4) ++frameErrors_;
            pushBit(bit);
            acc_ = 0;
            hadLong_ = false;
        }
    }

    void pushBit(int bit) {
        bits_.push_back(bit);
        while (bits_.size() >= 11) {
            if (bits_[0] == 0 && bits_[9] == 1 && bits_[10] == 1) {
                std::uint8_t value = 0;
                for (int i = 0; i < 8; ++i) {
                    if (bits_[static_cast<std::size_t>(i + 1)]) value |= static_cast<std::uint8_t>(1u << i);
                }
                current_.push_back(value);
                bits_.erase(bits_.begin(), bits_.begin() + 11);
            } else {
                if (bits_[0] == 0 && frameErrors_ < 500) ++frameErrors_;
                bits_.erase(bits_.begin());
                ++garbageBits_;
            }
        }
    }

    void completeTail() {
        if (!decoding_ || acc_ == 0) return;
        // The final low half of the last bit is not terminated by an edge when
        // the signal falls into silence.  Complete it using the same half
        // length as the preceding half (a stop bit is a 1 bit).
        acc_ += hadLong_ ? 2 : 1;
        if (acc_ >= 4) {
            if (acc_ > 4 && frameErrors_ < 500) ++frameErrors_;
            const int bit = hadLong_ ? 0 : 1;
            pushBit(bit);
            acc_ = 0;
            hadLong_ = false;
        }
    }

    void flushBlock(std::uint64_t endSample) {
        if (!decoding_) return;
        decoding_ = false;
        if (acc_ != 0 || !bits_.empty()) {
            bool allOnes = true;
            for (std::size_t i = 0; i < bits_.size(); ++i) {
                if (bits_[i] != 1) {
                    allOnes = false;
                    break;
                }
            }
            if (!allOnes || hadLong_) result_.clean = false;
        }
        if (!current_.empty()) {
            DecodedBlock block;
            block.bytes = current_;
            block.baud = blockBaud_;
            block.startSample = blockStart_;
            block.endSample = endSample;
            block.frameErrors = frameErrors_;
            block.garbageBits = garbageBits_;
            block.leaderSeconds = leaderSeconds_;
            block.manualBaud = options_.baudOverride != 0;
            const TapeFileType type = tapeTypeFromHeader(block.bytes);
            if (type != TapeFileType::Unknown) {
                block.header = true;
                block.type = type;
                currentType_ = type;
            } else {
                block.type = currentType_;
            }
            if (block.frameErrors > 0) {
                result_.clean = false;
                diags_.addAtSample(Severity::Warning, block.startSample,
                                   "フレーム同期エラーが " + std::to_string(block.frameErrors) +
                                       " 回ありました（推定 " + std::to_string(block.baud) + " ボー）");
            }
            if (!block.header && block.type == TapeFileType::Unknown) {
                diags_.addAtSample(Severity::Warning, block.startSample,
                                   "ヘッダーのないデータブロックを復号しました（" +
                                       std::to_string(block.bytes.size()) + " バイト）");
            }
            result_.blocks.push_back(block);
        }
        current_.clear();
        bits_.clear();
        acc_ = 0;
        hadLong_ = false;
        frameErrors_ = 0;
        garbageBits_ = 0;
        leaderSeconds_ = 0;
    }

    DecodeOptions options_;
    Diagnostics& diags_;
    DecodeResult result_;
    unsigned channels_ = 1;
    unsigned sampleRate_ = kSampleRate;
    int channel_ = 0;
    std::uint64_t gapSamples_ = 2400;
    std::uint64_t sampleIndex_ = 0;
    double prevX_ = 0.0;
    double prevY_ = 0.0;
    double env_ = 0.0;
    int sign_ = 0;
    bool haveEdge_ = false;
    std::uint64_t lastEdge_ = 0;

    bool runValid_ = false;
    std::uint64_t runHalf_ = 0;
    std::uint64_t runSum_ = 0;
    int runCount_ = 0;
    double prevRunHalf_ = 0.0;
    int prevRunCount_ = 0;

    bool inLeader_ = false;
    double leaderHalf_ = 0.0;
    std::uint64_t leaderStart_ = 0;

    bool decoding_ = false;
    int blockBaud_ = 0;
    double bitPeriod_ = 0.0;
    double unit_ = 1.0;
    int acc_ = 0;
    bool hadLong_ = false;
    int frameErrors_ = 0;
    int garbageBits_ = 0;
    std::uint64_t blockStart_ = 0;
    std::uint64_t dataStart_ = 0;
    double leaderSeconds_ = 0.0;
    std::vector<int> bits_;
    Bytes current_;
    TapeFileType currentType_ = TapeFileType::Unknown;
    bool manualInvalid_ = false;
};

}  // namespace

bool renderTapeToWav(const CasImage& image, IByteWriter& out, const FskOptions& options, Diagnostics& diags,
                     FskStats* stats) {
    if (options.baud != 1200 && options.baud != 2400) {
        diags.error("ボーレートは 1200 または 2400 を指定してください");
        return false;
    }
    if (image.blocks.empty()) {
        diags.error("テープイメージにブロックがありません");
        return false;
    }
    SampleBuffer sink(out, diags);
    if (!sink.begin(options.amplitude)) return false;

    const std::uint64_t silence = static_cast<std::uint64_t>(options.silenceSeconds * kSampleRate);
    const std::uint64_t tail = static_cast<std::uint64_t>(options.tailSilenceSeconds * kSampleRate);
    for (std::size_t i = 0; i < image.blocks.size(); ++i) {
        const TapeBlock& block = image.blocks[i];
        if (!sink.silence(silence)) return false;
        const std::uint64_t leader =
            block.header ? static_cast<std::uint64_t>(kLongLeaderSamples) : static_cast<std::uint64_t>(kShortLeaderSamples);
        if (!writeTone(sink, leader, static_cast<unsigned>(options.baud * 2))) return false;
        for (std::size_t b = 0; b < block.bytes.size(); ++b) {
            if (!writeByte(sink, options.baud, block.bytes[b])) return false;
        }
        if (stats) {
            ++stats->blocks;
            stats->bytes += block.bytes.size();
        }
    }
    if (!sink.silence(tail)) return false;
    if (!sink.finish()) return false;
    if (stats) stats->samples = sink.frames();
    return true;
}

DecodeResult demodulateWav(IByteReader& in, const DecodeOptions& options, Diagnostics& diags) {
    DecodeResult empty;
    WavReader reader;
    if (!reader.open(in, diags)) return empty;
    WavFormat fmt = reader.format();
    if (fmt.sampleRate != kSampleRate) {
        diags.error("復号は 48,000 Hz の WAV のみ対応しています");
        return empty;
    }
    if (options.channel >= static_cast<int>(fmt.channels)) {
        diags.error("指定されたチャンネルが WAV に存在しません");
        return empty;
    }
    Demodulator demod(options, diags);
    demod.begin(fmt.channels, fmt.sampleRate);

    std::vector<int16_t> frames(kBufferFrames * fmt.channels);
    while (true) {
        const std::size_t got = reader.readFrames(&frames[0], kBufferFrames, diags);
        if (got == 0) break;
        demod.feed(&frames[0], got);
    }
    demod.finish();
    if (reader.truncated()) {
        diags.warning("WAV が途中で切れているため、復号結果が不完全です");
        // demodulateWav returns by value; set the flag on the result below.
    }
    if (reader.framesRead() == 0) {
        diags.error("WAV に音声データがありません");
    }
    DecodeResult result = demod.result();
    if (reader.truncated()) result.clean = false;
    return result;
}

CasImage decodedToCas(const DecodeResult& result, Diagnostics& diags) {
    CasImage image;
    TapeFileType current = TapeFileType::Unknown;
    for (std::size_t i = 0; i < result.blocks.size(); ++i) {
        const DecodedBlock& decoded = result.blocks[i];
        TapeBlock block;
        block.bytes = decoded.bytes;
        const TapeFileType type = tapeTypeFromHeader(block.bytes);
        if (type != TapeFileType::Unknown) {
            block.header = true;
            block.type = type;
            current = type;
            image.blocks.push_back(block);
            CasFileInfo file;
            file.type = type;
            file.name = block.bytes.size() >= 16
                            ? std::string(reinterpret_cast<const char*>(&block.bytes[10]), 6)
                            : std::string();
            file.headerBlock = image.blocks.size() - 1;
            file.valid = true;
            image.files.push_back(file);
        } else {
            block.type = current;
            image.blocks.push_back(block);
            if (!image.files.empty()) {
                CasFileInfo& file = image.files.back();
                file.dataBlocks.push_back(image.blocks.size() - 1);
                if (file.type == TapeFileType::Ascii) {
                    file.payload.insert(file.payload.end(), block.bytes.begin(), block.bytes.end());
                } else if (file.payload.empty()) {
                    file.payload = block.bytes;
                }
            } else {
                diags.warning("復号したテープの先頭がヘッダーブロックではありません");
            }
        }
    }
    for (std::size_t i = 0; i < image.files.size(); ++i) {
        CasFileInfo& file = image.files[i];
        if (file.type == TapeFileType::Binary && file.payload.size() >= 6) {
            file.start = readLe16(&file.payload[0]);
            file.end = readLe16(&file.payload[2]);
            file.exec = readLe16(&file.payload[4]);
        }
    }
    return image;
}

}  // namespace msx
