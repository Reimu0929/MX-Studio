#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "msx/basic.h"
#include "msx/basic_complete.h"
#include "msx/basic_lexer.h"
#include "msx/basic_token.h"
#include "msx/bcd.h"
#include "msx/charset.h"
#include "msx/common.h"
#include "msx/profile.h"
#include "msx/tape.h"
#include "msx/utf8.h"
#include "msx/wav.h"

using namespace msx;

namespace {

std::ostream& operator<<(std::ostream& os, const Bytes& bytes) {
    os << '[';
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) os << ' ';
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02X", bytes[i]);
        os << buf;
    }
    os << ']';
    return os;
}

int g_failures = 0;
int g_checks = 0;
std::string g_current;

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        tests().push_back(Entry{name, fn});
    }
    struct Entry {
        std::string name;
        std::function<void()> fn;
    };
    static std::vector<Entry>& tests() {
        static std::vector<Entry> t;
        return t;
    }
};

#define TEST(name)                                    \
    static void test_##name();                        \
    static Registrar reg_##name(#name, test_##name);  \
    static void test_##name()

#define CHECK(cond)                                                                     \
    do {                                                                                \
        ++g_checks;                                                                     \
        if (!(cond)) {                                                                  \
            ++g_failures;                                                               \
            std::cerr << g_current << ": CHECK failed (" << __LINE__ << "): " #cond "\n"; \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                        \
    do {                                                                                      \
        ++g_checks;                                                                           \
        const auto va = (a);                                                                  \
        const auto vb = (b);                                                                  \
        if (!(va == vb)) {                                                                    \
            ++g_failures;                                                                     \
            std::cerr << g_current << ": CHECK_EQ failed (" << __LINE__ << "): " #a " == " #b \
                      << "\n  left: " << va << "\n  right: " << vb << "\n";                   \
        }                                                                                     \
    } while (0)

std::string hexDump(const Bytes& bytes) {
    std::ostringstream os;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) os << ' ';
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02X", bytes[i]);
        os << buf;
    }
    return os.str();
}

Bytes compileImage(const std::string& source, Diagnostics* outDiags = nullptr) {
    CompileOptions options;
    options.profile = &cf2000Profile();
    CompileResult r = compileProgram(source, options);
    if (outDiags) *outDiags = r.diags;
    return r.image;
}

Bytes bodyOf(const Bytes& image) {
    ParseImageResult parsed = parseProgramImage(image, cf2000Profile());
    if (parsed.program.lines.empty()) return Bytes();
    return parsed.program.lines[0].body;
}

Bytes makeWav(const Bytes& pcmBytes, unsigned channels) {
    MemoryWriter writer;
    WavFormat fmt;
    fmt.channels = channels;
    Diagnostics diags;
    WavWriter w;
    if (!w.open(writer, fmt, diags)) {
        std::cerr << "makeWav open failed: " << diags.format();
        return Bytes();
    }
    std::vector<int16_t> samples(pcmBytes.size() / 2);
    if (!pcmBytes.empty()) std::memcpy(&samples[0], &pcmBytes[0], samples.size() * 2);
    const std::size_t frames = samples.size() / channels;
    if (!w.writeFrames(&samples[0], frames, diags)) {
        std::cerr << "makeWav write failed: " << diags.format();
    }
    w.close(diags);
    return writer.data();
}

Bytes renderTape(const CasImage& tape, int baud, FskStats* stats = nullptr) {
    MemoryWriter writer;
    Diagnostics diags;
    FskOptions options;
    options.baud = baud;
    if (!renderTapeToWav(tape, writer, options, diags, stats)) {
        std::cerr << "renderTape failed: " << diags.format() << "\n";
        return Bytes();
    }
    return writer.data();
}

}  // namespace

// ---------------------------------------------------------------------------
TEST(charset_roundtrip) {
    for (int i = 0; i < 256; ++i) {
        const std::string u = charset::byteToUtf8(static_cast<uint8_t>(i));
        Bytes out;
        if (charset::encode(u, out) != charset::EncodeStatus::Ok) {
            // C0 control codes are intentionally kept as-is.
            CHECK(i < 0x20 || i == 0x7F);
            continue;
        }
        CHECK_EQ(out.size(), static_cast<std::size_t>(1));
        if (!out.empty()) {
            // 0x90 and 0xA0 both display U+3000; encoding is canonical 0xA0.
            if (i == 0x90) {
                CHECK_EQ(static_cast<int>(out[0]), 0xA0);
            } else {
                CHECK_EQ(static_cast<int>(out[0]), i);
            }
        }
    }
    const uint8_t kana[] = {0x96, 0xDE, 0xCA, 0xDF};
    CHECK_EQ(charset::decode(kana, sizeof(kana)), std::string("がパ"));
    Bytes encoded;
    CHECK(charset::encode("がパ", encoded) == charset::EncodeStatus::Ok);
    CHECK_EQ(encoded, Bytes({0x96, 0xDE, 0xCA, 0xDF}));

    Bytes vu;
    CHECK(charset::encode("ヴ", vu) == charset::EncodeStatus::Ok);
    CHECK_EQ(vu, Bytes({0xB3, 0xDE}));
    CHECK_EQ(charset::decode(vu), std::string("ヴ"));

    Bytes graphic;
    CHECK(charset::encode("月", graphic) == charset::EncodeStatus::Ok);
    CHECK_EQ(graphic, Bytes({0x01, 0x41}));
    CHECK_EQ(charset::decode(graphic), std::string("月"));

    Bytes bad;
    CHECK(charset::encode("€", bad) == charset::EncodeStatus::UnknownCharacter);
}

