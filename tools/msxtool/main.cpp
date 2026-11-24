#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <streambuf>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "msx/basic.h"
#include "msx/charset.h"
#include "msx/common.h"
#include "msx/profile.h"
#include "msx/tape.h"
#include "msx/utf8.h"
#include "msx/wav.h"

using namespace msx;

namespace {

#ifdef _WIN32
// The Windows console defaults to the OEM code page (932 on Japanese
// systems), while diagnostics are UTF-8.  Convert each complete line to
// UTF-16 and write it with WriteConsoleW.  If stdout/stderr is redirected,
// WriteConsoleW fails and the raw UTF-8 bytes are written instead.
class Utf8ConsoleBuf : public std::streambuf {
public:
    explicit Utf8ConsoleBuf(HANDLE handle) : handle_(handle) {}

protected:
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
        const char c = traits_type::to_char_type(ch);
        buffer_.push_back(c);
        if (c == '\n' || buffer_.size() >= 4096) sync();
        return ch;
    }

    int sync() override {
        if (buffer_.empty()) return 0;
        bool written = false;
        const int wlen = MultiByteToWideChar(CP_UTF8, 0, buffer_.data(),
                                             static_cast<int>(buffer_.size()), nullptr, 0);
        if (wlen > 0) {
            std::wstring wide(static_cast<std::size_t>(wlen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, buffer_.data(), static_cast<int>(buffer_.size()),
                                &wide[0], wlen);
            DWORD count = 0;
            written = WriteConsoleW(handle_, wide.data(), static_cast<DWORD>(wide.size()), &count,
                                    nullptr) != 0;
        }
        if (!written && handle_ != INVALID_HANDLE_VALUE) {
            DWORD count = 0;
            WriteFile(handle_, buffer_.data(), static_cast<DWORD>(buffer_.size()), &count, nullptr);
        }
        buffer_.clear();
        return 0;
    }

private:
    HANDLE handle_;
    std::string buffer_;
};

struct ConsoleFlushGuard {
    ~ConsoleFlushGuard() {
        std::cout.flush();
        std::cerr.flush();
    }
};
#endif

const char* kVersion = "0.1.0";

void printUsage() {
    std::cout
        << "msxtool " << kVersion << " - MSX CF-2000 tape utility\n\n"
        << "使い方:\n"
        << "  msxtool bas2cas  <in.bas> -o <out.cas> [--name NAME] [--ascii]\n"
        << "  msxtool bas2wav  <in.bas> -o <out.wav> [--name NAME] [--baud 1200|2400] [--volume 1-100] [--ascii]\n"
        << "  msxtool cas2wav  <in.cas> -o <out.wav> [--baud 1200|2400] [--volume 1-100]\n"
        << "  msxtool wav2cas  <in.wav> -o <out.cas> [--channel 0|1] [--baud auto|1200|2400]\n"
        << "  msxtool cas2bas  <in.cas> -o <out.bas> [--index N]\n"
        << "  msxtool wav2bas  <in.wav> -o <out.bas> [--channel 0|1] [--baud auto|1200|2400]\n"
        << "  msxtool cas2bin  <in.cas> -o <out.bin> [--index N]\n"
        << "  msxtool normalize <in.wav> -o <out.wav> [--target -6] [--max-gain 12]\n"
        << "  msxtool casinfo  <in.cas>\n"
        << "  msxtool validate <in.bas>\n"
        << "  msxtool tokenize <in.bas> -o <out.bin> [--tape]\n"
        << "  msxtool detokenize <in.bin> -o <out.bas>\n"
        << "  msxtool selftest\n"
        << "  msxtool version\n\n"
        << "ファイル名は MSX の 6 バイト制限に従います（超過はエラー、切り詰めません）。\n"
        << "BAUD の 1/2 は CSAVE のパラメータ（1=1200、2=2400）としても受け付けます。\n";
}

struct Options {
    std::string input;
    std::string output;
    std::string name;
    std::string baud = "auto";
    std::string channel = "-1";
    std::string index = "0";
    std::string volume = "100";
    std::string targetDbfs = "-6";
    std::string maxGainDb = "12";
    bool ascii = false;
    bool tape = false;
    bool lf = false;
    bool verbose = false;
    bool force = false;
};

bool parseOptions(int argc, char** argv, Options& opt) {
    std::vector<std::string> positional;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) return false;
            opt.output = argv[++i];
        } else if (arg == "--name") {
            if (i + 1 >= argc) return false;
            opt.name = argv[++i];
        } else if (arg == "--baud" || arg == "--speed") {
            if (i + 1 >= argc) return false;
            opt.baud = argv[++i];
        } else if (arg == "--channel") {
            if (i + 1 >= argc) return false;
            opt.channel = argv[++i];
        } else if (arg == "--index") {
            if (i + 1 >= argc) return false;
            opt.index = argv[++i];
        } else if (arg == "--volume") {
            if (i + 1 >= argc) return false;
            opt.volume = argv[++i];
        } else if (arg == "--target") {
            if (i + 1 >= argc) return false;
            opt.targetDbfs = argv[++i];
        } else if (arg == "--max-gain") {
            if (i + 1 >= argc) return false;
            opt.maxGainDb = argv[++i];
        } else if (arg == "--ascii") {
            opt.ascii = true;
        } else if (arg == "--tape") {
            opt.tape = true;
        } else if (arg == "--lf") {
            opt.lf = true;
        } else if (arg == "--verbose" || arg == "-v") {
            opt.verbose = true;
        } else if (arg == "--force" || arg == "-y" || arg == "--overwrite") {
            opt.force = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "不明なオプション: " << arg << "\n";
            return false;
        } else {
            positional.push_back(arg);
        }
    }
    if (!positional.empty()) opt.input = positional[0];
    if (positional.size() > 1 && opt.output.empty()) opt.output = positional[1];
    return true;
}

