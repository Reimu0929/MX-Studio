#include "msx/wav.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace msx {

namespace {

void putLe16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void putLe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

bool readExact(IByteReader& in, uint8_t* dst, std::size_t bytes) { return in.read(dst, bytes) == bytes; }

}  // namespace

bool WavWriter::open(IByteWriter& out, const WavFormat& format, Diagnostics& diags, uint64_t maxDataBytes) {
    if (format.sampleRate == 0 || (format.channels != 1 && format.channels != 2) ||
        format.bitsPerSample != 16) {
        diags.error("WAV 出力形式は 16 bit PCM、モノラルまたはステレオである必要があります");
        return false;
    }
    out_ = &out;
    format_ = format;
    maxDataBytes_ = maxDataBytes;
    dataBytes_ = 0;
    framesWritten_ = 0;
    sizeLimitReached_ = false;
    sealed_ = false;

    uint8_t header[44];
    std::memset(header, 0, sizeof(header));
    std::memcpy(header + 0, "RIFF", 4);
    putLe32(header + 4, 36);
    std::memcpy(header + 8, "WAVE", 4);
    std::memcpy(header + 12, "fmt ", 4);
    putLe32(header + 16, 16);
    putLe16(header + 20, 1);
    putLe16(header + 22, static_cast<uint16_t>(format.channels));
    putLe32(header + 24, format.sampleRate);
    putLe32(header + 28, format.byteRate());
    putLe16(header + 32, static_cast<uint16_t>(format.blockAlign()));
    putLe16(header + 34, 16);
    std::memcpy(header + 36, "data", 4);
    putLe32(header + 40, 0);
    if (!out_->write(header, sizeof(header))) {
        diags.error("WAV ヘッダーを書き込めません");
        out_ = nullptr;
        return false;
    }
    open_ = true;
    return true;
}

bool WavWriter::writeFrames(const int16_t* interleaved, std::size_t frames, Diagnostics& diags) {
    if (!open_ || sealed_) return false;
    if (frames == 0) return true;
    const std::size_t bytes = frames * format_.blockAlign();
    if (dataBytes_ + bytes > maxDataBytes_) {
        const uint64_t room = maxDataBytes_ - dataBytes_;
        const std::size_t allowedFrames = static_cast<std::size_t>(room / format_.blockAlign());
        if (allowedFrames == 0) {
            sizeLimitReached_ = true;
            diags.error("WAV ファイルのサイズ上限に達したため、書き込みを打ち切りました");
            return false;
        }
        Bytes tmp(bytes);
        std::memcpy(&tmp[0], interleaved, bytes);
        const std::size_t allowedBytes = allowedFrames * format_.blockAlign();
        if (!out_->write(&tmp[0], allowedBytes)) {
            diags.error("WAV データを書き込めません（ディスク容量または書き込みエラー）");
            return false;
        }
        dataBytes_ += allowedBytes;
        framesWritten_ += allowedFrames;
        sizeLimitReached_ = true;
        diags.error("WAV ファイルのサイズ上限に達したため、書き込みを打ち切りました");
        return false;
    }
    Bytes tmp(bytes);
    std::memcpy(&tmp[0], interleaved, bytes);
    if (!out_->write(&tmp[0], tmp.size())) {
        diags.error("WAV データを書き込めません（ディスク容量または書き込みエラー）");
        return false;
    }
    dataBytes_ += bytes;
    framesWritten_ += frames;
    return true;
}

bool WavWriter::close(Diagnostics& diags) {
    if (!open_) return false;
    if (sealed_) return true;
    sealed_ = true;
    bool ok = true;
    if (dataBytes_ > 0xFFFFFFFFULL - 36ULL) {
        diags.error("WAV データが RIFF の 4 GiB 制限を超えています");
        ok = false;
    } else {
        uint8_t sizes[8];
        putLe32(sizes, static_cast<uint32_t>(36ULL + dataBytes_));
        putLe32(sizes + 4, static_cast<uint32_t>(dataBytes_));
        const uint64_t end = out_->position();
        if (!out_->seek(4)) {
            diags.error("WAV ヘッダーのサイズを更新できません（シーク不能）");
            ok = false;
        } else if (!out_->write(sizes, 4)) {
            diags.error("WAV ヘッダーのサイズを更新できません");
            ok = false;
        }
        if (ok) {
            if (!out_->seek(40) || !out_->write(sizes + 4, 4)) {
                diags.error("WAV ヘッダーのデータサイズを更新できません");
                ok = false;
            }
        }
        if (!out_->seek(end) || !out_->flush()) {
            diags.error("WAV ファイルの後処理に失敗しました");
            ok = false;
        }
    }
    open_ = false;
    return ok;
}

