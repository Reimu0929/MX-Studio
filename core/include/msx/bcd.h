#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "msx/common.h"

namespace msx {
namespace bcd {

constexpr int kSingleDigits = 6;
constexpr int kDoubleDigits = 14;

int byteCount(int digits);

struct EncodeResult {
    bool ok = false;
    bool truncated = false;
    bool underflow = false;
    bool overflow = false;
    std::string message;
};

// Encodes the absolute value of an ASCII decimal literal ("0.1", "123.456",
// "1E+10", ".5").  The caller tokenizes the sign separately, as MSX does.
EncodeResult encode(const std::string& literal, int digits, Bytes& out);

// Returns the decimal text.  addSuffix appends '!' (6 digits) or '#' (14
// digits) so that re-tokenizing preserves the precision.
std::string decode(const uint8_t* data, int digits, bool addSuffix);

struct DecodeResult {
    std::string text;
    bool validBcd = true;
    bool negative = false;
};

DecodeResult decodeDetailed(const uint8_t* data, int digits, bool addSuffix);

}  // namespace bcd
}  // namespace msx
