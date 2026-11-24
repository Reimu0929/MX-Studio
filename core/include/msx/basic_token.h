#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace msx {

enum KeywordFlag : uint16_t {
    KF_None = 0,
    KF_Function = 1 << 0,
    KF_Statement = 1 << 1,
    KF_Operator = 1 << 2,
    KF_MSX2 = 1 << 3,
    KF_Disk = 1 << 4,
    KF_LineRef = 1 << 5,       // the following integer literal is a line reference
    KF_ThenElse = 1 << 6,      // line reference only for a bare integer at end of statement
    KF_PrependColon = 1 << 7,  // ELSE is stored as 3A A1
    KF_Comment = 1 << 8,       // REM
    KF_Data = 1 << 9,          // DATA
    KF_Paren = 1 << 10,        // text contains '(' (TAB(, SPC()
};

struct Keyword {
    const char* text;   // ASCII, upper case, e.g. "PRINT", "TAB("
    uint16_t code;      // 0x81-0xFF single byte, 0xFF00|low two bytes
    uint16_t flags;
};

const std::vector<Keyword>& keywords();

// Finds the longest keyword matching at position (case insensitive).
// Returns nullptr when no keyword matches.
const Keyword* matchKeyword(const std::string& text, std::size_t position);

// Returns nullptr for an unknown token code.
const Keyword* keywordByCode(uint16_t code);

// Text of a two byte token, for diagnostics ("FF86").
std::string tokenCodeText(uint16_t code);

}  // namespace msx