int amplitudeForVolume(const std::string& text, bool& ok) {
    const int percent = std::atoi(text.c_str());
    ok = percent >= 1 && percent <= 100;
    if (!ok) return 0;
    int amplitude = 14745 * percent / 100;
    if (amplitude < 1) amplitude = 1;
    if (amplitude > 14745) amplitude = 14745;
    return amplitude;
}

int baudValue(const std::string& text, int fallback) {
    if (text.empty() || text == "auto") return fallback;
    if (text == "1") return 1200;
    if (text == "2") return 2400;
    const int value = std::atoi(text.c_str());
    return (value == 1200 || value == 2400) ? value : -1;
}

std::string baseName(const std::string& path) {
    std::size_t slash = path.find_last_of("/\\");
    std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = file.find_last_of('.');
    if (dot != std::string::npos) file = file.substr(0, dot);
    return file;
}

void printDiags(const Diagnostics& diags, std::ostream& os) {
    for (std::size_t i = 0; i < diags.items().size(); ++i) {
        const Diagnostic& d = diags.items()[i];
        if (d.line > 0) {
            os << d.line;
            if (d.column > 0) os << ':' << d.column;
            os << ": ";
        } else if (d.sample > 0) {
            os << "sample " << d.sample << ": ";
        }
        switch (d.severity) {
            case Severity::Error: os << "error: "; break;
            case Severity::Warning: os << "warning: "; break;
            case Severity::Info: os << "info: "; break;
        }
        os << d.message << "\n";
    }
}

bool readText(const std::string& path, std::string& text) {
    Bytes bytes;
    std::string error;
    if (!readWholeFile(path, bytes, error)) {
        std::cerr << "error: " << error << "\n";
        return false;
    }
    text.assign(bytes.empty() ? "" : reinterpret_cast<const char*>(&bytes[0]), bytes.size());
    return true;
}

