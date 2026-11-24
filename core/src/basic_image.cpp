#include "msx/basic.h"

#include <cstdio>
#include <map>
#include <sstream>

#include "msx/basic_token.h"
#include "msx/bcd.h"
#include "msx/charset.h"

namespace msx {

namespace {

std::size_t operandLength(uint8_t op) {
    switch (op) {
        case 0x0F: return 1;
        case 0x0B:
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x1C: return 2;
        case 0x1D: return 4;
        case 0x1F: return 8;
        default: return 0;
    }
}

// Finds the end (the 0x00 terminator) of a tokenized line body.  BCD
// constants and two-byte tokens may contain 0x00 bytes, so a simple
// memchr() is not sufficient.
std::size_t findLineEnd(const uint8_t* data, std::size_t size, std::size_t start, Diagnostics& diags,
                        int lineNumber) {
    std::size_t q = start;
    bool inString = false;
    bool rem = false;
    bool dataMode = false;
    bool dataQuote = false;
    while (q < size) {
        const uint8_t b = data[q];
        if (inString) {
            inString = (b != 0x22);
            ++q;
            continue;
        }
        if (b == 0x00) return q;
        if (dataMode) {
            if (dataQuote) {
                if (b == 0x22) dataQuote = false;
                ++q;
                continue;
            }
            if (b == 0x22) {
                dataQuote = true;
                ++q;
                continue;
            }
            if (b == 0x3A) {
                dataMode = false;
                ++q;
                continue;
            }
            ++q;
            continue;
        }
        if (b == 0x00) return q;
        if (b == 0x22) {
            inString = true;
            ++q;
            continue;
        }
        if (b == 0x8F) {
            rem = true;
            ++q;
            continue;
        }
        if (rem) {
            ++q;
            continue;
        }
        if (b == 0x84) {
            dataMode = true;
            ++q;
            continue;
        }
        if (b == 0xFF) {
            if (q + 1 >= size) {
                diags.error(lineNumber, 0, "2 バイトトークンが行末で途切れています");
                return size;
            }
            q += 2;
            continue;
        }
        const std::size_t len = operandLength(b);
        if (len != 0) {
            if (q + 1 + len > size) {
                diags.error(lineNumber, 0, "数値定数のバイト数が行末で途切れています");
                return size;
            }
            q += 1 + len;
            continue;
        }
        ++q;
    }
    diags.error(lineNumber, 0, "行末の 0x00 が見つかりません（データが途切れています）");
    return size;
}

std::string hexValue(uint16_t value, bool upper) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), upper ? "%X" : "%x", value);
    return buf;
}

std::string octalValue(uint16_t value) {
    if (value == 0) return "0";
    std::string out;
    while (value != 0) {
        out.insert(out.begin(), static_cast<char>('0' + (value & 7)));
        value = static_cast<uint16_t>(value >> 3);
    }
    return out;
}

class TextBuilder {
public:
    explicit TextBuilder(bool readable) : readable_(readable) {}

    void raw(const std::string& s) {
        if (s.empty()) return;
        if (readable_ && isWordStart(s[0]) && (needSpace_ || lastCharIsWord())) text_.push_back(' ');
        text_ += s;
        needSpace_ = readable_ && isWordEnd(text_[text_.size() - 1]);
    }

    // Appends one identifier character or punctuation byte; adds a leading
    // space only when the previous token ended a word.
    void exact(const std::string& s) {
        if (s.empty()) return;
        if (readable_ && needSpace_ && isWordStart(s[0])) text_.push_back(' ');
        text_ += s;
        needSpace_ = false;
    }

    // Appends literal bytes (DATA/REM content) without changing spacing.
    void literal(const std::string& s) {
        text_ += s;
        needSpace_ = false;
    }

    void punctuation(const std::string& s) {
        text_ += s;
        needSpace_ = false;
    }

