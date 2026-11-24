#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace msx {

struct ModelProfile;

struct CompletionItem {
    enum class Kind { Keyword, Function, Variable, LineNumber };
    Kind kind = Kind::Keyword;
    std::string label;
    std::string insertText;
    std::string detail;
};

// Returns completion candidates for the caret byte offset in UTF-8 source.
std::vector<CompletionItem> completeAt(const std::string& utf8Source, std::size_t caret,
                                       const ModelProfile& profile);

// Returns a short argument hint for the statement at the caret, or an empty
// string when no hint is available.
std::string argumentHintAt(const std::string& utf8Source, std::size_t caret, const ModelProfile& profile);

}  // namespace msx
