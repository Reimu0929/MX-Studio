#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace msx {
namespace utf8 {

// Decodes one code point starting at index.  Returns false (and advances by
// one byte) on malformed input.
bool decode(const std::string& text, std::size_t& index, uint32_t& codePoint);
void append(std::string& out, uint32_t codePoint);

// Returns the byte offset of the code point that contains `byteOffset`.
std::size_t codepointStart(const std::string& text, std::size_t byteOffset);

// Validates and strips an optional UTF-8 BOM.
bool stripBom(const std::string& text, std::string& out);

}  // namespace utf8
}  // namespace msx