bool WavReader::open(IByteReader& in, Diagnostics& diags) {
    in_ = &in;
    fileSize_ = in.size();
    if (!in.seek(0)) {
        diags.error("WAV ファイルを先頭から読めません");
        return false;
    }
    uint8_t hdr[12];
    if (!readExact(in, hdr, sizeof(hdr))) {
        diags.error("WAV ヘッダーが短すぎます");
        return false;
    }
    if (std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        diags.error("RIFF/WAVE 形式ではありません");
        return false;
    }
    const uint32_t riffSize = readLe32(hdr + 4);
    if (riffSize != 0 && static_cast<uint64_t>(riffSize) + 8 > fileSize_) {
        diags.warning("RIFF サイズが実際のファイル長を超えています（途中切れの可能性）");
    }

    bool haveFmt = false;
    bool haveData = false;
    uint64_t pos = 12;
    while (pos + 8 <= fileSize_) {
        if (!in.seek(pos)) break;
        uint8_t chunk[8];
        if (!readExact(in, chunk, sizeof(chunk))) break;
        const uint32_t size = readLe32(chunk + 4);
        const uint64_t body = pos + 8;
        const bool fits = body + size <= fileSize_;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (!fits || size < 16) {
                diags.error("fmt チャンクが不正または途中で切れています");
                return false;
            }
            uint8_t fmt[40];
            const std::size_t toRead = std::min<std::size_t>(size, sizeof(fmt));
            if (!readExact(in, fmt, toRead)) {
                diags.error("fmt チャンクを読めません");
                return false;
            }
            const uint16_t tag = readLe16(fmt + 0);
            const uint16_t channels = readLe16(fmt + 2);
            const uint32_t rate = readLe32(fmt + 4);
            const uint16_t blockAlign = readLe16(fmt + 12);
            const uint16_t bits = readLe16(fmt + 14);
            if (tag != 1) {
                if (tag == 0xFFFE && size >= 40) {
                    static const uint8_t pcmGuid[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                                        0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
                    if (std::memcmp(fmt + 24, pcmGuid, sizeof(pcmGuid)) != 0) {
                        diags.error("WAVE_FORMAT_EXTENSIBLE のサブフォーマットが PCM ではありません");
                        return false;
                    }
                } else {
                    diags.error("PCM 以外の WAV 形式には対応していません（形式タグ " +
                                std::to_string(tag) + "）");
                    return false;
                }
            }
            if (channels != 1 && channels != 2) {
                diags.error("WAV のチャンネル数はモノラルまたはステレオのみ対応しています");
                return false;
            }
            if (bits != 16) {
                diags.error("WAV のビット深度は 16 bit のみ対応しています");
                return false;
            }
            if (rate != 48000) {
                diags.error("WAV のサンプリング周波数は 48,000 Hz のみ対応しています（" +
                            std::to_string(rate) + " Hz が指定されています）");
                return false;
            }
            if (blockAlign != channels * 2) {
                diags.error("WAV のブロック境界が 16 bit PCM と一致しません");
                return false;
            }
            format_.sampleRate = rate;
            format_.channels = channels;
            format_.bitsPerSample = 16;
            haveFmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            dataOffset_ = body;
            dataBytes_ = fits ? size : (fileSize_ > body ? fileSize_ - body : 0);
            if (!fits) {
                diags.warning("data チャンクが宣言サイズより短いため、実際の長さで復号します（途中切れ）");
                truncated_ = true;
            }
            haveData = true;
            break;
        } else {
            // Unknown chunk: skip it.  Odd-sized chunks are padded to even.
            const uint64_t advance = static_cast<uint64_t>(size) + (size & 1u);
            if (body + advance < body) break;
            pos = body + advance;
            continue;
        }
        const uint64_t advance = static_cast<uint64_t>(size) + (size & 1u);
        pos = body + advance;
    }
    if (!haveFmt) {
        diags.error("WAV に fmt チャンクがありません");
        return false;
    }
    if (!haveData) {
        diags.error("WAV に data チャンクがありません");
        return false;
    }
    if (dataBytes_ % format_.blockAlign() != 0) {
        diags.warning("data のバイト数がフレーム境界と一致しません（末尾を切り捨てます）");
        dataBytes_ -= dataBytes_ % format_.blockAlign();
        truncated_ = true;
    }
    dataFrames_ = dataBytes_ / format_.blockAlign();
    if (!in.seek(dataOffset_)) {
        diags.error("WAV の data チャンクへシークできません");
        return false;
    }
    open_ = true;
    return true;
}