TEST(bcd_vectors) {
    Bytes out;
    Diagnostics unused;
    bcd::EncodeResult r = bcd::encode("123456", 6, out);
    CHECK(r.ok);
    CHECK_EQ(out, Bytes({0x46, 0x12, 0x34, 0x56}));
    CHECK_EQ(bcd::decode(&out[0], 6, true), std::string("123456!"));

    out.clear();
    r = bcd::encode("123.456", 6, out);
    CHECK(r.ok);
    CHECK_EQ(out, Bytes({0x43, 0x12, 0x34, 0x56}));
    CHECK_EQ(bcd::decode(&out[0], 6, true), std::string("123.456!"));

    out.clear();
    r = bcd::encode("0.123456", 6, out);
    CHECK(r.ok);
    CHECK_EQ(out, Bytes({0x40, 0x12, 0x34, 0x56}));

    out.clear();
    r = bcd::encode("1234560", 6, out);
    CHECK(r.ok);
    CHECK_EQ(out, Bytes({0x47, 0x12, 0x34, 0x56}));

    out.clear();
    r = bcd::encode("1", 14, out);
    CHECK(r.ok);
    CHECK_EQ(static_cast<int>(out[0]), 0x41);
    CHECK_EQ(bcd::decode(&out[0], 14, true), std::string("1#"));

    out.clear();
    r = bcd::encode("0.1", 14, out);
    CHECK(r.ok);
    CHECK_EQ(static_cast<int>(out[0]), 0x40);
    const std::string text = bcd::decode(&out[0], 14, true);
    CHECK_EQ(text, std::string("0.1#"));
    Bytes again;
    std::string lit = text.substr(0, text.size() - 1);
    CHECK(bcd::encode(lit, 14, again).ok);
    CHECK_EQ(again, out);

    out.clear();
    r = bcd::encode("1E+10", 14, out);
    CHECK(r.ok);
    CHECK_EQ(static_cast<int>(out[0]), 0x4B);
    const std::string sci = bcd::decode(&out[0], 14, true);
    CHECK_EQ(sci, std::string("1E+10#"));
    again.clear();
    CHECK(bcd::encode(sci.substr(0, sci.size() - 1), 14, again).ok);
    CHECK_EQ(again, out);

    out.clear();
    r = bcd::encode("0", 6, out);
    CHECK(r.ok);
    CHECK_EQ(out, Bytes({0x00, 0x00, 0x00, 0x00}));
    CHECK_EQ(bcd::decode(&out[0], 6, true), std::string("0!"));
    Bytes zeroAgain;
    CHECK(bcd::encode("0", 6, zeroAgain).ok);
    CHECK_EQ(zeroAgain, out);
}

TEST(bcd_roundtrip_many) {
    const char* literals[] = {"1",       "9",        "10",       "123456",   "0.5",
                              "0.0001",  "999999",   "123.456",  "0.0123",   "1.5",
                              "3.14159265", "1E-10", "1E+20",   "0.999999", "2.718281828459045",
                              "0",       "0.0"};
    for (std::size_t i = 0; i < sizeof(literals) / sizeof(literals[0]); ++i) {
        for (int pass = 0; pass < 2; ++pass) {
            const int digits = pass == 0 ? 6 : 14;
            Bytes enc;
            bcd::EncodeResult r = bcd::encode(literals[i], digits, enc);
            CHECK(r.ok);
            if (!r.ok) continue;
            const std::string text = bcd::decode(&enc[0], digits, true);
            Bytes enc2;
            const std::string lit = text.substr(0, text.size() - 1);
            CHECK(bcd::encode(lit, digits, enc2).ok);
            CHECK_EQ(enc2, enc);
        }
    }
}

TEST(tokenizer_basic_bytes) {
    Diagnostics diags;
    const Bytes image = compileImage("10 PRINT \"HELLO\"\n", &diags);
    CHECK(!diags.hasErrors());
    CHECK_EQ(hexDump(image), std::string("0D 80 0A 00 91 22 48 45 4C 4C 4F 22 00 00 00"));
}

