#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "msx/common.h"

namespace msx {

struct ModelProfile;

enum class LexKind {
    LineNumber,
    Integer,
    Float,
    Hex,
    Octal,
    Binary,
    String,
    Comment,
    Data,
    Keyword,
    Operator,
    Variable,
    Separator,
    EndOfLine,
    Unknown
};

struct LexToken {
    LexKind kind = LexKind::Unknown;
    std::string text;
    uint16_t tokenCode = 0;
    std::size_t offset = 0;  // byte offset in the source
    std::size_t length = 0;  // byte length in the source
    int line = 0;            // 1-based
    int column = 0;          // 1-based, in Unicode code points
};

struct LexLine {
    int number = 0;
    int sourceLine = 0;
    std::size_t offset = 0;
    std::size_t length = 0;
};

struct LexResult {
    std::vector<LexToken> tokens;
    std::vector<LexLine> lines;
    Diagnostics diags;
};

LexResult lexSource(const std::string& utf8Source, const ModelProfile& profile);

}  // namespace msx