std::size_t WavReader::readFrames(int16_t* interleaved, std::size_t frames, Diagnostics& diags) {
    if (!open_ || finished_ || frames == 0) return 0;
    const uint64_t remaining = dataFrames_ - framesRead_;
    const std::size_t want = static_cast<std::size_t>(std::min<uint64_t>(frames, remaining));
    if (want == 0) {
        finished_ = true;
        return 0;
    }
    Bytes raw(want * format_.blockAlign());
    const std::size_t got = in_->read(&raw[0], raw.size());
    const std::size_t complete = got / format_.blockAlign();
    for (std::size_t i = 0; i < complete * format_.channels; ++i) {
        const uint8_t* p = &raw[i * 2];
        interleaved[i] = static_cast<int16_t>(static_cast<uint16_t>(p[0] | (p[1] << 8)));
    }
    framesRead_ += complete;
    if (complete < want) {
        truncated_ = true;
        finished_ = true;
        diags.warning("WAV の data が宣言より短く、読み込みが途中で終わりました");
    }
    if (framesRead_ >= dataFrames_) finished_ = true;
    return complete;
}

bool normalizeWav(IByteReader& in, IByteWriter& out, const NormalizeOptions& options,
                  Diagnostics& diags, NormalizeStats* stats) {
    if (!in.seek(0)) {
        diags.error("WAV を先頭から読めません");
        return false;
    }
    WavReader probe;
    if (!probe.open(in, diags)) return false;
    const WavFormat format = probe.format();
    const uint64_t totalFrames = probe.dataFrames();
    if (totalFrames == 0) {
        diags.error("WAV に音声データがありません");
        return false;
    }

    std::vector<int16_t> buffer(static_cast<std::size_t>(4096 * format.channels));
    int inputPeak = 0;
    uint64_t done = 0;
    while (done < totalFrames) {
        const std::size_t n = probe.readFrames(&buffer[0], 4096, diags);
        if (n == 0) break;
        const std::size_t samples = n * format.channels;
        for (std::size_t i = 0; i < samples; ++i) {
            const int v = buffer[i] < 0 ? -static_cast<int>(buffer[i]) : static_cast<int>(buffer[i]);
            if (v > inputPeak) inputPeak = v;
        }
        done += n;
    }
    if (inputPeak == 0) {
        diags.error("無音の WAV は正規化できません");
        return false;
    }

    const double targetPeak = std::min(32767.0, 32767.0 * std::pow(10.0, options.targetDbfs / 20.0));
    const double maxGain = std::max(0.0, std::pow(10.0, options.maxGainDb / 20.0));
    double gain = targetPeak / static_cast<double>(inputPeak);
    bool capped = false;
    if (gain > maxGain) {
        gain = maxGain;
        capped = true;
    }
    if (gain <= 0.0) {
        diags.error("正規化ゲインを計算できません");
        return false;
    }

    if (!in.seek(0)) {
        diags.error("WAV を先頭から読み直せません");
        return false;
    }
    WavReader reader;
    if (!reader.open(in, diags)) return false;
    WavWriter writer;
    if (!writer.open(out, format, diags)) return false;

    int outputPeak = 0;
    uint64_t written = 0;
    while (written < totalFrames) {
        const std::size_t n = reader.readFrames(&buffer[0], 4096, diags);
        if (n == 0) break;
        const std::size_t samples = n * format.channels;
        for (std::size_t i = 0; i < samples; ++i) {
            long value = std::lround(static_cast<double>(buffer[i]) * gain);
            if (value > 32767) value = 32767;
            if (value < -32768) value = -32768;
            buffer[i] = static_cast<int16_t>(value);
            const int v = buffer[i] < 0 ? -static_cast<int>(buffer[i]) : static_cast<int>(buffer[i]);
            if (v > outputPeak) outputPeak = v;
        }
        if (!writer.writeFrames(&buffer[0], n, diags)) return false;
        written += n;
    }
    if (!writer.close(diags)) return false;

    if (stats) {
        stats->inputPeak = inputPeak;
        stats->outputPeak = outputPeak;
        stats->gainDb = 20.0 * std::log10(gain);
        stats->gainCapped = capped;
        stats->frames = written;
    }
    if (capped) {
        char message[160];
        std::snprintf(message, sizeof(message),
                      "最大ゲイン（+%.1f dB）に達したため、目標レベルまで上がっていません",
                      options.maxGainDb);
        diags.warning(message);
    }
    return true;
}

bool decodeWavToSamples(const Bytes& wav, WavFormat& format, Bytes& pcm, Diagnostics& diags) {
    MemoryReader reader(wav);
    WavReader wr;
    if (!wr.open(reader, diags)) return false;
    format = wr.format();
    const std::size_t totalFrames = static_cast<std::size_t>(wr.dataFrames());
    std::vector<int16_t> samples(totalFrames * format.channels, 0);
    std::size_t done = 0;
    while (done < totalFrames) {
        const std::size_t n = wr.readFrames(&samples[done * format.channels], totalFrames - done, diags);
        if (n == 0) break;
        done += n;
    }
    samples.resize(done * format.channels);
    pcm.resize(samples.size() * sizeof(int16_t));
    if (!samples.empty()) std::memcpy(&pcm[0], &samples[0], pcm.size());
    return !diags.hasErrors() && !pcm.empty();
}

}  // namespace msx