TEST(tokenizer_numbers) {
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=5\n"))), std::string("41 EF 16"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=12\n"))), std::string("41 EF 0F 0C"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=300\n"))), std::string("41 EF 1C 2C 01"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=&HFF\n"))), std::string("41 EF 0C FF 00"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=&O777\n"))), std::string("41 EF 0B FF 01"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=&B101\n"))), std::string("41 EF 26 42 31 30 31"));
    // Default numeric literal is double precision.
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=0.1\n"))),
             std::string("41 EF 1F 40 10 00 00 00 00 00 00"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=1!\n"))),
             std::string("41 EF 1D 41 10 00 00"));
    // The 7th digit rounds a single-precision constant, as MSX does.
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=12345678!\n"))),
             std::string("41 EF 1D 48 12 34 57"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=9.9999999!\n"))),
             std::string("41 EF 1D 42 10 00 00"));
}

TEST(tokenizer_comments_data_else) {
    CHECK_EQ(hexDump(bodyOf(compileImage("10 'HELLO\n"))), std::string("3A 8F E6 48 45 4C 4C 4F"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 REM HELLO\n"))), std::string("8F 20 48 45 4C 4C 4F"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 DATA 1,2\n"))), std::string("84 20 31 2C 32"));
    const std::string elseBody = hexDump(bodyOf(compileImage("10 IF A THEN 20 ELSE 30\n")));
    CHECK_EQ(elseBody, std::string("8B 41 DA 0E 14 00 3A A1 0E 1E 00"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 IF A THEN 20 'c\n20 END\n"))),
             std::string("8B 41 DA 0E 14 00 3A 8F E6 63"));
}

TEST(tokenizer_word_operators) {
    CHECK_EQ(hexDump(bodyOf(compileImage("10 IF A AND B OR NOT C THEN 10\n20 END\n"))),
             std::string("8B 41 F6 42 F7 E0 43 DA 0E 0A 00"));
    CHECK_EQ(hexDump(bodyOf(compileImage("10 A=B MOD 2 XOR C EQV D IMP E\n"))),
             std::string("41 EF 42 FB 13 F8 43 F9 44 FA 45"));
}

TEST(tokenizer_line_refs) {
    const Bytes image = compileImage("10 GOTO 20\n20 END\n");
    CHECK_EQ(hexDump(bodyOf(image)), std::string("89 0E 14 00"));
    ParseImageResult parsed = parseProgramImage(image, cf2000Profile());
    CHECK_EQ(parsed.program.lines.size(), static_cast<std::size_t>(2));
    CHECK(parsed.program.linksValid);
}

TEST(tokenizer_diagnostics) {
    Diagnostics diags;
    compileImage("10 GOTO 100\n", &diags);
    bool found = false;
    for (std::size_t i = 0; i < diags.items().size(); ++i) {
        if (diags.items()[i].message.find("100") != std::string::npos) found = true;
    }
    CHECK(found);

    Diagnostics dup;
    compileImage("10 PRINT\n10 PRINT\n", &dup);
    CHECK(dup.hasErrors());

    Diagnostics outOfRange;
    compileImage("65530 PRINT\n", &outOfRange);
    CHECK(outOfRange.hasErrors());

    Diagnostics unencodable;
    compileImage("10 PRINT \"€\"\n", &unencodable);
    CHECK(unencodable.hasErrors());

    Diagnostics noLine;
    compileImage("PRINT\n", &noLine);
    CHECK(noLine.hasErrors());
}

TEST(detokenize_roundtrip) {
    const char* sources[] = {
        "10 PRINT \"HELLO\"\n20 GOTO 10\n",
        "10 A=0.1:B=1!:C=&HFF:D=&O17\n20 PRINT A;B;C;D\n",
        "10 'comment\r\n20 REM note\r\n30 DATA 1,2,\"X\"\r\n",
        "10 IF A THEN 20 ELSE 30\n20 END\n30 END\n",
        "10 PRINT \"こんにちは\"\n20 PRINT \"がぱ\"\n",
        "10 ?\"SHORTHAND\"\n20 PRINT TAB(3);\"X\"\n",
        "10 A=0.0:B=0!\n20 PRINT A;B\n",
    };
    for (std::size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        Diagnostics d1;
        const Bytes image = compileImage(sources[i], &d1);
        if (d1.hasErrors()) {
            std::cerr << "compile failed for: " << sources[i] << d1.format();
        }
        CHECK(!d1.hasErrors());
        ParseImageResult parsed = parseProgramImage(image, cf2000Profile());
        CHECK(parsed.ok());
        TextOptions opts;
        Diagnostics d2;
        const std::string text = detokenize(parsed.program, opts, d2);
        Diagnostics d3;
        const Bytes image2 = compileImage(text, &d3);
        CHECK(!d3.hasErrors());
        CHECK_EQ(hexDump(image2), hexDump(image));
        if (hexDump(image2) != hexDump(image)) {
            std::cerr << "source: " << sources[i] << "detokenized: " << text << "\n";
        }
    }
}

