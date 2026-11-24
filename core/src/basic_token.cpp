#include "msx/basic_token.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace msx {

namespace {

const Keyword kKeywords[] = {
    // Operators
    {">", 0xEE, KF_Operator},
    {"=", 0xEF, KF_Operator},
    {"<", 0xF0, KF_Operator},
    {"+", 0xF1, KF_Operator},
    {"-", 0xF2, KF_Operator},
    {"*", 0xF3, KF_Operator},
    {"/", 0xF4, KF_Operator},
    {"^", 0xF5, KF_Operator},
    {"\\", 0xFC, KF_Operator},

    // Functions
    {"ABS", 0xFF86, KF_Function},
    {"ASC", 0xFF95, KF_Function},
    {"ATN", 0xFF8E, KF_Function},
    {"ATTR$", 0xE9, KF_Function | KF_MSX2},
    {"BIN$", 0xFF9D, KF_Function},
    {"CDBL", 0xFFA0, KF_Function},
    {"CHR$", 0xFF96, KF_Function},
    {"CINT", 0xFF9E, KF_Function},
    {"COS", 0xFF8C, KF_Function},
    {"CSNG", 0xFF9F, KF_Function},
    {"CSRLIN", 0xE8, KF_Function},
    {"CVD", 0xFFAA, KF_Function | KF_Disk},
    {"CVI", 0xFFA8, KF_Function | KF_Disk},
    {"CVS", 0xFFA9, KF_Function | KF_Disk},
    {"DSKF", 0xFFA6, KF_Function | KF_Disk},
    {"DSKI$", 0xEA, KF_Function | KF_Disk},
    {"EOF", 0xFFAB, KF_Function | KF_Disk},
    {"ERL", 0xE1, KF_Function},
    {"ERR", 0xE2, KF_Function},
    {"EXP", 0xFF8B, KF_Function},
    {"FIX", 0xFFA1, KF_Function},
    {"FPOS", 0xFFA7, KF_Function | KF_Disk},
    {"FRE", 0xFF8F, KF_Function},
    {"HEX$", 0xFF9B, KF_Function},
    {"INKEY$", 0xEC, KF_Function},
    {"INP", 0xFF90, KF_Function},
    {"INSTR", 0xE5, KF_Function},
    {"INT", 0xFF85, KF_Function},
    {"LEFT$", 0xFF81, KF_Function},
    {"LEN", 0xFF92, KF_Function},
    {"LOC", 0xFFAC, KF_Function | KF_Disk},
    {"LOF", 0xFFAD, KF_Function | KF_Disk},
    {"LOG", 0xFF8A, KF_Function},
    {"LPOS", 0xFF9C, KF_Function},
    {"MID$", 0xFF83, KF_Function},
    {"MKD$", 0xFFB0, KF_Function | KF_Disk},
    {"MKI$", 0xFFAE, KF_Function | KF_Disk},
    {"MKS$", 0xFFAF, KF_Function | KF_Disk},
    {"OCT$", 0xFF9A, KF_Function},
    {"PAD", 0xFFA5, KF_Function},
    {"PDL", 0xFFA4, KF_Function},
    {"PEEK", 0xFF97, KF_Function},
    {"POINT", 0xED, KF_Function},
    {"POS", 0xFF91, KF_Function},
    {"RIGHT$", 0xFF82, KF_Function},
    {"RND", 0xFF88, KF_Function},
    {"SGN", 0xFF84, KF_Function},
    {"SIN", 0xFF89, KF_Function},
    {"SPACE$", 0xFF99, KF_Function},
    {"SPC(", 0xDF, KF_Function | KF_Paren},
    {"SQR", 0xFF87, KF_Function},
    {"STICK", 0xFFA2, KF_Function},
    {"STR$", 0xFF93, KF_Function},
    {"STRIG", 0xFFA3, KF_Function},
    {"TAB(", 0xDB, KF_Function | KF_Paren},
    {"TAN", 0xFF8D, KF_Function},
    {"USR", 0xDD, KF_Function},
    {"VAL", 0xFF94, KF_Function},
    {"VARPTR", 0xE7, KF_Function},
    {"VPEEK", 0xFF98, KF_Function},

    // Statements and commands
    {"AND", 0xF6, KF_Operator},
    {"AUTO", 0xA9, KF_Statement},
    {"BASE", 0xC9, KF_Function | KF_MSX2},
    {"BEEP", 0xC0, KF_Statement},
    {"BLOAD", 0xCF, KF_Statement},
    {"BSAVE", 0xD0, KF_Statement},
    {"CALL", 0xCA, KF_Statement},
    {"CIRCLE", 0xBC, KF_Statement},
    {"CLEAR", 0x92, KF_Statement},
    {"CLOAD", 0x9B, KF_Statement},
    {"CLOSE", 0xB4, KF_Statement | KF_Disk},
    {"CLS", 0x9F, KF_Statement},
    {"CMD", 0xD7, KF_Statement},
    {"COLOR", 0xBD, KF_Statement},
    {"CONT", 0x99, KF_Statement},
    {"COPY", 0xD6, KF_Statement},
    {"CSAVE", 0x9A, KF_Statement},
    {"DATA", 0x84, KF_Statement | KF_Data},
    {"DEF", 0x97, KF_Statement},
    {"DEFDBL", 0xAE, KF_Statement},
    {"DEFINT", 0xAC, KF_Statement},
    {"DEFSNG", 0xAD, KF_Statement},
    {"DEFSTR", 0xAB, KF_Statement},
    {"DELETE", 0xA8, KF_Statement | KF_LineRef},
    {"DIM", 0x86, KF_Statement},
    {"DRAW", 0xBE, KF_Statement},
    {"DSKO$", 0xD1, KF_Statement | KF_Disk},
    {"ELSE", 0x00A1, KF_Statement | KF_PrependColon | KF_ThenElse},
    {"END", 0x81, KF_Statement},
    {"EQV", 0xF9, KF_Operator},
    {"ERASE", 0xA5, KF_Statement},
    {"ERROR", 0xA6, KF_Statement},
    {"FIELD", 0xB1, KF_Statement | KF_Disk},
    {"FILES", 0xB7, KF_Statement | KF_Disk},
    {"FN", 0xDE, KF_Statement},
    {"FOR", 0x82, KF_Statement},
    {"GET", 0xB2, KF_Statement},
    {"GOSUB", 0x8D, KF_Statement | KF_LineRef},
    {"GOTO", 0x89, KF_Statement | KF_LineRef},
    {"IF", 0x8B, KF_Statement},
    {"IMP", 0xFA, KF_Operator},
    {"INPUT", 0x85, KF_Statement},
    {"IPL", 0xD5, KF_Statement | KF_MSX2 | KF_Disk},
    {"KEY", 0xCC, KF_Statement},
    {"KILL", 0xD4, KF_Statement | KF_Disk},
    {"LET", 0x88, KF_Statement},
    {"LFILES", 0xBB, KF_Statement | KF_Disk},
    {"LINE", 0xAF, KF_Statement},
    {"LIST", 0x93, KF_Statement | KF_LineRef},
    {"LLIST", 0x9E, KF_Statement | KF_LineRef},
    {"LOAD", 0xB5, KF_Statement},
    {"LOCATE", 0xD8, KF_Statement},
    {"LPRINT", 0x9D, KF_Statement},
    {"LSET", 0xB8, KF_Statement | KF_Disk},
    {"MAX", 0xCD, KF_Function | KF_MSX2},
    {"MERGE", 0xB6, KF_Statement},
    {"MOD", 0xFB, KF_Operator},
    {"MOTOR", 0xCE, KF_Statement},
    {"NAME", 0xD3, KF_Statement | KF_MSX2 | KF_Disk},
    {"NEW", 0x94, KF_Statement},
    {"NEXT", 0x83, KF_Statement},
    {"NOT", 0xE0, KF_Operator},
    {"OFF", 0xEB, KF_Statement},
    {"ON", 0x95, KF_Statement},
    {"OPEN", 0xB0, KF_Statement | KF_Disk},
    {"OR", 0xF7, KF_Operator},
    {"OUT", 0x9C, KF_Statement},
    {"PAINT", 0xBF, KF_Statement},
    {"PLAY", 0xC1, KF_Statement},
    {"POKE", 0x98, KF_Statement},
    {"PRESET", 0xC3, KF_Statement},
    {"PRINT", 0x91, KF_Statement},
    {"PSET", 0xC2, KF_Statement},
    {"PUT", 0xB3, KF_Statement},
    {"READ", 0x87, KF_Statement},
    {"REM", 0x8F, KF_Statement | KF_Comment},
    {"RENUM", 0xAA, KF_Statement | KF_LineRef},
    {"RESTORE", 0x8C, KF_Statement | KF_LineRef},
    {"RESUME", 0xA7, KF_Statement | KF_LineRef},
    {"RETURN", 0x8E, KF_Statement},
    {"RSET", 0xB9, KF_Statement | KF_Disk},
    {"RUN", 0x8A, KF_Statement | KF_LineRef},
    {"SAVE", 0xBA, KF_Statement},
    {"SCREEN", 0xC5, KF_Statement},
    {"SET", 0xD2, KF_Statement | KF_MSX2},
    {"SOUND", 0xC4, KF_Statement},
    {"SPRITE", 0xC7, KF_Statement},
    {"STEP", 0xDC, KF_Statement},
    {"STOP", 0x90, KF_Statement},
    {"SWAP", 0xA4, KF_Statement},
    {"THEN", 0xDA, KF_Statement | KF_ThenElse},
    {"TIME", 0xCB, KF_Function},
    {"TO", 0xD9, KF_Statement},
    {"TROFF", 0xA3, KF_Statement},
    {"TRON", 0xA2, KF_Statement},
    {"USING", 0xE4, KF_Statement},
    {"VDP", 0xC8, KF_Function},
    {"VPOKE", 0xC6, KF_Statement},
    {"WAIT", 0x96, KF_Statement},
    {"WIDTH", 0xA0, KF_Statement},
    {"XOR", 0xF8, KF_Operator},
};

struct Table {
    std::vector<Keyword> list;
    Table() {
        list.assign(kKeywords, kKeywords + sizeof(kKeywords) / sizeof(kKeywords[0]));
        std::stable_sort(list.begin(), list.end(), [](const Keyword& a, const Keyword& b) {
            return std::strlen(a.text) > std::strlen(b.text);
        });
    }
};

const Table& table() {
    static const Table t;
    return t;
}

char upperAscii(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

}  // namespace

const std::vector<Keyword>& keywords() { return table().list; }

const Keyword* matchKeyword(const std::string& text, std::size_t position) {
    const std::vector<Keyword>& list = table().list;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Keyword& kw = list[i];
        // Single-character punctuation operators are handled by the scanner's
        // switch statement; word operators (AND/OR/NOT/MOD/...) must match
        // here like any other keyword.
        if ((kw.flags & KF_Operator) && std::strlen(kw.text) == 1) continue;
        const std::size_t len = std::strlen(kw.text);
        if (position + len > text.size()) continue;
        bool ok = true;
        for (std::size_t k = 0; k < len; ++k) {
            if (upperAscii(text[position + k]) != kw.text[k]) {
                ok = false;
                break;
            }
        }
        if (ok) return &kw;
    }
    return nullptr;
}

const Keyword* keywordByCode(uint16_t code) {
    const std::vector<Keyword>& list = table().list;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Keyword& kw = list[i];
        uint16_t stored = kw.code;
        if (stored & 0xFF00) {
            if ((stored & 0xFF00) != 0xFF00) continue;  // ELSE 0x00A1
            if (code != stored) continue;
        } else if (code != stored) {
            continue;
        }
        return &kw;
    }
    return nullptr;
}

std::string tokenCodeText(uint16_t code) {
    char buf[16];
    if (code > 0xFF) std::snprintf(buf, sizeof(buf), "FF%02X", code & 0xFF);
    else std::snprintf(buf, sizeof(buf), "%02X", code);
    return buf;
}

}  // namespace msx
