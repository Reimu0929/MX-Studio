#pragma once

#include <cstdint>
#include <string>

namespace msx {

struct ModelProfile {
    std::string id;
    std::string displayName;
    std::string basicVersion;
    uint16_t baseAddress;       // start of the BASIC program area
    uint32_t ramBytes;
    uint32_t freeBytes;         // value printed by PRINT FRE(0) after boot
    uint32_t programAreaTop;    // baseAddress + ram - BASIC work area
    bool japaneseCharset;
    bool msx2Commands;
    bool diskBasic;

    uint32_t programCapacity() const { return programAreaTop > baseAddress ? programAreaTop - baseAddress : 0; }
};

// National CF-2000: Japanese MSX1, 16 KiB RAM, MSX BASIC 1.0.
// The owner's manual records "12431 Bytes free" on the start screen.
const ModelProfile& cf2000Profile();

// Returns nullptr when the id is unknown.
const ModelProfile* profileById(const std::string& id);

}  // namespace msx
