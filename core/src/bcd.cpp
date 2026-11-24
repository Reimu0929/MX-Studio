#include "msx/bcd.h"

#include <algorithm>
#include <cctype>

namespace msx {
namespace bcd {

int byteCount(int digits) { return 1 + (digits + 1) / 2; }

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

void packDigits(const std::string& digits, Bytes& out) {
    for (std::size_t i = 0; i < digits.size(); i += 2) {
        const int hi = digits[i] - '0';
        const int lo = (i + 1 < digits.size()) ? digits[i + 1] - '0' : 0;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
}

std::string unpackDigits(const uint8_t* data, int digits, bool& valid) {
    std::string out;
    valid = true;
    for (int i = 0; i < digits; ++i) {
        const uint8_t byte = data[1 + i / 2];
        const int nibble = (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
        if (nibble > 9) valid = false;
        out.push_back(static_cast<char>('0' + (nibble > 9 ? 0 : nibble)));
    }
    return out;
}

}  // namespace

EncodeResult encode(const std::string& literal, int digits, Bytes& out) {
    EncodeResult result;
    std::string mantissa;
    bool seenPoint = false;
    int pointPos = 0;
    std::size_t i = 0;

    while (i < literal.size() && isDigit(literal[i])) {
        mantissa.push_back(literal[i]);
        ++pointPos;
        ++i;
    }
    if (i < literal.size() && literal[i] == '.') {
        seenPoint = true;
        ++i;
        while (i < literal.size() && isDigit(literal[i])) {
            mantissa.push_back(literal[i]);
            ++i;
        }
    }
    if (mantissa.empty()) {
        result.message = "numeric literal has no digits";
        return result;
    }
    int exponent = 0;
    if (i < literal.size() && (literal[i] == 'E' || literal[i] == 'e')) {
        ++i;
        bool negative = false;
        if (i < literal.size() && (literal[i] == '+' || literal[i] == '-')) {
            negative = literal[i] == '-';
            ++i;
        }
        if (i >= literal.size() || !isDigit(literal[i])) {
            result.message = "invalid exponent";
            return result;
        }
        int value = 0;
        while (i < literal.size() && isDigit(literal[i])) {
            if (value < 10000) value = value * 10 + (literal[i] - '0');
            ++i;
        }
        exponent = negative ? -value : value;
    }
    if (i != literal.size()) {
        result.message = "unexpected character in numeric literal";
        return result;
    }
    (void)seenPoint;

    // Strip leading zeroes, adjusting the decimal exponent.
    std::size_t firstNonZero = 0;
    while (firstNonZero < mantissa.size() && mantissa[firstNonZero] == '0') ++firstNonZero;
    if (firstNonZero == mantissa.size()) {
        out.assign(static_cast<std::size_t>(byteCount(digits)), 0);
        result.ok = true;
        return result;
    }
    mantissa.erase(0, firstNonZero);
    pointPos -= static_cast<int>(firstNonZero);
    pointPos += exponent;

    if (static_cast<int>(mantissa.size()) > digits) {
        result.truncated = true;
        const bool roundUp = mantissa[static_cast<std::size_t>(digits)] >= '5';
        mantissa.resize(static_cast<std::size_t>(digits));
        if (roundUp) {
            int digitIndex = digits - 1;
            bool carried = true;
            for (; digitIndex >= 0; --digitIndex) {
                if (mantissa[static_cast<std::size_t>(digitIndex)] == '9') {
                    mantissa[static_cast<std::size_t>(digitIndex)] = '0';
                } else {
                    ++mantissa[static_cast<std::size_t>(digitIndex)];
                    carried = false;
                    break;
                }
            }
            if (carried) {
                mantissa.assign(1, '1');
                mantissa.append(static_cast<std::size_t>(digits - 1), '0');
                ++pointPos;
            }
        }
    }
    while (static_cast<int>(mantissa.size()) < digits) mantissa.push_back('0');

    if (pointPos > 62) {
        result.overflow = true;
        result.message = "exponent exceeds single/double precision range";
        return result;
    }
    if (pointPos < -63) {
        result.underflow = true;
        result.message = "exponent is below the representable range";
        return result;
    }

    out.clear();
    out.reserve(static_cast<std::size_t>(byteCount(digits)));
    out.push_back(static_cast<uint8_t>(pointPos + 64));
    packDigits(mantissa, out);
    result.ok = true;
    return result;
}

DecodeResult decodeDetailed(const uint8_t* data, int digits, bool addSuffix) {
    DecodeResult result;
    const uint8_t signExp = data[0];
    if (signExp == 0) {
        result.text = "0";
        if (addSuffix) result.text.push_back(digits == kSingleDigits ? '!' : '#');
        return result;
    }
    result.negative = (signExp & 0x80) != 0;
    const int exponent = static_cast<int>(signExp & 0x7F) - 64;
    std::string mantissa = unpackDigits(data, digits, result.validBcd);

    // Remove trailing zeroes; the mantissa is normalized by the ROM.
    while (mantissa.size() > 1 && mantissa[mantissa.size() - 1] == '0') mantissa.erase(mantissa.size() - 1);

    if (mantissa == "0") {
        result.text = "0";
        if (addSuffix) result.text.push_back(digits == kSingleDigits ? '!' : '#');
        return result;
    }

    const int exp10 = exponent;  // value = 0.<mantissa> * 10^exp10
    const int n = static_cast<int>(mantissa.size());
    std::string text;

    if (exp10 < -3 || exp10 > 9) {
        // Scientific notation: d.ddddE(+|-)n
        text.push_back(mantissa[0]);
        if (n > 1) {
            text.push_back('.');
            text += mantissa.substr(1);
        }
        const int e = exp10 - 1;
        text.push_back('E');
        if (e >= 0) {
            text.push_back('+');
        } else {
            text.push_back('-');
        }
        text += std::to_string(e >= 0 ? e : -e);
    } else if (exp10 >= n) {
        text = mantissa;
        text.append(static_cast<std::size_t>(exp10 - n), '0');
    } else if (exp10 > 0) {
        text = mantissa.substr(0, static_cast<std::size_t>(exp10));
        text.push_back('.');
        text += mantissa.substr(static_cast<std::size_t>(exp10));
    } else {
        text = "0.";
        text.append(static_cast<std::size_t>(-exp10), '0');
        text += mantissa;
    }

    if (result.negative) text.insert(text.begin(), '-');
    if (addSuffix) text.push_back(digits == kSingleDigits ? '!' : '#');
    result.text = text;
    return result;
}

std::string decode(const uint8_t* data, int digits, bool addSuffix) {
    return decodeDetailed(data, digits, addSuffix).text;
}

}  // namespace bcd
}  // namespace msx