bool outputAllowed(const std::string& path, bool force) {
    StdFileReader existing(path);
    if (existing.isOpen() && !force) {
        std::cerr << "error: 出力先が既に存在します: " << path
                  << "\n       上書きする場合は --force を指定してください。\n";
        return false;
    }
    return true;
}

bool writeBytes(const std::string& path, const Bytes& bytes) {
    std::string error;
    if (!writeWholeFile(path, bytes, error)) {
        std::cerr << "error: " << error << "\n";
        return false;
    }
    return true;
}

bool readBytes(const std::string& path, Bytes& bytes) {
    std::string error;
    if (!readWholeFile(path, bytes, error)) {
        std::cerr << "error: " << error << "\n";
        return false;
    }
    return true;
}

std::string defaultName(const Options& opt) {
    if (!opt.name.empty()) return opt.name;
    return baseName(opt.input);
}

bool buildSourceTape(const Options& opt, CasImage& tape, Bytes& imageOut) {
    std::string source;
    if (!readText(opt.input, source)) return false;
    std::string text;
    utf8::stripBom(source, text);

    Diagnostics diags;
    if (opt.ascii) {
        Bytes encoded;
        charset::EncodeError error;
        if (charset::encode(text, encoded, &error) != charset::EncodeStatus::Ok) {
            std::cerr << "error: MSX 文字コードに変換できない文字があります (U+" << std::hex << error.codePoint
                      << std::dec << ")\n";
            return false;
        }
        // MSX ASCII files use CR+LF and end with Ctrl-Z.
        Bytes withCrlf;
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            const uint8_t c = encoded[i];
            if (c == '\r') {
                withCrlf.push_back('\r');
                if (i + 1 < encoded.size() && encoded[i + 1] == '\n') {
                    withCrlf.push_back('\n');
                    ++i;
                }
            } else if (c == '\n') {
                withCrlf.push_back('\r');
                withCrlf.push_back('\n');
            } else {
                withCrlf.push_back(c);
            }
        }
        if (std::find(withCrlf.begin(), withCrlf.end(), 0x1A) != withCrlf.end()) {
            std::cerr << "error: ASCII ファイルに 0x1A (EOF) を含めることはできません\n";
            return false;
        }
        if (withCrlf.empty() || withCrlf[withCrlf.size() - 1] != 0x1A) withCrlf.push_back(0x1A);
        tape = makeAsciiTape(withCrlf, defaultName(opt), diags);
    } else {
        CompileOptions compile;
        compile.profile = &cf2000Profile();
        CompileResult result = compileProgram(text, compile);
        printDiags(result.diags, std::cerr);
        if (result.diags.hasErrors()) return false;
        imageOut = result.image;
        tape = makeBasicTape(toTapeBody(result.image, false), defaultName(opt), diags);
    }
    printDiags(diags, std::cerr);
    return !diags.hasErrors();
}

int cmdBas2Cas(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力と -o 出力を指定してください\n";
        return 1;
    }
    CasImage tape;
    Bytes image;
    if (!buildSourceTape(opt, tape, image)) return 2;
    Diagnostics diags;
    const Bytes cas = serializeCas(tape, true, diags);
    printDiags(diags, std::cerr);
    if (diags.hasErrors()) return 2;
    if (!outputAllowed(opt.output, opt.force)) return 1;
    if (!writeBytes(opt.output, cas)) return 1;
    std::cout << "wrote " << opt.output << " (" << cas.size() << " bytes, " << tape.files.size()
              << " file)\n";
    return 0;
}

