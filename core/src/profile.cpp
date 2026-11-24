#include "msx/profile.h"

namespace msx {

const ModelProfile& cf2000Profile() {
    static const ModelProfile profile = {
        "cf2000",
        "National CF-2000 (Japanese MSX1, 16 KB)",
        "MSX BASIC 1.0",
        0x8000,
        16u * 1024u,
        12431u,
        0xB08Fu,
        true,
        false,
        false,
    };
    return profile;
}

const ModelProfile* profileById(const std::string& id) {
    if (id == "cf2000" || id == "CF-2000" || id == "cf-2000") return &cf2000Profile();
    return nullptr;
}

}  // namespace msx