TEST(cas_roundtrip) {
    Diagnostics diags;
    const Bytes image = compileImage("10 PRINT \"HELLO\"\n20 END\n");
    const Bytes tapeBody = toTapeBody(image, false);
    CasImage tape = makeBasicTape(tapeBody, "TEST", diags);
    CHECK(!diags.hasErrors());
    CHECK_EQ(tape.blocks.size(), static_cast<std::size_t>(2));
    CHECK_EQ(static_cast<int>(tape.blocks[0].type), 0xD3);

    const Bytes cas = serializeCas(tape, true, diags);
    CHECK_EQ(cas.size() % 8, static_cast<std::size_t>(0));
    CHECK(isCasMagic(&cas[0]));
    Diagnostics pdiags;
    CasImage parsed = parseCas(cas, pdiags);
    CHECK_EQ(parsed.blocks.size(), static_cast<std::size_t>(2));
    CHECK_EQ(parsed.files.size(), static_cast<std::size_t>(1));
    CHECK_EQ(parsed.files[0].name, std::string("TEST  "));
    CHECK(parsed.files[0].valid);
    CHECK_EQ(cas.size(), static_cast<std::size_t>(8 + 16 + 8 + ((tapeBody.size() + 7) / 8) * 8));
}

TEST(cas_ascii_and_binary) {
    Diagnostics diags;
    Bytes ascii;
    const std::string text = "10 PRINT \"A\"\r\n";
    ascii.assign(text.begin(), text.end());
    CasImage asciiTape = makeAsciiTape(ascii, "ABC", diags);
    CHECK_EQ(asciiTape.blocks.size(), static_cast<std::size_t>(2));
    CHECK_EQ(asciiTape.blocks[1].bytes.size(), static_cast<std::size_t>(256));
    const Bytes cas = serializeCas(asciiTape, true, diags);
    CasImage parsed = parseCas(cas, diags);
    CHECK_EQ(parsed.files.size(), static_cast<std::size_t>(1));
    CHECK(parsed.files[0].valid);
    CHECK_EQ(static_cast<int>(parsed.files[0].type), 0xEA);

    Bytes code(16, 0xAA);
    CasImage bin = makeBinaryTape(code, 0x8000, 0x800F, 0x8000, "BIN", diags);
    CHECK(!diags.hasErrors());
    CasImage parsedBin = parseCas(serializeCas(bin, true, diags), diags);
    CHECK_EQ(parsedBin.files.size(), static_cast<std::size_t>(1));
    CHECK_EQ(static_cast<int>(parsedBin.files[0].start), 0x8000);
    CHECK_EQ(static_cast<int>(parsedBin.files[0].end), 0x800F);
    CHECK(parsedBin.files[0].valid);

    Diagnostics bad;
    makeBinaryTape(Bytes(15, 0xAA), 0x8000, 0x800F, 0x8000, "BIN", bad);
    CHECK(bad.hasErrors());
}

TEST(fsk_roundtrip_basic) {
    Diagnostics diags;
    const Bytes image = compileImage("10 PRINT \"HELLO\"\n20 GOTO 10\n");
    CasImage tape = makeBasicTape(toTapeBody(image, false), "TEST", diags);
    const int bauds[] = {1200, 2400};
    for (int bi = 0; bi < 2; ++bi) {
        FskStats stats;
        const Bytes wav = renderTape(tape, bauds[bi], &stats);
        CHECK(!wav.empty());
        CHECK_EQ(wav.size(), static_cast<std::size_t>(44 + stats.samples * 2));
        MemoryReader reader(wav);
        Diagnostics dd;
        DecodeOptions options;
        DecodeResult decoded = demodulateWav(reader, options, dd);
        CHECK(!dd.hasErrors());
        CHECK(decoded.clean);
        CHECK_EQ(decoded.blocks.size(), static_cast<std::size_t>(2));
        if (decoded.blocks.size() == 2) {
            CHECK_EQ(decoded.blocks[0].baud, bauds[bi]);
            CHECK(decoded.blocks[0].header);
            CHECK_EQ(decoded.blocks[0].bytes.size(), static_cast<std::size_t>(16));
            CHECK_EQ(decoded.blocks[0].bytes[0], static_cast<uint8_t>(0xD3));
            CHECK_EQ(decoded.blocks[1].bytes, tape.blocks[1].bytes);
            CHECK_EQ(decoded.blocks[1].frameErrors, 0);
        }
    }
}