    void startLine(int number) {
        if (readable_) {
            text_ += std::to_string(number);
            text_.push_back(' ');
            needSpace_ = false;
        }
    }

    void endLine(bool crlf) {
        text_ += crlf ? "\r\n" : "\n";
        needSpace_ = false;
    }

    const std::string& str() const { return text_; }

private:
    static bool isWordStart(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               c == '"' || c == '\'' || static_cast<unsigned char>(c) >= 0x80;
    }
    static bool isWordEnd(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               c == ')' || c == '"' || static_cast<unsigned char>(c) >= 0x80;
    }
    bool lastCharIsWord() const {
        return !text_.empty() && isWordEnd(text_[text_.size() - 1]);
    }

    bool readable_;
    bool needSpace_ = false;
    std::string text_;
};

}  // namespace

ParseImageResult parseProgramImage(const uint8_t* data, std::size_t size, const ModelProfile& profile) {
    ParseImageResult result;
    std::size_t pos = 0;
    while (pos + 2 <= size) {
        const uint16_t link = readLe16(data + pos);
        if (link == 0) {
            pos += 2;
            break;
        }
        if (pos + 4 > size) {
            result.diags.error("行ヘッダーが途中で終わっています");
            break;
        }
        ParsedLine line;
        line.storedLink = link;
        line.imageOffset = pos;
        line.number = readLe16(data + pos + 2);
        const std::size_t bodyStart = pos + 4;
        const std::size_t end = findLineEnd(data, size, bodyStart, result.diags, line.number);
        if (end > size) {
            line.body.assign(data + bodyStart, data + size);
            result.program.lines.push_back(line);
            break;
        }
        line.body.assign(data + bodyStart, data + end);
        result.program.lines.push_back(line);
        pos = end + 1;
        if (line.number > 65529) {
            result.diags.warning(line.number, 0, "行番号が 65529 を超えています");
        }
    }
    if (pos < size && pos + 2 > size) {
        result.diags.error("プログラム終端が途切れています");
    }

    uint16_t base = profile.baseAddress;
    if (!result.program.lines.empty()) {
        const ParsedLine& first = result.program.lines[0];
        const int candidate = static_cast<int>(first.storedLink) -
                              static_cast<int>(4 + first.body.size() + 1);
        if (candidate >= 0x4000 && candidate <= 0xF000) {
            base = static_cast<uint16_t>(candidate);
        }
    }
    result.program.baseAddress = base;
    result.program.linksValid = true;
    for (std::size_t i = 0; i < result.program.lines.size(); ++i) {
        const ParsedLine& line = result.program.lines[i];
        const int expected = base + static_cast<int>(line.imageOffset + 4 + line.body.size() + 1);
        if ((expected & 0xFFFF) != line.storedLink) {
            result.program.linksValid = false;
            // Some disk tools store 0xFFFF/0x0000 links; the ROM recalculates
            // them on load.  Do not flood the user with one warning per line.
            if (line.storedLink != 0x0000 && line.storedLink != 0xFFFF) {
                result.diags.warning(
                    line.number, 0,
                    "行リンクが不整合です（格納値 &H" + hexValue(line.storedLink, true) + "、期待値 &H" +
                        hexValue(static_cast<uint16_t>(expected & 0xFFFF), true) +
                        "）。MSX はロード時に再計算するため実行には影響しません");
            }
        }
        if (i != 0 && result.program.lines[i - 1].number >= line.number) {
            result.diags.warning(line.number, 0, "行番号が昇順ではありません");
        }
    }
    return result;
}

ParseImageResult parseProgramImage(const Bytes& data, const ModelProfile& profile) {
    return parseProgramImage(data.empty() ? nullptr : &data[0], data.size(), profile);
}

ParseImageResult parseDiskBasicFile(const Bytes& file, const ModelProfile& profile) {
    Bytes body = file;
    if (!body.empty() && body[0] == 0xFF) body.erase(body.begin());
    return parseProgramImage(body, profile);
}

