#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "msx/common.h"

namespace msx {

enum class TapeFileType : uint8_t {
    Unknown = 0x00,
    Basic = 0xD3,
    Binary = 0xD0,
    Ascii = 0xEA,
};

struct TapeBlock {
    Bytes bytes;
    bool header = false;
    TapeFileType type = TapeFileType::Unknown;
};

struct CasFileInfo {
    TapeFileType type = TapeFileType::Unknown;
    std::string name;      // 6 MSX bytes, space padded
    std::size_t headerBlock = 0;
    std::vector<std::size_t> dataBlocks;
    Bytes payload;         // data bytes (ASCII blocks concatenated)
    uint16_t start = 0;    // binary only
    uint16_t end = 0;
    uint16_t exec = 0;
    bool valid = false;
};

struct CasImage {
    std::vector<TapeBlock> blocks;
    std::vector<CasFileInfo> files;
};

const uint8_t* casMagic();  // 8 bytes: 1F A6 DE BA CC 13 7D 74
bool isCasMagic(const uint8_t* data);

TapeFileType tapeTypeFromHeader(const Bytes& payload);
std::string tapeTypeName(TapeFileType type);

// Encodes a file name to the 6-byte MSX field.  Longer names are an error,
// never silently truncated.
bool nameToMsx6(const std::string& utf8Name, std::string& msxName6, Diagnostics& diags);

CasImage parseCas(const Bytes& data, Diagnostics& diags);
Bytes serializeCas(const CasImage& image, bool alignToEight, Diagnostics& diags);

CasImage makeBasicTape(const Bytes& tapeBody, const std::string& name, Diagnostics& diags);
CasImage makeAsciiTape(const Bytes& msxAscii, const std::string& name, Diagnostics& diags);
CasImage makeBinaryTape(const Bytes& code, uint16_t start, uint16_t end, uint16_t exec,
                        const std::string& name, Diagnostics& diags);

// ---------------------------------------------------------------------------
// FSK modem (1200 / 2400 baud, 48 kHz, 16 bit PCM WAV in/out)
// ---------------------------------------------------------------------------
struct FskOptions {
    int baud = 1200;
    int amplitude = 14745;         // about -7 dBFS
    double silenceSeconds = 0.35;  // gap before each block
    double tailSilenceSeconds = 0.2;
};

struct FskStats {
    uint64_t samples = 0;
    uint64_t bytes = 0;
    uint64_t blocks = 0;
};

bool renderTapeToWav(const CasImage& image, IByteWriter& out, const FskOptions& options, Diagnostics& diags,
                     FskStats* stats = nullptr);

struct DecodeOptions {
    int channel = -1;       // -1: first channel; 0/1: explicit stereo channel
    int baudOverride = 0;   // 0 = automatic, otherwise 1200 or 2400
    double gapSeconds = 0.05;
};

struct DecodedBlock {
    Bytes bytes;
    int baud = 0;
    uint64_t startSample = 0;
    uint64_t endSample = 0;
    bool header = false;
    TapeFileType type = TapeFileType::Unknown;
    int frameErrors = 0;
    int garbageBits = 0;
    double leaderSeconds = 0.0;
    bool manualBaud = false;
};

struct DecodeResult {
    std::vector<DecodedBlock> blocks;
    int baud = 0;  // dominant detected baud, 0 if unknown
    bool clean = true;  // false when a frame error or truncated WAV was seen
};

DecodeResult demodulateWav(IByteReader& in, const DecodeOptions& options, Diagnostics& diags);
CasImage decodedToCas(const DecodeResult& result, Diagnostics& diags);

}  // namespace msx