int cmdBas2Wav(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力と -o 出力を指定してください\n";
        return 1;
    }
    const int baud = baudValue(opt.baud, 1200);
    if (baud < 0) {
        std::cerr << "error: --baud は 1200 または 2400 を指定してください\n";
        return 1;
    }
    CasImage tape;
    Bytes image;
    if (!buildSourceTape(opt, tape, image)) return 2;
    if (!outputAllowed(opt.output, opt.force)) return 1;
    StdFileWriter writer(opt.output);
    if (!writer.isOpen()) {
        std::cerr << "error: 出力ファイルを作成できません: " << opt.output << "\n";
        return 1;
    }
    bool volumeOk = false;
    const int amplitude = amplitudeForVolume(opt.volume, volumeOk);
    if (!volumeOk) {
        std::cerr << "error: --volume は 1～100 の範囲で指定してください\n";
        return 1;
    }
    Diagnostics diags;
    FskOptions options;
    options.baud = baud;
    options.amplitude = amplitude;
    FskStats stats;
    if (!renderTapeToWav(tape, writer, options, diags, &stats)) {
        printDiags(diags, std::cerr);
        return 2;
    }
    printDiags(diags, std::cerr);
    std::cout << "wrote " << opt.output << " (" << stats.samples << " samples, "
              << (static_cast<double>(stats.samples) / 48000.0) << " s, " << baud << " baud, "
              << "volume " << opt.volume << "%)\n";
    return 0;
}

int cmdCas2Wav(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力と -o 出力を指定してください\n";
        return 1;
    }
    const int baud = baudValue(opt.baud, 1200);
    if (baud < 0) {
        std::cerr << "error: --baud は 1200 または 2400 を指定してください\n";
        return 1;
    }
    Bytes cas;
    if (!readBytes(opt.input, cas)) return 1;
    Diagnostics diags;
    CasImage tape = parseCas(cas, diags);
    printDiags(diags, std::cerr);
    if (tape.blocks.empty() || diags.hasErrors()) return 2;
    if (!outputAllowed(opt.output, opt.force)) return 1;
    StdFileWriter writer(opt.output);
    if (!writer.isOpen()) {
        std::cerr << "error: 出力ファイルを作成できません: " << opt.output << "\n";
        return 1;
    }
    bool volumeOk = false;
    const int amplitude = amplitudeForVolume(opt.volume, volumeOk);
    if (!volumeOk) {
        std::cerr << "error: --volume は 1～100 の範囲で指定してください\n";
        return 1;
    }
    FskOptions options;
    options.baud = baud;
    options.amplitude = amplitude;
    FskStats stats;
    if (!renderTapeToWav(tape, writer, options, diags, &stats)) {
        printDiags(diags, std::cerr);
        return 2;
    }
    std::cout << "wrote " << opt.output << " (" << stats.samples << " samples, " << baud
              << " baud, volume " << opt.volume << "%)\n";
    return 0;
}

DecodeResult decodeWavFile(const std::string& path, const Options& opt, Diagnostics& diags) {
    StdFileReader reader(path);
    if (!reader.isOpen()) {
        diags.error("WAV を開けません: " + path);
        return DecodeResult();
    }
    DecodeOptions options;
    options.channel = std::atoi(opt.channel.c_str());
    const int baud = baudValue(opt.baud, 0);
    if (baud < 0) {
        diags.error("--baud は auto、1200、2400 のいずれかにしてください");
        return DecodeResult();
    }
    options.baudOverride = baud;
    return demodulateWav(reader, options, diags);
}

int cmdWav2Cas(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力と -o 出力を指定してください\n";
        return 1;
    }
    Diagnostics diags;
    DecodeResult decoded = decodeWavFile(opt.input, opt, diags);
    printDiags(diags, std::cerr);
    if (decoded.blocks.empty()) return 2;
    if (!decoded.clean) {
        std::cerr << "error: フレームエラーまたは途中切れがあるため、CAS を書き出しません。\n"
                     "       WAV を確認し、必要なら --baud 1200/2400 を指定して再試行してください。\n";
        return 2;
    }
    CasImage image = decodedToCas(decoded, diags);
    const Bytes cas = serializeCas(image, true, diags);
    if (!outputAllowed(opt.output, opt.force)) return 1;
    if (!writeBytes(opt.output, cas)) return 1;
    std::cout << "wrote " << opt.output << " (" << image.blocks.size() << " blocks, "
              << image.files.size() << " files, " << cas.size() << " bytes)\n";
    return 0;
}

