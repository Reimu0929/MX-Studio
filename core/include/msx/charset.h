#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "msx/common.h"

namespace msx {
namespace charset {

// Japanese MSX video character set (openMSX MSXVIDJP.TXT, 2021-11-21).
// 0xFE/0xFF are not defined by that table; they are represented losslessly
// with private-use code points U+F0FE/U+F0FF.
std::string byteToUtf8(uint8_t value);
std::string decode(const uint8_t* data, std::size_t size);
std::string decode(const Bytes& data);

enum class EncodeStatus {
    Ok,
    UnknownCharacter,  // no MSX byte exists for this Unicode code point
    MalformedUtf8
};

struct EncodeError {
    EncodeStatus status = EncodeStatus::Ok;
    std::size_t byteOffset = 0;
    uint32_t codePoint = 0;
};

// Encodes UTF-8 text (typically the content of a string, REM or DATA item)
// to the Japanese MSX character set.  Unmappable characters are reported,
// never replaced by '?'.
EncodeStatus encode(const std::string& utf8, Bytes& out, EncodeError* error = nullptr);

bool isControlCode(uint8_t value);

}  // namespace charset
}  // namespace msx