TEST(fsk_impairments) {
    Diagnostics diags;
    CasImage tape = makeBasicTape(toTapeBody(compileImage("10 PRINT \"ABC\"\n"), false), "T", diags);
    const Bytes wav = renderTape(tape, 2400);
    CHECK(!wav.empty());

    // Extract PCM.
    Diagnostics rd;
    MemoryReader mr(wav);
    WavReader wr;
    CHECK(wr.open(mr, rd));
    std::vector<int16_t> pcm(static_cast<std::size_t>(wr.dataFrames()));
    std::size_t got = 0;
    while (got < pcm.size()) {
        const std::size_t n = wr.readFrames(&pcm[got], pcm.size() - got, rd);
        if (n == 0) break;
        got += n;
    }
    CHECK_EQ(got, pcm.size());

    struct Case {
        const char* name;
        double scale;
        int offset;
        bool invert;
        int noise;
    };
    const Case cases[] = {
        {"invert", 1.0, 0, true, 0},
        {"offset", 1.0, 3000, false, 0},
        {"quiet", 0.25, 0, false, 0},
        {"noise", 1.0, 0, false, 800},
    };
    for (std::size_t ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ++ci) {
        std::vector<int16_t> altered(pcm.size());
        uint32_t seed = 12345;
        for (std::size_t i = 0; i < pcm.size(); ++i) {
            double v = static_cast<double>(pcm[i]) * cases[ci].scale;
            if (cases[ci].invert) v = -v;
            if (cases[ci].noise) {
                seed = seed * 1664525u + 1013904223u;
                const int n = static_cast<int>((seed >> 16) & 0x7FFF) - 16384;
                v += n * cases[ci].noise / 16384.0;
            }
            v += cases[ci].offset;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            altered[i] = static_cast<int16_t>(std::lround(v));
        }
        Bytes alteredWav = makeWav(Bytes(reinterpret_cast<const uint8_t*>(&altered[0]),
                                         reinterpret_cast<const uint8_t*>(&altered[0]) + altered.size() * 2),
                                   1);
        MemoryReader r2(alteredWav);
        Diagnostics d2;
        DecodeOptions opt;
        DecodeResult res = demodulateWav(r2, opt, d2);
        if (res.blocks.size() != 2 || res.blocks[1].bytes != tape.blocks[1].bytes) {
            std::cerr << "impairment failed: " << cases[ci].name << " blocks=" << res.blocks.size()
                      << " diags=" << d2.format() << "\n";
        }
        CHECK_EQ(res.blocks.size(), static_cast<std::size_t>(2));
        if (res.blocks.size() == 2) CHECK_EQ(res.blocks[1].bytes, tape.blocks[1].bytes);
    }
}

TEST(fsk_dropout_is_not_clean) {
    Diagnostics diags;
    CasImage tape = makeBasicTape(toTapeBody(compileImage("10 PRINT \"DROPOUT-TEST\"\n20 GOTO 10\n"), false),
                                  "DROP", diags);
    const Bytes wav = renderTape(tape, 2400);
    MemoryReader mr(wav);
    WavReader wr;
    Diagnostics rd;
    CHECK(wr.open(mr, rd));
    std::vector<int16_t> pcm(static_cast<std::size_t>(wr.dataFrames()));
    std::size_t got = 0;
    while (got < pcm.size()) {
        const std::size_t n = wr.readFrames(&pcm[got], pcm.size() - got, rd);
        if (n == 0) break;
        got += n;
    }
    const std::size_t start = static_cast<std::size_t>(9.2 * 48000.0);
    const std::size_t length = static_cast<std::size_t>(0.2 * 48000.0);
    for (std::size_t i = start; i < start + length && i < pcm.size(); ++i) pcm[i] = 0;
    Bytes broken = makeWav(Bytes(reinterpret_cast<const uint8_t*>(&pcm[0]),
                                 reinterpret_cast<const uint8_t*>(&pcm[0]) + pcm.size() * 2),
                           1);
    MemoryReader br(broken);
    Diagnostics bd;
    DecodeResult res = demodulateWav(br, DecodeOptions(), bd);
    CHECK(!res.clean);
}

TEST(fsk_speed_change) {
    Diagnostics diags;
    CasImage tape = makeBasicTape(toTapeBody(compileImage("10 PRINT \"SPEED\"\n"), false), "S", diags);
    const Bytes wav = renderTape(tape, 1200);
    MemoryReader mr(wav);
    WavReader wr;
    Diagnostics rd;
    CHECK(wr.open(mr, rd));
    std::vector<int16_t> pcm(static_cast<std::size_t>(wr.dataFrames()));
    std::size_t got = 0;
    while (got < pcm.size()) {
        const std::size_t n = wr.readFrames(&pcm[got], pcm.size() - got, rd);
        if (n == 0) break;
        got += n;
    }
    const double speed = 1.05;
    const std::size_t outFrames = static_cast<std::size_t>(pcm.size() * speed);
    std::vector<int16_t> stretched(outFrames);
    for (std::size_t i = 0; i < outFrames; ++i) {
        const double src = i / speed;
        const std::size_t i0 = static_cast<std::size_t>(src);
        const std::size_t i1 = std::min(i0 + 1, pcm.size() - 1);
        const double frac = src - static_cast<double>(i0);
        stretched[i] = static_cast<int16_t>(std::lround(pcm[i0] * (1.0 - frac) + pcm[i1] * frac));
    }
    Bytes sw = makeWav(Bytes(reinterpret_cast<const uint8_t*>(&stretched[0]),
                             reinterpret_cast<const uint8_t*>(&stretched[0]) + stretched.size() * 2),
                       1);
    MemoryReader r2(sw);
    Diagnostics d2;
    DecodeOptions opt;
    DecodeResult res = demodulateWav(r2, opt, d2);
    CHECK_EQ(res.blocks.size(), static_cast<std::size_t>(2));
    if (res.blocks.size() == 2) {
        CHECK_EQ(res.blocks[1].bytes, tape.blocks[1].bytes);
        CHECK_EQ(res.blocks[1].baud, 1200);
    }
}