int selectFileIndex(const CasImage& image, const std::string& indexText, TapeFileType want) {
    if (indexText != "0" && !indexText.empty()) {
        const int idx = std::atoi(indexText.c_str());
        if (idx < 0 || static_cast<std::size_t>(idx) >= image.files.size()) return -1;
        return idx;
    }
    for (std::size_t i = 0; i < image.files.size(); ++i) {
        if (image.files[i].type == want) return static_cast<int>(i);
    }
    return -1;
}

std::string asciiPayloadToText(const Bytes& payload, bool lf) {
    std::size_t eof = payload.size();
    for (std::size_t i = 0; i < payload.size(); ++i) {
        if (payload[i] == 0x1A) {
            eof = i;
            break;
        }
    }
    Bytes body(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(eof));
    std::string text = charset::decode(body);
    if (lf) {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
            out.push_back(text[i]);
        }
        text = out;
    }
    return text;
}

int cmdCas2Bas(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力と -o 出力を指定してください\n";
        return 1;
    }
    Bytes cas;
    if (!readBytes(opt.input, cas)) return 1;
    Diagnostics diags;
    CasImage image = parseCas(cas, diags);
    printDiags(diags, std::cerr);
    if (diags.hasErrors()) {
        std::cerr << "error: CAS の構造にエラーがあるため書き出しません\n";
        return 2;
    }
    int idx = -1;
    if (opt.index != "0" && !opt.index.empty()) {
        idx = selectFileIndex(image, opt.index, TapeFileType::Basic);
    } else {
        for (std::size_t i = 0; i < image.files.size(); ++i) {
            if (image.files[i].type == TapeFileType::Basic) {
                idx = static_cast<int>(i);
                break;
            }
        }
        if (idx < 0) {
            for (std::size_t i = 0; i < image.files.size(); ++i) {
                if (image.files[i].type == TapeFileType::Ascii) {
                    idx = static_cast<int>(i);
                    break;
                }
            }
        }
    }
    if (idx < 0) {
        std::cerr << "error: BASIC/ASCII ファイルが見つかりません\n";
        return 2;
    }
    const CasFileInfo& file = image.files[static_cast<std::size_t>(idx)];
    std::string text;
    if (file.type == TapeFileType::Basic) {
        ParseImageResult parsed = parseProgramImage(file.payload, cf2000Profile());
        printDiags(parsed.diags, std::cerr);
        if (!parsed.ok()) {
            std::cerr << "error: BASIC の構造が壊れているため書き出しません\n";
            return 2;
        }
        TextOptions textOptions;
        textOptions.crlf = !opt.lf;
        text = detokenize(parsed.program, textOptions, diags);
        printDiags(diags, std::cerr);
    } else if (file.type == TapeFileType::Ascii) {
        text = asciiPayloadToText(file.payload, opt.lf);
    } else {
        std::cerr << "error: 指定ファイルは BASIC/ASCII ではありません\n";
        return 2;
    }
    Bytes out(text.begin(), text.end());
    if (!writeBytes(opt.output, out)) return 1;
    std::cout << "wrote " << opt.output << " (" << out.size() << " bytes)\n";
    return 0;
}

