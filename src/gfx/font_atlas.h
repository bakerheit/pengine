#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "gfx/glyph_atlas.h"

namespace apricot {

// Runtime-rasterised printable ASCII atlas used by the game UI. The regular
// UI face, title face, Material Symbols and a solid texel all live in one
// texture so styled headings do not add another HUD draw call.
inline constexpr int kUiFontCellW = 64;
inline constexpr int kUiFontCellH = 72;
inline constexpr int kUiFontCols = 16;
inline constexpr int kUiFontRows = 13;
inline constexpr int kUiFontAtlasW = kUiFontCellW * kUiFontCols;
inline constexpr int kUiFontAtlasH = kUiFontCellH * kUiFontRows;
inline constexpr float kUiFontRasterHeight = 64.0f;
inline constexpr float kUiTitleFontRasterHeight = 56.0f;

enum class UiSymbol : std::size_t {
    LocalGasStation = 0,
    OilBarrel,
    ModeDual,
    Target,
    MoneyRange,
    LocalLaundryService,
    Build,
    Count,
};

inline constexpr std::size_t kUiSymbolCount =
    static_cast<std::size_t>(UiSymbol::Count);

struct UiGlyph {
    GlyphUV uv{};
    float x_offset = 0.0f;
    float y_offset = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float advance = 0.0f;
};

struct UiFontAtlas {
    std::vector<uint8_t> pixels;
    std::array<UiGlyph, kCharCount> glyphs{};
    std::array<UiGlyph, kCharCount> title_glyphs{};
    std::array<UiGlyph, kUiSymbolCount> symbols{};
    GlyphUV solid{};
    float ascent = 0.0f;
    float line_height = 0.0f;
    float title_ascent = 0.0f;
    float title_line_height = 0.0f;
    bool title_available = false;
    bool symbols_available = false;
};

// Loads and rasterises a TrueType font. False means the caller should retain
// the built-in bitmap fallback rather than losing all game text.
bool build_ui_font_atlas(const std::string& path,
                         const std::string& title_path,
                         const std::string& symbols_path,
                         UiFontAtlas& out);

inline int ui_glyph_index(char c) { return glyph_cell(c); }

}  // namespace apricot