TEST(fsk_stereo_and_manual) {
    Diagnostics diags;
    CasImage tape = makeBasicTape(toTapeBody(compileImage("10 PRINT \"STEREO\"\n"), false), "ST", diags);
    const Bytes monoWav = renderTape(tape, 1200);
    MemoryReader mr(monoWav);
    WavReader wr;
    Diagnostics rd;
    CHECK(wr.open(mr, rd));
    std::vector<int16_t> mono(static_cast<std::size_t>(wr.dataFrames()));
    std::size_t got = 0;
    while (got < mono.size()) {
        const std::size_t n = wr.readFrames(&mono[got], mono.size() - got, rd);
        if (n == 0) break;
        got += n;
    }
    std::vector<int16_t> stereo(mono.size() * 2);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        stereo[i * 2] = mono[i];
        stereo[i * 2 + 1] = mono[i];
    }
    Bytes stereoWav = makeWav(Bytes(reinterpret_cast<const uint8_t*>(&stereo[0]),
                                     reinterpret_cast<const uint8_t*>(&stereo[0]) + stereo.size() * 2),
                              2);
    MemoryReader sr(stereoWav);
    Diagnostics sd;
    DecodeOptions so;
    so.channel = 1;
    DecodeResult stereoResult = demodulateWav(sr, so, sd);
    CHECK_EQ(stereoResult.blocks.size(), static_cast<std::size_t>(2));
    if (stereoResult.blocks.size() == 2) CHECK_EQ(stereoResult.blocks[1].bytes, tape.blocks[1].bytes);

    // Manual baud with the very beginning (part of the leader) removed.
    const std::size_t cut = 40000;
    std::vector<int16_t> clipped(mono.begin() + static_cast<std::ptrdiff_t>(cut), mono.end());
    Bytes clippedWav = makeWav(Bytes(reinterpret_cast<const uint8_t*>(&clipped[0]),
                                     reinterpret_cast<const uint8_t*>(&clipped[0]) + clipped.size() * 2),
                               1);
    MemoryReader cr(clippedWav);
    Diagnostics cd;
    DecodeOptions co;
    co.baudOverride = 1200;
    DecodeResult clipResult = demodulateWav(cr, co, cd);
    CHECK_EQ(clipResult.blocks.size(), static_cast<std::size_t>(2));
    if (clipResult.blocks.size() == 2) {
        CHECK_EQ(clipResult.blocks[0].bytes.size(), static_cast<std::size_t>(16));
        CHECK_EQ(clipResult.blocks[1].bytes, tape.blocks[1].bytes);
    }
}

TEST(cas_multiple_and_disk_format) {
    Diagnostics diags;
    CasImage one = makeBasicTape(toTapeBody(compileImage("10 PRINT \"ONE\"\n"), false), "ONE", diags);
    CasImage two = makeBasicTape(toTapeBody(compileImage("10 PRINT \"TWO\"\n"), false), "TWO", diags);
    CasImage both;
    both.blocks = one.blocks;
    both.blocks.insert(both.blocks.end(), two.blocks.begin(), two.blocks.end());
    const Bytes cas = serializeCas(both, true, diags);
    CasImage parsed = parseCas(cas, diags);
    CHECK_EQ(parsed.files.size(), static_cast<std::size_t>(2));
    if (parsed.files.size() == 2) {
        CHECK_EQ(parsed.files[0].name, std::string("ONE   "));
        CHECK_EQ(parsed.files[1].name, std::string("TWO   "));
    }

    const Bytes image = compileImage("10 A=1\n20 GOTO 10\n");
    const Bytes disk = toDiskFormat(image);
    CHECK_EQ(static_cast<int>(disk[0]), 0xFF);
    ParseImageResult diskParsed = parseDiskBasicFile(disk, cf2000Profile());
    CHECK_EQ(diskParsed.program.lines.size(), static_cast<std::size_t>(2));
    CHECK_EQ(hexDump(relinkImage(image, 0x8000)), hexDump(image));

    Bytes ascii(600, 'A');
    CasImage asciiTape = makeAsciiTape(ascii, "BIG", diags);
    CHECK_EQ(asciiTape.blocks.size(), static_cast<std::size_t>(4));  // header + 3 blocks
    CasImage asciiParsed = parseCas(serializeCas(asciiTape, true, diags), diags);
    CHECK_EQ(asciiParsed.files.size(), static_cast<std::size_t>(1));
    CHECK_EQ(asciiParsed.files[0].dataBlocks.size(), static_cast<std::size_t>(3));
}

