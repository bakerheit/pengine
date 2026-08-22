#include "core/asset_root.h"

#include <filesystem>

// Compile-time fallback root: the dev checkout's assets tree. Only reached
// when there is no ./assets in the working directory — i.e. headless test
// binaries running out of the build dir.
#ifndef APRICOT_ASSETS_DIR
#define APRICOT_ASSETS_DIR "assets"
#endif

namespace apricot {

const std::string& asset_root() {
    static const std::string root = [] {
        std::error_code ec;
        return std::filesystem::exists("assets", ec)
                   ? std::string("assets")
                   : std::string(APRICOT_ASSETS_DIR);
    }();
    return root;
}

std::string asset_path(std::string_view rel) {
    std::string p = asset_root();
    p.reserve(p.size() + 1 + rel.size());
    p += '/';
    p.append(rel);
    return p;
}

}  // namespace apricot
