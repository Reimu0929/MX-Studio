#include "msx/tape.h"

#include <algorithm>
#include <cstring>

#include "msx/basic.h"
#include "msx/charset.h"
#include "msx/profile.h"

namespace msx {

namespace {

const uint8_t kMagic[8] = {0x1F, 0xA6, 0xDE, 0xBA, 0xCC, 0x13, 0x7D, 0x74};

bool allSame(const Bytes& bytes, std::size_t offset, std::size_t count) {
    if (offset + count > bytes.size()) return false;
    for (std::size_t i = 1; i < count; ++i) {
        if (bytes[offset + i] != bytes[offset]) return false;
    }
    return true;
}


void addFile(CasImage& image, std::size_t headerIndex) {
    const TapeBlock& header = image.blocks[headerIndex];
    CasFileInfo file;
    file.headerBlock = headerIndex;
    file.type = header.type;
    file.name = header.bytes.size() >= 16 ? std::string(reinterpret_cast<const char*>(&header.bytes[10]), 6)
                                          : std::string();
    file.valid = header.type != TapeFileType::Unknown;
    for (std::size_t i = headerIndex + 1; i < image.blocks.size(); ++i) {
        if (image.blocks[i].header) break;
        file.dataBlocks.push_back(i);
        const TapeBlock& block = image.blocks[i];
        if (file.type == TapeFileType::Ascii) {
            file.payload.insert(file.payload.end(), block.bytes.begin(), block.bytes.end());
        } else if (file.payload.empty()) {
            file.payload = block.bytes;
        }
    }
    if (file.type == TapeFileType::Binary && file.payload.size() >= 6) {
        file.start = readLe16(&file.payload[0]);
        file.end = readLe16(&file.payload[2]);
        file.exec = readLe16(&file.payload[4]);
    }
    image.files.push_back(file);
}

bool validateBasicPayload(const Bytes& payload, Diagnostics& diags, const std::string& name) {
    if (payload.empty()) {
        diags.warning("BASIC ファイル '" + name + "' のデータブロックが空です");
        return false;
    }
    ParseImageResult parsed = parseProgramImage(payload, cf2000Profile());
    if (!parsed.ok()) {
        diags.warning("BASIC ファイル '" + name + "' のプログラム構造に問題があります");
        return false;
    }
    return true;
}

}  // namespace

const uint8_t* casMagic() { return kMagic; }

bool isCasMagic(const uint8_t* data) { return std::memcmp(data, kMagic, 8) == 0; }

TapeFileType tapeTypeFromHeader(const Bytes& payload) {
    if (payload.size() < 10) return TapeFileType::Unknown;
    if (!allSame(payload, 0, 10)) return TapeFileType::Unknown;
    switch (payload[0]) {
        case 0xD3: return TapeFileType::Basic;
        case 0xD0: return TapeFileType::Binary;
        case 0xEA: return TapeFileType::Ascii;
        default: return TapeFileType::Unknown;
    }
}

std::string tapeTypeName(TapeFileType type) {
    switch (type) {
        case TapeFileType::Basic: return "BASIC";
        case TapeFileType::Binary: return "バイナリ";
        case TapeFileType::Ascii: return "ASCII/データ";
        default: return "不明";
    }
}

bool nameToMsx6(const std::string& utf8Name, std::string& msxName6, Diagnostics& diags) {
    Bytes encoded;
    charset::EncodeError error;
    if (charset::encode(utf8Name, encoded, &error) != charset::EncodeStatus::Ok) {
        diags.error("ファイル名を MSX 文字コードに変換できません");
        return false;
    }
    if (encoded.empty()) {
        msxName6.assign(6, ' ');
        return true;
    }
    if (encoded.size() > 6) {
        diags.error("ファイル名は 6 バイト以内にしてください（現在 " + std::to_string(encoded.size()) +
                    " バイト）");
        return false;
    }
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] < 0x20 || encoded[i] == 0x7F) {
            diags.error("ファイル名に制御文字は使用できません");
            return false;
        }
    }
    msxName6.assign(reinterpret_cast<const char*>(&encoded[0]), encoded.size());
    msxName6.append(6 - encoded.size(), ' ');
    return true;
}