TEST(detokenize_line_address_reference) {
    // Build a tiny image manually: 10 GOTO (0x0D -> line 20), 20 END.
    Bytes image;
    appendLe16(image, 0x8000 + 4 + 2 + 2 + 3 + 1);
    appendLe16(image, 10);
    image.push_back(0x89);
    image.push_back(0x0D);
    appendLe16(image, 0x8000 + 4 + 2 + 2 + 3 + 1);
    image.push_back(0x00);
    appendLe16(image, 0x8000 + 4 + 2 + 5 + 1);
    appendLe16(image, 20);
    image.push_back(0x81);
    image.push_back(0x00);
    appendLe16(image, 0x0000);
    ParseImageResult parsed = parseProgramImage(image, cf2000Profile());
    CHECK_EQ(parsed.program.lines.size(), static_cast<std::size_t>(2));
    Diagnostics diags;
    TextOptions options;
    options.crlf = false;
    const std::string text = detokenize(parsed.program, options, diags);
    CHECK(text.find("GOTO 20") != std::string::npos);
}

TEST(completion_inside_literal) {
    const std::string src = "10 PRINT \"PRI\"\n";
    std::vector<CompletionItem> items = completeAt(src, 13, cf2000Profile());
    CHECK(items.empty());
}

TEST(wav_validation) {
    // Odd-sized unknown chunk before fmt must be skipped with padding.
    Bytes chunk(5, 0x55);
    Bytes wav;
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    const uint32_t total = 4 + 8 + 5 + 1 + 24 + 8 + 4;
    appendLe32(wav, total);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E'});
    wav.insert(wav.end(), {'J', 'U', 'N', 'K'});
    appendLe32(wav, static_cast<uint32_t>(chunk.size()));
    wav.insert(wav.end(), chunk.begin(), chunk.end());
    wav.push_back(0);  // pad
    wav.insert(wav.end(), {'f', 'm', 't', ' '});
    appendLe32(wav, 16);
    appendLe16(wav, 1);
    appendLe16(wav, 1);
    appendLe32(wav, 48000);
    appendLe32(wav, 96000);
    appendLe16(wav, 2);
    appendLe16(wav, 16);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    appendLe32(wav, 4);
    appendLe16(wav, 0x1234);
    appendLe16(wav, 0x5678);
    MemoryReader r(wav);
    Diagnostics d;
    WavReader wr;
    CHECK(wr.open(r, d));
    int16_t sample[2];
    CHECK_EQ(wr.readFrames(sample, 1, d), static_cast<std::size_t>(1));
    CHECK_EQ(static_cast<uint16_t>(sample[0]), static_cast<uint16_t>(0x1234));

    // WAVE_FORMAT_EXTENSIBLE with the PCM subformat must be accepted.
    Bytes ext;
    ext.insert(ext.end(), {'R', 'I', 'F', 'F'});
    appendLe32(ext, 4 + 8 + 40 + 8 + 4);
    ext.insert(ext.end(), {'W', 'A', 'V', 'E'});
    ext.insert(ext.end(), {'f', 'm', 't', ' '});
    appendLe32(ext, 40);
    appendLe16(ext, 0xFFFE);
    appendLe16(ext, 1);
    appendLe32(ext, 48000);
    appendLe32(ext, 96000);
    appendLe16(ext, 2);
    appendLe16(ext, 16);
    appendLe16(ext, 22);      // cbSize
    appendLe16(ext, 16);      // valid bits
    appendLe32(ext, 0);       // channel mask
    const uint8_t pcmGuid[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    ext.insert(ext.end(), pcmGuid, pcmGuid + 16);
    ext.insert(ext.end(), {'d', 'a', 't', 'a'});
    appendLe32(ext, 2);
    appendLe16(ext, 0x0102);
    {
        MemoryReader er(ext);
        Diagnostics ed;
        WavReader ewr;
        CHECK(ewr.open(er, ed));
        int16_t s = 0;
        CHECK_EQ(ewr.readFrames(&s, 1, ed), static_cast<std::size_t>(1));
        CHECK_EQ(static_cast<uint16_t>(s), static_cast<uint16_t>(0x0102));
    }

    // 44.1 kHz must be rejected.
    Bytes good = makeWav(Bytes({0x00, 0x00, 0x00, 0x00}), 1);
    Bytes badRate = good;
    badRate[24] = 0x44;
    badRate[25] = 0xAC;
    badRate[26] = 0x00;
    badRate[27] = 0x00;
    MemoryReader r2(badRate);
    Diagnostics d2;
    WavReader wr2;
    CHECK(!wr2.open(r2, d2));
    CHECK(d2.hasErrors());

    Bytes badBits = good;
    badBits[34] = 8;  // bits per sample
    badBits[35] = 0;
    MemoryReader r3(badBits);
    Diagnostics d3;
    WavReader wr3;
    CHECK(!wr3.open(r3, d3));
    CHECK(d3.hasErrors());
}

TEST(wav_normalize) {
    auto makeSamples = [](std::size_t count, int value) {
        std::vector<int16_t> samples(count, static_cast<int16_t>(value));
        Bytes bytes(samples.size() * 2);
        std::memcpy(&bytes[0], &samples[0], bytes.size());
        return makeWav(bytes, 1);
    };

    // A quiet signal is amplified, but the default +12 dB cap applies.
    {
        const Bytes wav = makeSamples(1000, 1000);
        MemoryReader in(wav);
        MemoryWriter out;
        Diagnostics diags;
        NormalizeStats stats;
        CHECK(normalizeWav(in, out, NormalizeOptions(), diags, &stats));
        CHECK(stats.gainCapped);
        CHECK(stats.outputPeak > 3900 && stats.outputPeak < 4050);
        CHECK(stats.frames == 1000);
        // The output must still be a valid WAV.
        MemoryReader check(out.data());
        Diagnostics rd;
        WavReader reader;
        CHECK(reader.open(check, rd));
        CHECK_EQ(reader.dataFrames(), static_cast<uint64_t>(1000));
    }

    // A normal-level signal is scaled to the -6 dBFS target.
    {
        const Bytes wav = makeSamples(1000, 10000);
        MemoryReader in(wav);
        MemoryWriter out;
        Diagnostics diags;
        NormalizeStats stats;
        CHECK(normalizeWav(in, out, NormalizeOptions(), diags, &stats));
        CHECK(!stats.gainCapped);
        CHECK(stats.outputPeak > 16300 && stats.outputPeak < 16500);
    }

    // Silence cannot be normalized.
    {
        const Bytes wav = makeSamples(100, 0);
        MemoryReader in(wav);
        MemoryWriter out;
        Diagnostics diags;
        CHECK(!normalizeWav(in, out, NormalizeOptions(), diags, nullptr));
        CHECK(diags.hasErrors());
    }
}

TEST(pipeline_with_japanese) {
    Diagnostics diags;
    const std::string source = "10 PRINT \"こんにちは、MSX！\"\n20 GOTO 10\n";
    CompileResult compiled = compileProgram(source, CompileOptions());
    CHECK(!compiled.diags.hasErrors());
    CasImage tape = makeBasicTape(toTapeBody(compiled.image, false), "JP", diags);
    const Bytes wav = renderTape(tape, 2400);
    MemoryReader reader(wav);
    Diagnostics dd;
    DecodeResult decoded = demodulateWav(reader, DecodeOptions(), dd);
    CHECK(!dd.hasErrors());
    CHECK_EQ(decoded.blocks.size(), static_cast<std::size_t>(2));
    CasImage back = decodedToCas(decoded, dd);
    CHECK_EQ(back.files.size(), static_cast<std::size_t>(1));
    const Bytes cas = serializeCas(back, true, dd);
    CasImage parsed = parseCas(cas, dd);
    CHECK_EQ(parsed.files.size(), static_cast<std::size_t>(1));
    if (!parsed.files.empty()) {
        Diagnostics det;
        ParseImageResult img = parseProgramImage(parsed.files[0].payload, cf2000Profile());
        const std::string text = detokenize(img.program, TextOptions(), det);
        CHECK(text.find("こんにちは、MSX!") != std::string::npos);
    }
}

TEST(completion_basics) {
    const std::string src = "10 GOTO 100\n100 A=1:PRINT A\n";
    std::vector<CompletionItem> items = completeAt(src, src.find("100 A") + 3, cf2000Profile());
    bool hasLine = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == CompletionItem::Kind::LineNumber && items[i].label == "100") hasLine = true;
    }
    CHECK(hasLine);

    std::vector<CompletionItem> pri = completeAt("10 PRI", 6, cf2000Profile());
    bool hasPrint = false;
    for (std::size_t i = 0; i < pri.size(); ++i) {
        if (pri[i].label == "PRINT") hasPrint = true;
    }
    CHECK(hasPrint);

    CHECK_EQ(argumentHintAt("10 PRINT ", 9, cf2000Profile()).empty(), false);
}

