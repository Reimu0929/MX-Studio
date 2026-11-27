#include "msx/basic_complete.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "msx/basic_lexer.h"
#include "msx/basic_token.h"
#include "msx/profile.h"
#include "msx/utf8.h"

namespace msx {

namespace {

bool isWordByte(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '%' ||
           c == '!' || c == '.' || c == '#';
}

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool startsWithIgnoreCase(const std::string& text, const std::string& prefix) {
    if (prefix.size() > text.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (lowerAscii(text[i]) != lowerAscii(prefix[i])) return false;
    }
    return true;
}

struct Hint {
    const char* keyword;
    const char* text;
};

const Hint kHints[] = {
    {"PRINT", "PRINT [式][;|,]..."},
    {"LPRINT", "LPRINT [式][;|,]..."},
    {"INPUT", "INPUT [\"プロンプト\";] 変数 [, 変数...]"},
    {"LINE INPUT", "LINE INPUT [\"プロンプト\";] 文字変数"},
    {"GOTO", "GOTO 行番号"},
    {"GOSUB", "GOSUB 行番号"},
    {"RETURN", "RETURN [行番号]"},
    {"IF", "IF 条件 THEN 文 [ELSE 文]"},
    {"FOR", "FOR 変数=初期値 TO 終値 [STEP 増分]"},
    {"NEXT", "NEXT [変数]"},
    {"DIM", "DIM 配列(最大添字) [, ...]"},
    {"DEF", "DEF FN 名前(引数)=式"},
    {"LOCATE", "LOCATE [X],[Y]"},
    {"COLOR", "COLOR [前景],[背景],[周辺]"},
    {"SCREEN", "SCREEN [モード],[スプライトサイズ],[キークリック],[ボーレート]"},
    {"OPEN", "OPEN \"ファイル名\" FOR INPUT|OUTPUT AS #番号"},
    {"CLOSE", "CLOSE [#番号 [, ...]]"},
    {"CSAVE", "CSAVE \"名前\"[,1|2]"},
    {"CLOAD", "CLOAD [\"名前\"]"},
    {"SAVE", "SAVE \"ファイル名\"[,A]"},
    {"LOAD", "LOAD \"ファイル名\"[,R]"},
    {"BLOAD", "BLOAD \"ファイル名\"[,R][,オフセット]"},
    {"BSAVE", "BSAVE \"ファイル名\",開始,終了[,実行]"},
    {"CIRCLE", "CIRCLE (X,Y),半径[,色[,開始,終了,比率]]"},
    {"LINE", "LINE [[STEP](X1,Y1)]-(X2,Y2)[,色[,BF]]"},
    {"PSET", "PSET [STEP](X,Y)[,色]"},
    {"PRESET", "PRESET [STEP](X,Y)[,色]"},
    {"PAINT", "PAINT [STEP](X,Y)[,色[,境界色]]"},
    {"DRAW", "DRAW 文字列"},
    {"PLAY", "PLAY 文字列"},
    {"SOUND", "SOUND チャンネル,周波数,音量[,音量変化[,周期]]"},
    {"POKE", "POKE アドレス,値[,値...]"},
    {"VPOKE", "VPOKE アドレス,値"},
    {"OUT", "OUT ポート,値"},
    {"WAIT", "WAIT ポート,マスク[,反転マスク]"},
    {"ON", "ON 式 GOTO|GOSUB 行番号 [, ...]"},
    {"CALL", "CALL 拡張文[(引数,...)]"},
    {"STRING$", "STRING$(個数, 文字)"},
    {"MID$", "MID$(文字列, 開始[, 長さ])"},
    {"INSTR", "INSTR([開始,] 文字列, 検索文字列)"},
    {"POINT", "POINT (X,Y)"},
    {"SPC(", "SPC(個数)"},
    {"TAB(", "TAB(桁)"},
    {"GET", "GET [#番号,] 変数 [, ...]"},
    {"PUT", "PUT [#番号,] 変数 [, ...]"},
};

const char* hintFor(const std::string& word) {
    for (std::size_t i = 0; i < sizeof(kHints) / sizeof(kHints[0]); ++i) {
        if (word == kHints[i].keyword) return kHints[i].text;
    }
    return nullptr;
}

}  // namespace

std::vector<CompletionItem> completeAt(const std::string& utf8Source, std::size_t caret,
                                       const ModelProfile& profile) {
    std::vector<CompletionItem> items;
    if (caret > utf8Source.size()) caret = utf8Source.size();
    caret = utf8::codepointStart(utf8Source, caret);

    LexResult lex = lexSource(utf8Source, profile);
    bool inLiteral = false;
    for (std::size_t i = 0; i < lex.tokens.size(); ++i) {
        const LexToken& t = lex.tokens[i];
        if (t.kind == LexKind::LineNumber) continue;
        if (caret > t.offset && caret <= t.offset + t.length) {
            if (t.kind == LexKind::String || t.kind == LexKind::Comment || t.kind == LexKind::Data) {
                inLiteral = true;
            }
        }
    }
    if (inLiteral) return items;

    const std::size_t lineStart = utf8Source.rfind('\n', caret == 0 ? 0 : caret - 1);
    std::size_t ls = 0;
    if (lineStart != std::string::npos && lineStart < caret) ls = lineStart + 1;
    std::size_t le = utf8Source.find_first_of("\r\n", caret);
    if (le == std::string::npos) le = utf8Source.size();
    const std::string line = utf8Source.substr(ls, le - ls);
    std::size_t localCaret = caret - ls;

    std::size_t prefixStart = localCaret;
    while (prefixStart > 0 && isWordByte(line[prefixStart - 1])) --prefixStart;
    const std::string prefix = line.substr(prefixStart, localCaret - prefixStart);
    const std::size_t absolutePrefix = ls + prefixStart;

    // The token that comes before the word being typed decides the context.
    const LexToken* before = nullptr;
    for (std::size_t i = 0; i < lex.tokens.size(); ++i) {
        const LexToken& t = lex.tokens[i];
        if (t.offset < ls) continue;
        if (t.offset >= absolutePrefix) break;
        if (t.kind == LexKind::EndOfLine) break;
        if (t.kind == LexKind::LineNumber && !before) continue;
        before = &t;
    }

    const bool digitPrefix = !prefix.empty() && std::isdigit(static_cast<unsigned char>(prefix[0]));
    bool lineRefKeyword = false;
    static const char* const kLineRefWords[] = {"GOTO", "GOSUB", "RESTORE", "RUN", "RESUME",
                                                "LIST", "LLIST", "DELETE", "RENUM"};
    if (before && before->kind == LexKind::Keyword) {
        for (std::size_t i = 0; i < sizeof(kLineRefWords) / sizeof(kLineRefWords[0]); ++i) {
            if (before->text == kLineRefWords[i]) {
                lineRefKeyword = true;
                break;
            }
        }
    }

    const bool afterThenElse = before && before->kind == LexKind::Keyword &&
                               (before->text == "THEN" || before->text == "ELSE");

    if (digitPrefix || lineRefKeyword) {
        for (std::size_t i = 0; i < lex.lines.size(); ++i) {
            const std::string label = std::to_string(lex.lines[i].number);
            if (!startsWithIgnoreCase(label, prefix)) continue;
            CompletionItem item;
            item.kind = CompletionItem::Kind::LineNumber;
            item.label = label;
            item.insertText = label;
            item.detail = "行番号";
            items.push_back(item);
        }
        return items;
    }

    bool statementContext = true;
    if (before) {
        statementContext = (before->kind == LexKind::Separator && before->text == ":") ||
                           (before->kind == LexKind::Keyword &&
                            (before->text == "THEN" || before->text == "ELSE"));
    }

    if (afterThenElse && (prefix.empty() || digitPrefix)) {
        for (std::size_t i = 0; i < lex.lines.size(); ++i) {
            CompletionItem item;
            item.kind = CompletionItem::Kind::LineNumber;
            item.label = std::to_string(lex.lines[i].number);
            item.insertText = item.label;
            item.detail = "行番号";
            items.push_back(item);
        }
    }

    std::vector<CompletionItem> keywordsFound;
    std::set<std::string> seen;
    const std::vector<Keyword>& table = keywords();
    for (std::size_t i = 0; i < table.size(); ++i) {
        const Keyword& kw = table[i];
        if (kw.flags & KF_Operator) continue;
        if ((kw.flags & KF_MSX2) && !profile.msx2Commands) continue;
        const bool isFunction = (kw.flags & KF_Function) != 0;
        if (statementContext && isFunction) continue;
        if (!statementContext && !isFunction) continue;
        if (!startsWithIgnoreCase(kw.text, prefix)) continue;
        if (!seen.insert(kw.text).second) continue;
        CompletionItem item;
        item.kind = isFunction ? CompletionItem::Kind::Function : CompletionItem::Kind::Keyword;
        item.label = kw.text;
        item.insertText = kw.text;
        if (isFunction) {
            if (item.insertText.empty() || item.insertText[item.insertText.size() - 1] != '(') {
                item.insertText += "(";
            }
            item.detail = "関数";
        } else {
            item.insertText += " ";
            item.detail = "命令";
        }
        if (kw.flags & KF_Disk) item.detail += "（ディスクBASIC）";
        keywordsFound.push_back(item);
    }
    std::sort(keywordsFound.begin(), keywordsFound.end(),
              [](const CompletionItem& a, const CompletionItem& b) { return a.label < b.label; });
    items.insert(items.end(), keywordsFound.begin(), keywordsFound.end());

    std::set<std::string> variables;
    for (std::size_t i = 0; i < lex.tokens.size(); ++i) {
        const LexToken& t = lex.tokens[i];
        if (t.kind != LexKind::Variable) continue;
        // Skip the word being typed.
        if (t.offset < caret && t.offset + t.length >= caret) continue;
        variables.insert(t.text);
    }
    std::vector<CompletionItem> variableItems;
    for (std::set<std::string>::const_iterator it = variables.begin(); it != variables.end(); ++it) {
        if (!startsWithIgnoreCase(*it, prefix)) continue;
        if (seen.count(*it) != 0) continue;
        CompletionItem item;
        item.kind = CompletionItem::Kind::Variable;
        item.label = *it;
        item.insertText = *it;
        item.detail = "変数";
        variableItems.push_back(item);
    }
    std::sort(variableItems.begin(), variableItems.end(),
              [](const CompletionItem& a, const CompletionItem& b) { return a.label < b.label; });
    items.insert(items.end(), variableItems.begin(), variableItems.end());
    return items;
}

std::string argumentHintAt(const std::string& utf8Source, std::size_t caret, const ModelProfile& profile) {
    if (caret > utf8Source.size()) caret = utf8Source.size();
    caret = utf8::codepointStart(utf8Source, caret);
    LexResult lex = lexSource(utf8Source, profile);
    const LexToken* lastKeyword = nullptr;
    for (std::size_t i = 0; i < lex.tokens.size(); ++i) {
        const LexToken& t = lex.tokens[i];
        if (t.offset >= caret) break;
        if (t.kind == LexKind::Keyword) lastKeyword = &t;
        if (t.kind == LexKind::EndOfLine) break;
    }
    if (!lastKeyword) return std::string();
    const char* hint = hintFor(lastKeyword->text);
    if (!hint && lastKeyword->text == "LINE") hint = hintFor("LINE");
    return hint ? std::string(hint) : std::string();
}

std::string keywordHint(const std::string& keyword) {
    std::string upper;
    upper.reserve(keyword.size());
    for (std::size_t i = 0; i < keyword.size(); ++i) {
        const char c = keyword[i];
        upper += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    const char* hint = hintFor(upper);
    return hint ? std::string(hint) : std::string();
}

}  // namespace msx