CasImage parseCas(const Bytes& data, Diagnostics& diags) {
    CasImage image;
    std::vector<std::size_t> starts;
    for (std::size_t pos = 0; pos + 8 <= data.size(); ++pos) {
        if ((pos % 8) == 0 && isCasMagic(&data[pos])) starts.push_back(pos);
    }
    if (starts.empty()) {
        // Be forgiving for images that are not 8-byte aligned, but say so.
        for (std::size_t pos = 0; pos + 8 <= data.size(); ++pos) {
            if (isCasMagic(&data[pos])) starts.push_back(pos);
        }
        if (!starts.empty()) {
            diags.warning("CAS のヘッダーが 8 バイト境界にありません（非標準の可能性）");
        }
    }
    if (starts.empty()) {
        diags.error("CAS ファイルにマーカー（1F A6 DE BA CC 13 7D 74）がありません");
        return image;
    }
    if (starts[0] != 0) {
        diags.warning("CAS の先頭に " + std::to_string(starts[0]) + " バイトの未解釈データがあります");
    }
    TapeFileType currentType = TapeFileType::Unknown;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const std::size_t begin = starts[i] + 8;
        const std::size_t end = (i + 1 < starts.size()) ? starts[i + 1] : data.size();
        if (begin > end) {
            diags.error("CAS のブロック境界が不正です");
            break;
        }
        TapeBlock block;
        block.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(begin),
                           data.begin() + static_cast<std::ptrdiff_t>(end));
        const TapeFileType type = tapeTypeFromHeader(block.bytes);
        if (type != TapeFileType::Unknown) {
            block.header = true;
            block.type = type;
            currentType = type;
            if (block.bytes.size() < 16) {
                diags.warning("CAS のヘッダーブロックが 16 バイト未満です");
            }
            image.blocks.push_back(block);
            addFile(image, image.blocks.size() - 1);
            continue;
        }
        block.type = currentType;
        if (currentType != TapeFileType::Unknown && !image.files.empty()) {
            image.files.back().dataBlocks.push_back(image.blocks.size());
            if (image.files.back().type == TapeFileType::Ascii) {
                image.files.back().payload.insert(image.files.back().payload.end(), block.bytes.begin(),
                                                  block.bytes.end());
            } else if (image.files.back().payload.empty()) {
                image.files.back().payload = block.bytes;
            }
        } else {
            diags.warning("CAS にヘッダーのないデータブロックがあります");
        }
        image.blocks.push_back(block);
    }

    // Validate the grouped files.
    for (std::size_t i = 0; i < image.files.size(); ++i) {
        CasFileInfo& file = image.files[i];
        const std::string name = file.name;
        if (file.dataBlocks.empty()) {
            diags.warning("ファイル '" + name + "' にデータブロックがありません");
            file.valid = false;
            continue;
        }
        if (file.type == TapeFileType::Binary && file.payload.size() >= 6) {
            file.start = readLe16(&file.payload[0]);
            file.end = readLe16(&file.payload[2]);
            file.exec = readLe16(&file.payload[4]);
        }
        if (file.type == TapeFileType::Basic) {
            file.valid = validateBasicPayload(file.payload, diags, name);
            if (file.payload.size() < 7) {
                diags.warning("BASIC ファイル '" + name + "' の終端パディングが不足しています");
            }
        } else if (file.type == TapeFileType::Binary) {
            if (file.payload.size() < 6) {
                diags.error("バイナリファイル '" + name + "' のアドレスフィールドが不足しています");
                file.valid = false;
            } else {
                const uint32_t expected = static_cast<uint32_t>(file.end) - file.start + 1u;
                const std::size_t actual = file.payload.size() - 6;
                if (file.end < file.start) {
                    diags.error("バイナリファイル '" + name + "' の開始・終了アドレスが逆転しています");
                    file.valid = false;
                } else if (actual < expected) {
                    diags.error("バイナリファイル '" + name + "' のデータが不足しています（宣言 " +
                                std::to_string(expected) + " バイト、実データ " + std::to_string(actual) +
                                " バイト）");
                    file.valid = false;
                } else {
                    if (actual > expected) {
                        // CAS alignment padding is allowed only when it is zero.
                        bool zeroPadding = true;
                        for (std::size_t k = 6 + expected; k < file.payload.size(); ++k) {
                            if (file.payload[k] != 0x00) {
                                zeroPadding = false;
                                break;
                            }
                        }
                        if (!zeroPadding) {
                            diags.warning("バイナリファイル '" + name + "' に宣言長を超えるデータがあります");
                        }
                    }
                    file.valid = true;
                }
            }
        } else if (file.type == TapeFileType::Ascii) {
            for (std::size_t k = 0; k < file.dataBlocks.size(); ++k) {
                const Bytes& block = image.blocks[file.dataBlocks[k]].bytes;
                if (block.size() != 256) {
                    diags.warning("ASCII ブロックが 256 バイトではありません（" + std::to_string(block.size()) +
                                  " バイト）");
                }
            }
            const bool hasEof = std::find(file.payload.begin(), file.payload.end(), 0x1A) != file.payload.end();
            if (!hasEof) {
                diags.warning("ASCII ファイル '" + name + "' に EOF (0x1A) がありません");
            }
            file.valid = true;
        }
    }
    return image;
}