std::string detokenize(const ParsedProgram& program, const TextOptions& options, Diagnostics& diags) {
    std::map<uint32_t, int> addressToLine;
    for (std::size_t i = 0; i < program.lines.size(); ++i) {
        const uint32_t address =
            static_cast<uint32_t>(program.baseAddress + program.lines[i].imageOffset);
        addressToLine[address] = program.lines[i].number;
    }

    TextBuilder out(options.readableSpacing);
    for (std::size_t li = 0; li < program.lines.size(); ++li) {
        const ParsedLine& line = program.lines[li];
        if (options.includeLineNumbers) out.startLine(line.number);
        bool rem = false;
        bool dataMode = false;
        bool dataQuote = false;
        const Bytes& b = line.body;
        std::size_t i = 0;
        while (i < b.size()) {
            const uint8_t v = b[i];
            if (rem) {
                out.literal(charset::decode(&b[i], b.size() - i));
                i = b.size();
                break;
            }
            if (dataMode) {
                if (dataQuote) {
                    const std::size_t start = i;
                    while (i < b.size() && b[i] != 0x22) ++i;
                    out.literal(charset::decode(&b[start], i - start));
                    if (i < b.size()) {
                        out.literal("\"");
                        ++i;
                    }
                    dataQuote = false;
                    continue;
                }
                if (v == 0x22) {
                    out.literal("\"");
                    ++i;
                    dataQuote = true;
                    continue;
                }
                if (v == 0x3A) {
                    out.punctuation(":");
                    dataMode = false;
                    ++i;
                    continue;
                }
                const std::size_t start = i;
                while (i < b.size() && b[i] != 0x3A && b[i] != 0x22) ++i;
                out.literal(charset::decode(&b[start], i - start));
                continue;
            }
            if (v == 0x22) {
                const std::size_t start = i;
                ++i;
                while (i < b.size() && b[i] != 0x22) ++i;
                if (i < b.size()) ++i;
                out.raw(charset::decode(&b[start], i - start));
                continue;
            }
            if (v == 0x3A && i + 2 < b.size() && b[i + 1] == 0x8F && b[i + 2] == 0xE6) {
                out.punctuation("'");
                i += 3;
                rem = true;
                continue;
            }
            if (v == 0x3A && i + 1 < b.size() && b[i + 1] == 0xA1) {
                out.raw("ELSE");
                i += 2;
                continue;
            }
            if (v == 0x8F) {
                out.raw("REM");
                ++i;
                rem = true;
                continue;
            }
            if (v == 0x84) {
                out.raw("DATA");
                ++i;
                dataMode = true;
                continue;
            }
            if (v >= 0x11 && v <= 0x1A) {
                out.raw(std::to_string(v - 0x11));
                ++i;
                continue;
            }
            if (v == 0x0F) {
                if (i + 1 >= b.size()) {
                    diags.error(line.number, 0, "0F トークンの値がありません");
                    break;
                }
                out.raw(std::to_string(b[i + 1]));
                i += 2;
                continue;
            }
            if (v == 0x1C) {
                if (i + 2 >= b.size()) {
                    diags.error(line.number, 0, "1C トークンの値がありません");
                    break;
                }
                const int16_t value = static_cast<int16_t>(readLe16(&b[i + 1]));
                out.raw(std::to_string(value));
                i += 3;
                continue;
            }
            if (v == 0x0E) {
                if (i + 2 >= b.size()) {
                    diags.error(line.number, 0, "0E トークンの値がありません");
                    break;
                }
                out.raw(std::to_string(readLe16(&b[i + 1])));
                i += 3;
                continue;
            }
            if (v == 0x0D) {
                if (i + 2 >= b.size()) {
                    diags.error(line.number, 0, "0D トークンの値がありません");
                    break;
                }
                const uint16_t address = readLe16(&b[i + 1]);
                std::map<uint32_t, int>::const_iterator it = addressToLine.find(address);
                if (it != addressToLine.end()) {
                    out.raw(std::to_string(it->second));
                } else {
                    diags.warning(line.number, 0, "0D の行アドレス &H" + hexValue(address, true) +
                                                    " が行頭と一致しません");
                    out.raw("{&H" + hexValue(address, true) + "}");
                }
                i += 3;
                continue;
            }
            if (v == 0x1D || v == 0x1F) {
                const int digits = (v == 0x1D) ? bcd::kSingleDigits : bcd::kDoubleDigits;
                const int bytes = bcd::byteCount(digits);
                if (i + 1 + static_cast<std::size_t>(bytes) > b.size()) {
                    diags.error(line.number, 0, "浮動小数点数が途中で途切れています");
                    break;
                }
                const bcd::DecodeResult decoded = bcd::decodeDetailed(&b[i + 1], digits, options.addPrecisionSuffix);
                if (!decoded.validBcd) {
                    diags.error(line.number, 0, "BCD ではない数値定数です（A-F の桁を含む）");
                }
                out.raw(decoded.text);
                i += 1 + static_cast<std::size_t>(bytes);
                continue;
            }
            if (v == 0x0B) {
                if (i + 2 >= b.size()) {
                    diags.error(line.number, 0, "0B トークンの値がありません");
                    break;
                }
                out.raw("&O" + octalValue(readLe16(&b[i + 1])));
                i += 3;
                continue;
            }
            if (v == 0x0C) {
                if (i + 2 >= b.size()) {
                    diags.error(line.number, 0, "0C トークンの値がありません");
                    break;
                }
                out.raw("&H" + hexValue(readLe16(&b[i + 1]), true));
                i += 3;
                continue;
            }
            if (v == 0xFF) {
                if (i + 1 >= b.size()) {
                    diags.error(line.number, 0, "2 バイトトークンが途切れています");
                    break;
                }
                const uint16_t code = static_cast<uint16_t>(0xFF00 | b[i + 1]);
                const Keyword* kw = keywordByCode(code);
                if (kw) {
                    out.raw(kw->text);
                } else {
                    const std::string txt = tokenCodeText(code);
                    diags.warning(line.number, 0, "未知のトークン {" + txt + "}");
                    out.raw("{" + txt + "}");
                }
                i += 2;
                continue;
            }
            if (v >= 0x81) {
                const Keyword* kw = keywordByCode(v);
                if (kw) {
                    out.raw(kw->text);
                } else {
                    const std::string txt = tokenCodeText(v);
                    diags.warning(line.number, 0, "未知のトークン {" + txt + "}");
                    out.raw("{" + txt + "}");
                }
                ++i;
                continue;
            }
            if (v >= 0x20) {
                out.exact(charset::byteToUtf8(v));
                ++i;
                continue;
            }
            // Control code inside the token stream (outside a literal).
            diags.warning(line.number, 0, "制御コード 0x" + hexByte(v) + " を検出しました");
            out.raw("{0x" + hexByte(v) + "}");
            ++i;
        }
        if (options.includeLineNumbers) out.endLine(options.crlf);
    }
    return out.str();
}

Bytes relinkImage(const Bytes& image, uint16_t baseAddress) {
    Diagnostics diags;
    ParseImageResult parsed = parseProgramImage(image, cf2000Profile());
    Bytes out;
    for (std::size_t i = 0; i < parsed.program.lines.size(); ++i) {
        const ParsedLine& line = parsed.program.lines[i];
        const std::size_t next = static_cast<std::size_t>(baseAddress) + out.size() + 4 + line.body.size() + 1;
        appendLe16(out, static_cast<uint16_t>(next & 0xFFFF));
        appendLe16(out, static_cast<uint16_t>(line.number));
        out.insert(out.end(), line.body.begin(), line.body.end());
        out.push_back(0x00);
    }
    appendLe16(out, 0x0000);
    return out;
}

}  // namespace msx
