#include "msx/basic.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

#include "msx/basic_token.h"
#include "msx/bcd.h"
#include "msx/charset.h"
#include "msx/utf8.h"

namespace msx {

namespace {

bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }
bool isAsciiAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isWordChar(char c) {
    return isAsciiAlpha(c) || isAsciiDigit(c) || c == '_' || c == '$' || c == '%' || c == '!' || c == '#';
}
char upperAscii(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

int columnAt(const std::string& line, std::size_t bytePos) {
    std::size_t i = 0;
    int column = 1;
    while (i < bytePos && i < line.size()) {
        uint32_t cp = 0;
        if (!utf8::decode(line, i, cp)) break;
        ++column;
    }
    return column;
}

struct LineRef {
    int target = 0;
    int sourceLine = 0;
    int column = 0;
    std::string what;
};

struct LineBuild {
    int number = 0;
    int sourceLine = 0;
    Bytes body;
};

class Compiler {
public:
    Compiler(const std::string& source, const CompileOptions& options, CompileResult& result)
        : source_(source), options_(options), result_(result) {
        if (!options_.profile) options_.profile = &cf2000Profile();
    }

    void run() {
        std::string text;
        utf8::stripBom(source_, text);
        splitLines(text);
        for (std::size_t i = 0; i < sourceLines_.size(); ++i) {
            compileLine(sourceLines_[i].text, sourceLines_[i].number);
        }
        assemble();
        checkRefs();
        if (result_.lines.empty()) {
            result_.diags.info("プログラム行がありません（空のプログラムを生成します）");
        }
    }

private:
    struct SourceLine {
        std::string text;
        int number = 0;
    };

    void splitLines(const std::string& text) {
        std::size_t i = 0;
        int lineNo = 1;
        std::string cur;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '\r') {
                sourceLines_.push_back(SourceLine{cur, lineNo++});
                cur.clear();
                ++i;
                if (i < text.size() && text[i] == '\n') ++i;
            } else if (c == '\n') {
                sourceLines_.push_back(SourceLine{cur, lineNo++});
                cur.clear();
                ++i;
            } else {
                cur.push_back(c);
                ++i;
            }
        }
        if (!cur.empty() || (!text.empty() && text[text.size() - 1] != '\n' && text[text.size() - 1] != '\r')) {
            sourceLines_.push_back(SourceLine{cur, lineNo});
        }
    }