int cmdWav2Bas(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力と -o 出力を指定してください\n";
        return 1;
    }
    Diagnostics diags;
    DecodeResult decoded = decodeWavFile(opt.input, opt, diags);
    printDiags(diags, std::cerr);
    if (decoded.blocks.empty()) return 2;
    if (!decoded.clean) {
        std::cerr << "error: フレームエラーまたは途中切れがあるため、BASIC を書き出しません。\n";
        return 2;
    }
    CasImage image = decodedToCas(decoded, diags);
    int idx = -1;
    for (std::size_t i = 0; i < image.files.size(); ++i) {
        if (image.files[i].type == TapeFileType::Basic) {
            idx = static_cast<int>(i);
            break;
        }
    }
    std::string text;
    if (idx >= 0) {
        ParseImageResult parsed = parseProgramImage(image.files[static_cast<std::size_t>(idx)].payload,
                                                    cf2000Profile());
        printDiags(parsed.diags, std::cerr);
        if (!parsed.ok()) {
            std::cerr << "error: BASIC の構造が壊れているため書き出しません\n";
            return 2;
        }
        TextOptions textOptions;
        textOptions.crlf = !opt.lf;
        Diagnostics dtext;
        text = detokenize(parsed.program, textOptions, dtext);
        printDiags(dtext, std::cerr);
    } else {
        for (std::size_t i = 0; i < image.files.size(); ++i) {
            if (image.files[i].type == TapeFileType::Ascii) {
                text = asciiPayloadToText(image.files[i].payload, opt.lf);
                break;
            }
        }
        if (text.empty()) {
            std::cerr << "error: BASIC/ASCII ファイルが見つかりません\n";
            return 2;
        }
    }
    Bytes out(text.begin(), text.end());
    if (!outputAllowed(opt.output, opt.force)) return 1;
    if (!writeBytes(opt.output, out)) return 1;
    std::cout << "wrote " << opt.output << " (" << out.size() << " bytes, " << decoded.blocks.size()
              << " blocks, " << decoded.baud << " baud)\n";
    return 0;
}

int cmdCas2Bin(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力と -o 出力を指定してください\n";
        return 1;
    }
    Bytes cas;
    if (!readBytes(opt.input, cas)) return 1;
    Diagnostics diags;
    CasImage image = parseCas(cas, diags);
    printDiags(diags, std::cerr);
    int idx = selectFileIndex(image, opt.index, TapeFileType::Binary);
    if (idx < 0) {
        std::cerr << "error: バイナリファイルが見つかりません\n";
        return 2;
    }
    const CasFileInfo& file = image.files[static_cast<std::size_t>(idx)];
    if (!file.valid || file.payload.size() < 6) {
        std::cerr << "error: バイナリファイルが不正です\n";
        return 2;
    }
    const std::size_t expected = static_cast<std::size_t>(file.end - file.start) + 1;
    const std::size_t available = file.payload.size() - 6;
    const std::size_t take = std::min(expected, available);
    Bytes code(file.payload.begin() + 6, file.payload.begin() + 6 + static_cast<std::ptrdiff_t>(take));
    if (!outputAllowed(opt.output, opt.force)) return 1;
    if (!writeBytes(opt.output, code)) return 1;
    std::cout << "wrote " << opt.output << " (" << code.size() << " bytes, load &H" << std::hex
              << file.start << "-&H" << file.end << ", exec &H" << file.exec << std::dec << ")\n";
    return 0;
}

int cmdNormalize(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力 WAV と -o 出力を指定してください\n";
        return 1;
    }
    if (!outputAllowed(opt.output, opt.force)) return 1;
    StdFileReader reader(opt.input);
    StdFileWriter writer(opt.output);
    if (!reader.isOpen() || !writer.isOpen()) {
        std::cerr << "error: WAV を開けません: " << opt.input << " / " << opt.output << "\n";
        return 1;
    }
    NormalizeOptions options;
    options.targetDbfs = std::atof(opt.targetDbfs.c_str());
    options.maxGainDb = std::atof(opt.maxGainDb.c_str());
    Diagnostics diags;
    NormalizeStats stats;
    if (!normalizeWav(reader, writer, options, diags, &stats)) {
        printDiags(diags, std::cerr);
        std::remove(opt.output.c_str());
        return 2;
    }
    printDiags(diags, std::cerr);
    std::cout << "wrote " << opt.output << " (peak " << stats.inputPeak << " -> " << stats.outputPeak
              << ", gain " << stats.gainDb << " dB, " << stats.frames << " frames)\n";
    return 0;
}