TEST(lexer_basics) {
    LexResult lex = lexSource("10 A=0.5:REM hi\n20 PRINT \"X\"\n", cf2000Profile());
    CHECK(!lex.diags.hasErrors());
    CHECK_EQ(lex.lines.size(), static_cast<std::size_t>(2));
    bool hasComment = false;
    bool hasFloat = false;
    for (std::size_t i = 0; i < lex.tokens.size(); ++i) {
        if (lex.tokens[i].kind == LexKind::Comment) hasComment = true;
        if (lex.tokens[i].kind == LexKind::Float) hasFloat = true;
    }
    CHECK(hasComment);
    CHECK(hasFloat);
}

TEST(token_table_lookup) {
    const Keyword* abs = keywordByCode(0xFF86);
    CHECK(abs != nullptr && std::string(abs->text) == "ABS");
    const Keyword* print = keywordByCode(0x91);
    CHECK(print != nullptr && std::string(print->text) == "PRINT");
    CHECK(keywordByCode(0x8091) == nullptr);
}

int main() {
    for (std::size_t i = 0; i < Registrar::tests().size(); ++i) {
        g_current = Registrar::tests()[i].name;
        const int before = g_failures;
        Registrar::tests()[i].fn();
        std::cout << (g_failures == before ? "[ OK ] " : "[FAIL] ") << g_current << "\n";
    }
    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