Bytes serializeCas(const CasImage& image, bool alignToEight, Diagnostics& diags) {
    Bytes out;
    for (std::size_t i = 0; i < image.blocks.size(); ++i) {
        const TapeBlock& block = image.blocks[i];
        out.insert(out.end(), kMagic, kMagic + 8);
        out.insert(out.end(), block.bytes.begin(), block.bytes.end());
        if (alignToEight) {
            while ((out.size() % 8) != 0) out.push_back(0x00);
        }
    }
    (void)diags;
    return out;
}

CasImage makeBasicTape(const Bytes& tapeBody, const std::string& name, Diagnostics& diags) {
    CasImage image;
    std::string name6;
    if (!nameToMsx6(name, name6, diags)) return image;
    TapeBlock header;
    header.header = true;
    header.type = TapeFileType::Basic;
    header.bytes.assign(10, 0xD3);
    header.bytes.insert(header.bytes.end(), name6.begin(), name6.end());
    TapeBlock data;
    data.type = TapeFileType::Basic;
    data.bytes = tapeBody;
    image.blocks.push_back(header);
    image.blocks.push_back(data);
    CasFileInfo file;
    file.type = TapeFileType::Basic;
    file.name = name6;
    file.headerBlock = 0;
    file.dataBlocks.push_back(1);
    file.payload = tapeBody;
    file.valid = true;
    image.files.push_back(file);
    return image;
}

CasImage makeAsciiTape(const Bytes& msxAscii, const std::string& name, Diagnostics& diags) {
    CasImage image;
    std::string name6;
    if (!nameToMsx6(name, name6, diags)) return image;
    Bytes data = msxAscii;
    if (data.empty() || data[data.size() - 1] != 0x1A) data.push_back(0x1A);
    TapeBlock header;
    header.header = true;
    header.type = TapeFileType::Ascii;
    header.bytes.assign(10, 0xEA);
    header.bytes.insert(header.bytes.end(), name6.begin(), name6.end());
    image.blocks.push_back(header);
    CasFileInfo file;
    file.type = TapeFileType::Ascii;
    file.name = name6;
    file.headerBlock = 0;
    file.payload = data;
    for (std::size_t pos = 0; pos < data.size(); pos += 256) {
        const std::size_t n = std::min<std::size_t>(256, data.size() - pos);
        TapeBlock block;
        block.type = TapeFileType::Ascii;
        block.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                           data.begin() + static_cast<std::ptrdiff_t>(pos + n));
        while (block.bytes.size() < 256) block.bytes.push_back(0x1A);
        file.dataBlocks.push_back(image.blocks.size());
        image.blocks.push_back(block);
    }
    file.valid = true;
    image.files.push_back(file);
    return image;
}

CasImage makeBinaryTape(const Bytes& code, uint16_t start, uint16_t end, uint16_t exec,
                        const std::string& name, Diagnostics& diags) {
    CasImage image;
    if (end < start) {
        diags.error("バイナリの終了アドレスが開始アドレスより小さいです");
        return image;
    }
    const std::size_t expected = static_cast<std::size_t>(end - start) + 1;
    if (code.size() != expected) {
        diags.error("バイナリの長さがアドレス範囲と一致しません（期待 " + std::to_string(expected) +
                    " バイト、実データ " + std::to_string(code.size()) + " バイト）");
        return image;
    }
    std::string name6;
    if (!nameToMsx6(name, name6, diags)) return image;
    TapeBlock header;
    header.header = true;
    header.type = TapeFileType::Binary;
    header.bytes.assign(10, 0xD0);
    header.bytes.insert(header.bytes.end(), name6.begin(), name6.end());
    TapeBlock data;
    data.type = TapeFileType::Binary;
    appendLe16(data.bytes, start);
    appendLe16(data.bytes, end);
    appendLe16(data.bytes, exec);
    data.bytes.insert(data.bytes.end(), code.begin(), code.end());
    image.blocks.push_back(header);
    image.blocks.push_back(data);
    CasFileInfo file;
    file.type = TapeFileType::Binary;
    file.name = name6;
    file.headerBlock = 0;
    file.dataBlocks.push_back(1);
    file.payload = data.bytes;
    file.start = start;
    file.end = end;
    file.exec = exec;
    file.valid = true;
    image.files.push_back(file);
    return image;
}

}  // namespace msx