int cmdCasInfo(const Options& opt) {
    if (opt.input.empty()) {
        std::cerr << "error: 入力 CAS を指定してください\n";
        return 1;
    }
    Bytes cas;
    if (!readBytes(opt.input, cas)) return 1;
    Diagnostics diags;
    CasImage image = parseCas(cas, diags);
    std::cout << opt.input << ": " << cas.size() << " bytes, " << image.blocks.size()
              << " blocks, " << image.files.size() << " files\n";
    for (std::size_t i = 0; i < image.files.size(); ++i) {
        const CasFileInfo& f = image.files[i];
        const std::string nameText = charset::decode(
            reinterpret_cast<const uint8_t*>(f.name.data()), f.name.size());
        std::cout << "  [" << i << "] " << tapeTypeName(f.type) << " name='" << nameText << "'"
                  << " header#" << f.headerBlock << " data=" << f.dataBlocks.size()
                  << " bytes=" << f.payload.size();
        if (f.type == TapeFileType::Binary) {
            std::cout << " start=&H" << std::hex << f.start << " end=&H" << f.end << " exec=&H" << f.exec
                      << std::dec;
        }
        std::cout << (f.valid ? " [ok]" : " [invalid]") << "\n";
        if (f.type == TapeFileType::Basic) {
            ParseImageResult parsed = parseProgramImage(f.payload, cf2000Profile());
            std::size_t lines = parsed.program.lines.size();
            std::cout << "      BASIC lines=" << lines << " body=" << f.payload.size()
                      << " bytes (links " << (parsed.program.linksValid ? "ok" : "mismatch") << ")\n";
        }
    }
    size_t warnings = 0;
    for (std::size_t i = 0; i < diags.items().size(); ++i) {
        if (diags.items()[i].severity == Severity::Warning) ++warnings;
    }
    if (warnings != 0) {
        std::cout << "  warnings: " << warnings << " (use --verbose for details)\n";
        if (opt.verbose) printDiags(diags, std::cout);
    }
    if (diags.hasErrors()) {
        printDiags(diags, std::cerr);
        return 2;
    }
    return 0;
}

int cmdValidate(const Options& opt) {
    if (opt.input.empty()) {
        std::cerr << "error: 入力 .bas を指定してください\n";
        return 1;
    }
    std::string source;
    if (!readText(opt.input, source)) return 1;
    std::string text;
    utf8::stripBom(source, text);
    CompileOptions options;
    options.profile = &cf2000Profile();
    CompileResult result = compileProgram(text, options);
    printDiags(result.diags, std::cout);
    std::cout << (result.diags.hasErrors() ? "NG" : "OK") << "  lines=" << result.lines.size()
              << " program=" << result.image.size() << " bytes\n";
    return result.diags.hasErrors() ? 2 : 0;
}

int cmdTokenize(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力と -o 出力を指定してください\n";
        return 1;
    }
    std::string source;
    if (!readText(opt.input, source)) return 1;
    std::string text;
    utf8::stripBom(source, text);
    CompileOptions options;
    options.profile = &cf2000Profile();
    CompileResult result = compileProgram(text, options);
    printDiags(result.diags, std::cerr);
    if (result.diags.hasErrors()) return 2;
    const Bytes out = opt.tape ? toTapeBody(result.image, true) : toDiskFormat(result.image);
    if (!outputAllowed(opt.output, opt.force)) return 1;
    if (!writeBytes(opt.output, out)) return 1;
    std::cout << "wrote " << opt.output << " (" << out.size() << " bytes, "
              << (opt.tape ? "tape body" : "disk format") << ")\n";
    return 0;
}

