#include "msx/basic_lexer.h"

#include <cctype>

#include "msx/basic_token.h"
#include "msx/profile.h"
#include "msx/utf8.h"

namespace msx {

namespace {

int columnOf(const std::string& line, std::size_t bytePos) {
    std::size_t i = 0;
    int column = 1;
    while (i < bytePos && i < line.size()) {
        uint32_t cp = 0;
        if (!utf8::decode(line, i, cp)) break;
        ++column;
    }
    return column;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
char upperAscii(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

int findDataEnd(const std::string& line, std::size_t start) {
    std::size_t q = start;
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
    return static_cast<int>(q);
}

}  // namespace

LexResult lexSource(const std::string& utf8Source, const ModelProfile&) {
    LexResult result;
    std::string text;
    utf8::stripBom(utf8Source, text);

    std::size_t lineStart = 0;
    int sourceLine = 1;
    while (true) {
        std::size_t lineEnd = lineStart;
        while (lineEnd < text.size() && text[lineEnd] != '\n' && text[lineEnd] != '\r') ++lineEnd;
        const std::string line = text.substr(lineStart, lineEnd - lineStart);

        std::size_t p = 0;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
        if (p < line.size()) {
            if (isDigit(line[p])) {
                const std::size_t start = p;
                int number = 0;
                while (p < line.size() && isDigit(line[p])) {
                    number = number * 10 + (line[p] - '0');
                    ++p;
                }
                LexToken tok;
                tok.kind = LexKind::LineNumber;
                tok.text = line.substr(start, p - start);
                tok.offset = lineStart + start;
                tok.length = p - start;
                tok.line = sourceLine;
                tok.column = columnOf(line, start);
                result.tokens.push_back(tok);

                LexLine info;
                info.number = number;
                info.sourceLine = sourceLine;
                info.offset = lineStart;
                info.length = line.size();
                result.lines.push_back(info);
            } else {
                result.diags.error(sourceLine, columnOf(line, p), "行番号がありません");
            }
        }

        while (p < line.size()) {
            const char c = line[p];
            if (c == ' ' || c == '\t') {
                ++p;
                continue;
            }
            const std::size_t start = p;
            LexToken tok;
            tok.offset = lineStart + start;
            tok.line = sourceLine;
            tok.column = columnOf(line, start);

            if (c == '"') {
                ++p;
                while (p < line.size() && line[p] != '"') ++p;
                if (p < line.size()) ++p;
                tok.kind = LexKind::String;
            } else if (c == '\'') {
                p = line.size();
                tok.kind = LexKind::Comment;
            } else if (c == '&' && p + 1 < line.size() &&
                       (upperAscii(line[p + 1]) == 'H' || upperAscii(line[p + 1]) == 'O' ||
                        upperAscii(line[p + 1]) == 'B')) {
                const char kind = upperAscii(line[p + 1]);
                p += 2;
                if (kind == 'H') {
                    while (p < line.size() && std::isxdigit(static_cast<unsigned char>(line[p]))) ++p;
                    tok.kind = LexKind::Hex;
                } else if (kind == 'O') {
                    while (p < line.size() && line[p] >= '0' && line[p] <= '7') ++p;
                    tok.kind = LexKind::Octal;
                } else {
                    while (p < line.size() && (line[p] == '0' || line[p] == '1')) ++p;
                    tok.kind = LexKind::Binary;
                }
            } else if (isDigit(c) || (c == '.' && p + 1 < line.size() && isDigit(line[p + 1]))) {
                bool point = false;
                bool exp = false;
                while (p < line.size() && isDigit(line[p])) ++p;
                if (p < line.size() && line[p] == '.') {
                    point = true;
                    ++p;
                    while (p < line.size() && isDigit(line[p])) ++p;
                }
                if (p < line.size() && (line[p] == 'E' || line[p] == 'e')) {
                    std::size_t q = p + 1;
                    if (q < line.size() && (line[q] == '+' || line[q] == '-')) ++q;
                    if (q < line.size() && isDigit(line[q])) {
                        p = q;
                        while (p < line.size() && isDigit(line[p])) ++p;
                        exp = true;
                    }
                }
                if (p < line.size() && (line[p] == '!' || line[p] == '#')) ++p;
                tok.kind = (point || exp) ? LexKind::Float : LexKind::Integer;
            } else {
                const Keyword* kw = matchKeyword(line, p);
                if (kw) {
                    p += std::char_traits<char>::length(kw->text);
                    tok.kind = LexKind::Keyword;
                    tok.tokenCode = kw->code;
                    tok.text = line.substr(start, p - start);
                    tok.length = p - start;
                    result.tokens.push_back(tok);
                    if (kw->flags & KF_Comment) {
                        if (p < line.size()) {
                            LexToken comment;
                            comment.kind = LexKind::Comment;
                            comment.offset = lineStart + p;
                            comment.line = sourceLine;
                            comment.column = columnOf(line, p);
                            comment.text = line.substr(p);
                            comment.length = comment.text.size();
                            result.tokens.push_back(comment);
                        }
                        p = line.size();
                    } else if (kw->flags & KF_Data) {
                        const std::size_t dataStart = p;
                        p = static_cast<std::size_t>(findDataEnd(line, p));
                        LexToken data;
                        data.kind = LexKind::Data;
                        data.offset = lineStart + dataStart;
                        data.length = p - dataStart;
                        data.text = line.substr(dataStart, p - dataStart);
                        data.line = sourceLine;
                        data.column = columnOf(line, dataStart);
                        result.tokens.push_back(data);
                    }
                    continue;
                }
                if (c == '>' || c == '=' || c == '<' || c == '+' || c == '-' || c == '*' || c == '/' ||
                    c == '^' || c == '\\') {
                    ++p;
                    tok.kind = LexKind::Operator;
                } else if (c == ',' || c == ';' || c == ':' || c == '(' || c == ')' || c == '#') {
                    ++p;
                    tok.kind = LexKind::Separator;
                } else if (static_cast<unsigned char>(c) < 0x80) {
                    while (p < line.size()) {
                        const char d = line[p];
                        if (std::isalnum(static_cast<unsigned char>(d)) || d == '_' || d == '$' ||
                            d == '%' || d == '!' || d == '.') {
                            ++p;
                        } else {
                            break;
                        }
                    }
                    if (p == start) ++p;
                    tok.kind = LexKind::Variable;
                } else {
                    std::size_t q = p;
                    uint32_t cp = 0;
                    utf8::decode(line, q, cp);
                    result.diags.error(sourceLine, columnOf(line, p),
                                       "文字列・REM・DATA 以外では非 ASCII 文字を使用できません");
                    p = q;
                    tok.kind = LexKind::Unknown;
                }
            }

            if (tok.text.empty() && p > start) tok.text = line.substr(start, p - start);
            if (tok.length == 0) tok.length = p - start;
            result.tokens.push_back(tok);
        }

        LexToken eol;
        eol.kind = LexKind::EndOfLine;
        eol.offset = lineStart + line.size();
        eol.line = sourceLine;
        eol.column = columnOf(line, line.size());
        result.tokens.push_back(eol);

        if (lineEnd >= text.size()) break;
        if (text[lineEnd] == '\r' && lineEnd + 1 < text.size() && text[lineEnd + 1] == '\n') {
            lineStart = lineEnd + 2;
        } else {
            lineStart = lineEnd + 1;
        }
        ++sourceLine;
    }
    return result;
}

}  // namespace msx