    void encodeRun(const std::string& line, std::size_t begin, std::size_t end, int sourceLine, Bytes& out) {
        if (begin >= end) return;
        const std::string run = line.substr(begin, end - begin);
        Bytes encoded;
        charset::EncodeError error;
        const charset::EncodeStatus status = charset::encode(run, encoded, &error);
        if (status == charset::EncodeStatus::Ok) {
            out.insert(out.end(), encoded.begin(), encoded.end());
            return;
        }
        std::size_t j = 0;
        while (j < run.size()) {
            const std::size_t cpStart = j;
            uint32_t cp = 0;
            const bool ok = utf8::decode(run, j, cp);
            const std::string one = ok ? run.substr(cpStart, j - cpStart) : std::string();
            Bytes b;
            charset::EncodeError e2;
            if (ok && charset::encode(one, b, &e2) == charset::EncodeStatus::Ok) {
                out.insert(out.end(), b.begin(), b.end());
                continue;
            }
            const int column = columnAt(line, begin + cpStart);
            if (!ok) {
                result_.diags.error(sourceLine, column, "UTF-8 として不正なバイト列です");
            } else {
                char buf[192];
                const bool kanji = (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
                                   cp == 0x3005 || cp == 0x3006 || cp == 0x30F6;
                if (kanji) {
                    std::snprintf(buf, sizeof(buf),
                                  "漢字 U+%04X は CF-2000 の文字コードにないためテープへ出力できません"
                                  "（PC 上では編集・保存できます）",
                                  cp);
                } else if (cp >= 0x20 && cp <= 0x7E) {
                    std::snprintf(buf, sizeof(buf),
                                  "MSX 日本語文字コードに変換できない文字 U+%04X '%c' です", cp,
                                  static_cast<char>(cp));
                } else {
                    std::snprintf(buf, sizeof(buf), "MSX 日本語文字コードに変換できない文字 U+%04X です", cp);
                }
                result_.diags.error(sourceLine, column, buf);
            }
        }
    }

    void emitKeyword(const Keyword& kw, std::size_t matchStart) {
        if (kw.flags & KF_PrependColon) current_.body.push_back(0x3A);
        if (kw.code > 0xFF) {
            current_.body.push_back(0xFF);
            current_.body.push_back(static_cast<uint8_t>(kw.code & 0xFF));
        } else {
            current_.body.push_back(static_cast<uint8_t>(kw.code));
        }
        if (options_.warnUnsupportedKeywords) {
            const uint16_t flags = kw.flags;
            if ((flags & KF_MSX2) && options_.profile && !options_.profile->msx2Commands) {
                result_.diags.warning(currentSourceLine_, columnAt(currentLine_, matchStart),
                                      std::string(kw.text) + " は MSX BASIC 1.0（CF-2000）では未定義です");
            }
            if ((flags & KF_Disk) && options_.profile && !options_.profile->diskBasic) {
                result_.diags.warning(currentSourceLine_, columnAt(currentLine_, matchStart),
                                      std::string(kw.text) +
                                          " はディスク BASIC が必要です（標準 CF-2000 では使用できません）");
            }
        }
    }

    bool bareLineNumberAfter(const std::string& s, std::size_t p, int& value) {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
        const std::size_t start = p;
        while (p < s.size() && isAsciiDigit(s[p])) ++p;
        if (p == start) return false;
        long v = 0;
        for (std::size_t i = start; i < p; ++i) v = v * 10 + (s[i] - '0');
        if (v > 65529) return false;
        std::size_t q = p;
        while (q < s.size() && (s[q] == ' ' || s[q] == '\t')) ++q;
        if (q >= s.size() || s[q] == ':') {
            value = static_cast<int>(v);
            return true;
        }
        // A trailing comment also ends the statement.
        if (s[q] == '\'') {
            value = static_cast<int>(v);
            return true;
        }
        if (q + 3 <= s.size() && upperAscii(s[q]) == 'R' && upperAscii(s[q + 1]) == 'E' &&
            upperAscii(s[q + 2]) == 'M') {
            value = static_cast<int>(v);
            return true;
        }
        if (q + 4 <= s.size()) {
            if (upperAscii(s[q]) == 'E' && upperAscii(s[q + 1]) == 'L' && upperAscii(s[q + 2]) == 'S' &&
                upperAscii(s[q + 3]) == 'E') {
                value = static_cast<int>(v);
                return true;
            }
        }
        return false;
    }

    bool nextIsComma(const std::string& s, std::size_t p) const {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
        return p < s.size() && s[p] == ',';
    }

    void compileLine(const std::string& line, int sourceLine) {
        currentSourceLine_ = sourceLine;
        currentLine_ = line;

        std::size_t p = 0;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
        if (p >= line.size()) return;

        if (!isAsciiDigit(line[p])) {
            result_.diags.error(sourceLine, columnAt(line, p), "行番号がありません");
            return;
        }
        const std::size_t numStart = p;
        long lineNumber = 0;
        while (p < line.size() && isAsciiDigit(line[p])) {
            if (lineNumber < 1000000) lineNumber = lineNumber * 10 + (line[p] - '0');
            ++p;
        }
        if (lineNumber > 65529) {
            result_.diags.error(sourceLine, columnAt(line, numStart), "行番号が範囲外です（0～65529）");
            return;
        }

        current_ = LineBuild();
        current_.number = static_cast<int>(lineNumber);
        current_.sourceLine = sourceLine;

        bool pendingLineRef = false;
        std::string pendingWhat = "GOTO";
        bool inWord = false;

        while (p < line.size()) {
            const char c = line[p];
            if (c == ' ' || c == '\t') {
                ++p;
                inWord = false;
                continue;
            }
            if (pendingLineRef && !isAsciiDigit(c) && c != ',') {
                pendingLineRef = false;
            }
            if (!isWordChar(c)) inWord = false;

            if (c == '"') {
                const std::size_t start = p;
                ++p;
                while (p < line.size() && line[p] != '"') ++p;
                if (p < line.size()) {
                    ++p;
                } else {
                    result_.diags.warning(sourceLine, columnAt(line, start), "文字列が閉じていません");
                }
                encodeRun(line, start, p, sourceLine, current_.body);
                pendingLineRef = false;
                inWord = false;
                continue;
            }

            if (c == '\'') {
                current_.body.push_back(0x3A);
                current_.body.push_back(0x8F);
                current_.body.push_back(0xE6);
                encodeRun(line, p + 1, line.size(), sourceLine, current_.body);
                p = line.size();
                break;
            }

            if (c == '?') {
                current_.body.push_back(0x91);
                ++p;
                pendingLineRef = false;
                inWord = false;
                continue;
            }

            if (c == '&' && p + 2 <= line.size()) {
                const char kind = upperAscii(line[p + 1]);
                if (kind == 'H' || kind == 'O' || kind == 'B') {
                    const std::size_t start = p;
                    p += 2;
                    const std::size_t digitsStart = p;
                    if (kind == 'H') {
                        while (p < line.size() && std::isxdigit(static_cast<unsigned char>(line[p]))) ++p;
                    } else if (kind == 'O') {
                        while (p < line.size() && line[p] >= '0' && line[p] <= '7') ++p;
                    } else {
                        while (p < line.size() && (line[p] == '0' || line[p] == '1')) ++p;
                    }
                    if (p == digitsStart) {
                        result_.diags.error(sourceLine, columnAt(line, start),
                                            std::string("&") + kind + " の後に数値がありません");
                        pendingLineRef = false;
                        continue;
                    }
                    if (p - digitsStart > 4) {
                        result_.diags.warning(sourceLine, columnAt(line, start),
                                              "16 ビットを超える数値定数です");
                        p = digitsStart + 4;
                    }
                    if (kind == 'B') {
                        current_.body.push_back('&');
                        current_.body.push_back('B');
                        for (std::size_t i = digitsStart; i < p; ++i) {
                            current_.body.push_back(static_cast<uint8_t>(line[i]));
                        }
                    } else {
                        unsigned value = 0;
                        for (std::size_t i = digitsStart; i < p; ++i) {
                            const char d = line[i];
                            int digit = 0;
                            if (d >= '0' && d <= '9') {
                                digit = d - '0';
                            } else if (kind == 'H') {
                                digit = std::tolower(static_cast<unsigned char>(d)) - 'a' + 10;
                            }
                            value = value * (kind == 'H' ? 16u : 8u) + static_cast<unsigned>(digit);
                        }
                        current_.body.push_back(kind == 'H' ? 0x0C : 0x0B);
                        appendLe16(current_.body, static_cast<uint16_t>(value));
                    }
                    pendingLineRef = false;
                    inWord = false;
                    continue;
                }
            }

            if (isAsciiDigit(c) || (c == '.' && p + 1 < line.size() && isAsciiDigit(line[p + 1]))) {
                const std::size_t start = p;
                bool hasPoint = false;
                bool hasExponent = false;
                while (p < line.size() && isAsciiDigit(line[p])) ++p;
                if (p < line.size() && line[p] == '.') {
                    hasPoint = true;
                    ++p;
                    while (p < line.size() && isAsciiDigit(line[p])) ++p;
                }
                if (p < line.size() && (line[p] == 'E' || line[p] == 'e')) {
                    std::size_t q = p + 1;
                    if (q < line.size() && (line[q] == '+' || line[q] == '-')) ++q;
                    if (q < line.size() && isAsciiDigit(line[q])) {
                        p = q;
                        while (p < line.size() && isAsciiDigit(line[p])) ++p;
                        hasExponent = true;
                    }
                }
                bool singleSuffix = false;
                bool doubleSuffix = false;
                if (p < line.size() && (line[p] == '!' || line[p] == '#')) {
                    singleSuffix = line[p] == '!';
                    doubleSuffix = line[p] == '#';
                    ++p;
                }
                const std::string literal = line.substr(start, p - start - (singleSuffix || doubleSuffix ? 1 : 0));

                if (!hasPoint && !hasExponent && !singleSuffix && !doubleSuffix) {
                    long long value = 0;
                    for (std::size_t i = 0; i < literal.size(); ++i) {
                        if (value < 1000000000LL) value = value * 10 + (literal[i] - '0');
                    }
                    if (pendingLineRef) {
                        if (value > 65529) {
                            result_.diags.error(sourceLine, columnAt(line, start),
                                                "行番号が範囲外です（0～65529）");
                        } else {
                            current_.body.push_back(0x0E);
                            appendLe16(current_.body, static_cast<uint16_t>(value));
                            refs_.push_back(LineRef{static_cast<int>(value), sourceLine,
                                                    columnAt(line, start), pendingWhat});
                        }
                        pendingLineRef = nextIsComma(line, p);
                    } else if (value <= 9) {
                        current_.body.push_back(static_cast<uint8_t>(0x11 + value));
                    } else if (value <= 255) {
                        current_.body.push_back(0x0F);
                        current_.body.push_back(static_cast<uint8_t>(value));
                    } else if (value <= 32767) {
                        current_.body.push_back(0x1C);
                        appendLe16(current_.body, static_cast<uint16_t>(value));
                    } else {
                        Bytes encoded;
                        bcd::EncodeResult r = bcd::encode(literal, bcd::kDoubleDigits, encoded);
                        if (!r.ok) {
                            result_.diags.error(sourceLine, columnAt(line, start), r.message);
                        } else {
                            current_.body.push_back(0x1F);
                            current_.body.insert(current_.body.end(), encoded.begin(), encoded.end());
                        }
                    }
                } else {
                    const bool useSingle = singleSuffix;
                    const int digits = useSingle ? bcd::kSingleDigits : bcd::kDoubleDigits;
                    Bytes encoded;
                    bcd::EncodeResult r = bcd::encode(literal, digits, encoded);
                    if (!r.ok) {
                        result_.diags.error(sourceLine, columnAt(line, start), r.message);
                    } else {
                        if (r.truncated) {
                            result_.diags.warning(sourceLine, columnAt(line, start),
                                                  "数値定数の有効桁数を超えたため丸めました");
                        }
                        current_.body.push_back(useSingle ? 0x1D : 0x1F);
                        current_.body.insert(current_.body.end(), encoded.begin(), encoded.end());
                    }
                }
                pendingLineRef = false;
                inWord = false;
                (void)doubleSuffix;
                continue;
            }

            const Keyword* kw = matchKeyword(line, p);
            if (kw) {
                const std::size_t matchStart = p;
                if (inWord) {
                    result_.diags.warning(sourceLine, columnAt(line, matchStart),
                                          std::string("キーワード ") + kw->text +
                                              " が変数名の内部でトークン化されます（MSX BASIC と同じ挙動）");
                }
                emitKeyword(*kw, matchStart);
                p += std::strlen(kw->text);
                inWord = false;
                const uint16_t flags = kw->flags;
                if (flags & KF_Comment) {
                    encodeRun(line, p, line.size(), sourceLine, current_.body);
                    p = line.size();
                    break;
                }
                if (flags & KF_Data) {
                    std::size_t q = p;
                    while (q < line.size()) {
                        if (line[q] == '"') {
                            ++q;
                            while (q < line.size() && line[q] != '"') ++q;
                            if (q < line.size()) ++q;
                        } else if (line[q] == ':') {
                            break;
                        } else {
                            ++q;
                        }
                    }
                    encodeRun(line, p, q, sourceLine, current_.body);
                    p = q;
                    pendingLineRef = false;
                    continue;
                }
                if (flags & KF_LineRef) {
                    pendingLineRef = true;
                    pendingWhat = kw->text;
                }
                if (flags & KF_ThenElse) {
                    int dummy = 0;
                    pendingLineRef = bareLineNumberAfter(line, p, dummy);
                    pendingWhat = kw->text;
                }
                continue;
            }

            switch (c) {
                case '>': current_.body.push_back(0xEE); ++p; pendingLineRef = false; inWord = false; continue;
                case '=': current_.body.push_back(0xEF); ++p; pendingLineRef = false; inWord = false; continue;
                case '<': current_.body.push_back(0xF0); ++p; pendingLineRef = false; inWord = false; continue;
                case '+': current_.body.push_back(0xF1); ++p; pendingLineRef = false; inWord = false; continue;
                case '-': current_.body.push_back(0xF2); ++p; pendingLineRef = false; inWord = false; continue;
                case '*': current_.body.push_back(0xF3); ++p; pendingLineRef = false; inWord = false; continue;
                case '/': current_.body.push_back(0xF4); ++p; pendingLineRef = false; inWord = false; continue;
                case '^': current_.body.push_back(0xF5); ++p; pendingLineRef = false; inWord = false; continue;
                case '\\': current_.body.push_back(0xFC); ++p; pendingLineRef = false; inWord = false; continue;
                case ':':
                    current_.body.push_back(0x3A);
                    ++p;
                    pendingLineRef = false;
                    inWord = false;
                    continue;
                case ',':
                    current_.body.push_back(',');
                    ++p;
                    inWord = false;
                    continue;
                default:
                    break;
            }

            if (static_cast<unsigned char>(c) < 0x80) {
                current_.body.push_back(static_cast<uint8_t>(c));
                if (isAsciiAlpha(c)) inWord = true;
                pendingLineRef = false;
                ++p;
                continue;
            }

            const std::size_t cpStart = p;
            std::size_t q = p;
            uint32_t cp = 0;
            utf8::decode(line, q, cp);
            result_.diags.error(sourceLine, columnAt(line, cpStart),
                                "文字列・REM・DATA 以外では日本語などの非 ASCII 文字を使用できません");
            p = q;
        }

        if (!current_.body.empty()) {
            if (lineNumbers_.insert(current_.number).second == false) {
                result_.diags.error(sourceLine, columnAt(line, numStart),
                                    "行番号 " + std::to_string(current_.number) + " が重複しています");
                return;
            }
            if (current_.body.size() > 255) {
                result_.diags.warning(sourceLine, 0,
                                      "1 行のトークン列が 255 バイトを超えています（実機の制限に注意）");
            }
            builds_.push_back(current_);
        }
    }

    void assemble() {
        const uint16_t base = options_.profile ? options_.profile->baseAddress : 0x8000;
        Bytes& image = result_.image;
        for (std::size_t i = 0; i < builds_.size(); ++i) {
            const LineBuild& line = builds_[i];
            const std::size_t offset = image.size();
            const std::size_t nextAddr = static_cast<std::size_t>(base) + offset + 4 + line.body.size() + 1;
            if (nextAddr > 0xFFFF) {
                result_.diags.error(line.sourceLine, 0, "行リンクのアドレスが 64 KB を超えました");
                break;
            }
            SourceLineInfo info;
            info.sourceLine = line.sourceLine;
            info.number = line.number;
            info.imageOffset = offset;
            info.bodySize = line.body.size();
            result_.lines.push_back(info);
            appendLe16(image, static_cast<uint16_t>(nextAddr));
            appendLe16(image, static_cast<uint16_t>(line.number));
            image.insert(image.end(), line.body.begin(), line.body.end());
            image.push_back(0x00);
        }
        if (result_.lines.empty()) {
            image = Bytes(2, 0);
        } else {
            appendLe16(image, 0x0000);
        }

        const uint32_t used = static_cast<uint32_t>(image.size());
        const uint32_t freeBytes = options_.profile ? options_.profile->freeBytes : 12431u;
        if (used > freeBytes) {
            result_.diags.error("プログラムが空き容量を超えています（" + std::to_string(used) +
                                " バイト、FRE(0)=" + std::to_string(freeBytes) + " バイト）");
        } else {
            result_.diags.info("行数 " + std::to_string(result_.lines.size()) + "、プログラム " +
                               std::to_string(used) + " バイト、残り " + std::to_string(freeBytes - used) +
                               " バイト（変数・配列・スタックを除く）");
        }
    }

    void checkRefs() {
        std::set<int> numbers;
        for (std::size_t i = 0; i < result_.lines.size(); ++i) numbers.insert(result_.lines[i].number);
        std::size_t reported = 0;
        for (std::size_t i = 0; i < refs_.size(); ++i) {
            const LineRef& ref = refs_[i];
            if (numbers.find(ref.target) == numbers.end() && reported < 100) {
                ++reported;
                result_.diags.warning(ref.sourceLine, ref.column,
                                      ref.what + " のジャンプ先 " + std::to_string(ref.target) +
                                          " 行が定義されていません");
            }
        }
    }

    const std::string& source_;
    CompileOptions options_;
    CompileResult& result_;
    std::vector<SourceLine> sourceLines_;
    std::vector<LineBuild> builds_;
    std::set<int> lineNumbers_;
    std::vector<LineRef> refs_;
    LineBuild current_;
    int currentSourceLine_ = 0;
    std::string currentLine_;
};

}  // namespace

CompileResult compileProgram(const std::string& utf8Source, const CompileOptions& options) {
    CompileResult result;
    Compiler compiler(utf8Source, options, result);
    compiler.run();
    return result;
}

Bytes toDiskFormat(const Bytes& image) {
    Bytes out;
    out.reserve(image.size() + 1);
    out.push_back(0xFF);
    out.insert(out.end(), image.begin(), image.end());
    return out;
}

Bytes toTapeBody(const Bytes& image, bool padToEight) {
    Bytes out = image;
    out.insert(out.end(), 7, 0x00);
    if (padToEight) {
        while ((out.size() % 8) != 0) out.push_back(0x00);
    }
    return out;
}

}  // namespace msx