int cmdDetokenize(const Options& opt) {
    if (opt.input.empty() || opt.output.empty()) {
        std::cerr << "error: 入力と -o 出力を指定してください\n";
        return 1;
    }
    Bytes bytes;
    if (!readBytes(opt.input, bytes)) return 1;
    ParseImageResult parsed = parseDiskBasicFile(bytes, cf2000Profile());
    printDiags(parsed.diags, std::cerr);
    TextOptions textOptions;
    textOptions.crlf = !opt.lf;
    Diagnostics diags;
    const std::string text = detokenize(parsed.program, textOptions, diags);
    printDiags(diags, std::cerr);
    Bytes out(text.begin(), text.end());
    if (!outputAllowed(opt.output, opt.force)) return 1;
    if (!writeBytes(opt.output, out)) return 1;
    std::cout << "wrote " << opt.output << " (" << parsed.program.lines.size() << " lines)\n";
    return 0;
}

int cmdSelfTest() {
    Diagnostics diags;
    const std::string source =
        "10 REM SELFTEST\n20 A=1.5:FOR I=1 TO 3:PRINT \"TEST\";I:NEXT I\n30 GOTO 20\n";
    CompileResult compiled = compileProgram(source, CompileOptions());
    if (compiled.diags.hasErrors()) {
        printDiags(compiled.diags, std::cerr);
        std::cerr << "selftest: compile failed\n";
        return 1;
    }
    CasImage tape = makeBasicTape(toTapeBody(compiled.image, false), "SELFT", diags);
    const int bauds[] = {1200, 2400};
    for (int b = 0; b < 2; ++b) {
        MemoryWriter writer;
        FskOptions options;
        options.baud = bauds[b];
        FskStats stats;
        if (!renderTapeToWav(tape, writer, options, diags, &stats)) {
            printDiags(diags, std::cerr);
            std::cerr << "selftest: render failed\n";
            return 1;
        }
        MemoryReader reader(writer.data());
        DecodeOptions decode;
        Diagnostics dd;
        DecodeResult decoded = demodulateWav(reader, decode, dd);
        if (decoded.blocks.size() != 2 || decoded.blocks[1].bytes != tape.blocks[1].bytes) {
            printDiags(dd, std::cerr);
            std::cerr << "selftest: decode mismatch at " << bauds[b] << " baud\n";
            return 1;
        }
        std::cout << "  " << bauds[b] << " baud: " << stats.samples << " samples, "
                  << decoded.blocks[1].bytes.size() << " bytes, frame errors="
                  << decoded.blocks[1].frameErrors << " -> OK\n";
    }
    std::cout << "selftest: OK\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    std::cout.rdbuf(new Utf8ConsoleBuf(GetStdHandle(STD_OUTPUT_HANDLE)));
    std::cerr.rdbuf(new Utf8ConsoleBuf(GetStdHandle(STD_ERROR_HANDLE)));
    ConsoleFlushGuard flushGuard;
    (void)flushGuard;
#endif
    if (argc < 2) {
        printUsage();
        return 1;
    }
    const std::string command = argv[1];
    if (command == "version" || command == "--version") {
        std::cout << "msxtool " << kVersion << "\n";
        return 0;
    }
    if (command == "selftest") return cmdSelfTest();

    Options opt;
    if (!parseOptions(argc, argv, opt)) {
        printUsage();
        return 1;
    }
    if (command == "bas2cas") return cmdBas2Cas(opt);
    if (command == "bas2wav") return cmdBas2Wav(opt);
    if (command == "cas2wav") return cmdCas2Wav(opt);
    if (command == "wav2cas") return cmdWav2Cas(opt);
    if (command == "cas2bas") return cmdCas2Bas(opt);
    if (command == "wav2bas") return cmdWav2Bas(opt);
    if (command == "cas2bin") return cmdCas2Bin(opt);
    if (command == "normalize") return cmdNormalize(opt);
    if (command == "casinfo") return cmdCasInfo(opt);
    if (command == "validate") return cmdValidate(opt);
    if (command == "tokenize") return cmdTokenize(opt);
    if (command == "detokenize") return cmdDetokenize(opt);
    std::cerr << "不明なコマンド: " << command << "\n";
    printUsage();
    return 1;
}
