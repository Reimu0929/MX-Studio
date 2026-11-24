#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "msx/common.h"
#include "msx/profile.h"

namespace msx {

struct CompileOptions {
    const ModelProfile* profile = nullptr;
    bool warnUnsupportedKeywords = true;
};

struct SourceLineInfo {
    int sourceLine = 0;      // 1-based line in the UTF-8 source
    int number = 0;
    std::size_t imageOffset = 0;
    std::size_t bodySize = 0;
};

struct CompileResult {
    Bytes image;  // in-memory tokenized BASIC program, no 0xFF marker
    std::vector<SourceLineInfo> lines;
    Diagnostics diags;

    bool ok() const { return !diags.hasErrors(); }
    std::size_t lineCount() const { return lines.size(); }
};

CompileResult compileProgram(const std::string& utf8Source, const CompileOptions& options);

struct ParsedLine {
    int number = 0;
    std::size_t imageOffset = 0;
    Bytes body;  // tokenized statements, without the 0x00 terminator
    uint16_t storedLink = 0;
};

struct ParsedProgram {
    std::vector<ParsedLine> lines;
    bool linksValid = true;
    uint16_t baseAddress = 0x8000;
};

struct ParseImageResult {
    ParsedProgram program;
    Diagnostics diags;
    bool ok() const { return !diags.hasErrors(); }
};

ParseImageResult parseProgramImage(const uint8_t* data, std::size_t size, const ModelProfile& profile);
ParseImageResult parseProgramImage(const Bytes& data, const ModelProfile& profile);
ParseImageResult parseDiskBasicFile(const Bytes& file, const ModelProfile& profile);

struct TextOptions {
    bool addPrecisionSuffix = true;  // '!' for single, '#' for double
    bool includeLineNumbers = true;
    bool readableSpacing = true;
    bool crlf = true;
};

std::string detokenize(const ParsedProgram& program, const TextOptions& options, Diagnostics& diags);

Bytes toDiskFormat(const Bytes& image);
Bytes toTapeBody(const Bytes& image, bool padToEight);

// Re-links the next-line pointers using baseAddress.  The MSX ROM recalculates
// them on load, but a correctly linked image is useful for inspection.
Bytes relinkImage(const Bytes& image, uint16_t baseAddress);

}  // namespace msx
