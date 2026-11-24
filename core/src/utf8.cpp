#include "msx/utf8.h"

namespace msx {
namespace utf8 {

bool decode(const std::string& text, std::size_t& index, uint32_t& cp) {
    if (index >= text.size()) return false;
    const uint8_t b0 = static_cast<uint8_t>(text[index]);
    if (b0 < 0x80) {
        cp = b0;
        ++index;
        return true;
    }
    int need = 0;
    uint32_t value = 0;
    if ((b0 & 0xE0) == 0xC0) {
        need = 1;
        value = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        need = 2;
        value = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        need = 3;
        value = b0 & 0x07;
    } else {
        cp = 0xFFFD;
        ++index;
        return false;
    }
    if (index + static_cast<std::size_t>(need) >= text.size()) {
        cp = 0xFFFD;
        ++index;
        return false;
    }
    for (int i = 1; i <= need; ++i) {
        const uint8_t b = static_cast<uint8_t>(text[index + static_cast<std::size_t>(i)]);
        if ((b & 0xC0) != 0x80) {
            cp = 0xFFFD;
            ++index;
            return false;
        }
        value = (value << 6) | (b & 0x3F);
    }
    index += static_cast<std::size_t>(need) + 1;
    cp = value;
    return true;
}

void append(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::size_t codepointStart(const std::string& text, std::size_t byteOffset) {
    if (byteOffset >= text.size()) return text.size();
    while (byteOffset > 0 && (static_cast<uint8_t>(text[byteOffset]) & 0xC0) == 0x80) {
        --byteOffset;
    }
    return byteOffset;
}

bool stripBom(const std::string& text, std::string& out) {
    if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF &&
        static_cast<uint8_t>(text[1]) == 0xBB && static_cast<uint8_t>(text[2]) == 0xBF) {
        out = text.substr(3);
    } else {
        out = text;
    }
    return true;
}

}  // namespace utf8
}  // namespace msx
