#include "gfx/font_atlas.h"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "core/log.h"

namespace apricot {
namespace {

constexpr int kUiCellMargin = 2;
constexpr int kUiSolidCell = kUiFontCols * kUiFontRows - 1;
constexpr int kUiFirstSymbolCell = kCharCount;
constexpr int kUiFirstTitleCell =
    kUiFirstSymbolCell + static_cast<int>(kUiSymbolCount);
constexpr float kUiSymbolRasterHeight = 56.0f;
constexpr std::array<int, kUiSymbolCount> kUiSymbolCodepoints{
    0xe546,  // local_gas_station
    0xec15,  // oil_barrel
    0xf557,  // mode_dual
    0xe719,  // target
    0xf245,  // money_range
    0xe54a,  // local_laundry_service
    0xf8cd,  // build
};
static_assert(kUiFirstTitleCell + kCharCount < kUiSolidCell,
              "UI glyphs must leave the final atlas cell solid");

bool read_binary(const std::string& path, std::vector<unsigned char>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    bytes.assign(std::istreambuf_iterator<char>(file),
                 std::istreambuf_iterator<char>());
    return !bytes.empty();
}

bool rasterize_ascii(const std::string& path, const char* face_name,
                     float raster_height, int first_cell,
                     std::array<UiGlyph, kCharCount>& glyphs,
                     float& out_ascent, float& out_line_height,
                     std::vector<uint8_t>& pixels) {
    std::vector<unsigned char> font_bytes;
    if (!read_binary(path, font_bytes)) {
        AP_WARN("hud: could not read %s font '%s'", face_name, path.c_str());
        return false;
    }
    const int offset = stbtt_GetFontOffsetForIndex(font_bytes.data(), 0);
    stbtt_fontinfo font{};
    if (offset < 0 || static_cast<std::size_t>(offset) >= font_bytes.size() ||
        stbtt_InitFont(&font, font_bytes.data(), offset) == 0) {
        AP_WARN("hud: %s font '%s' is not valid TrueType data", face_name,
                path.c_str());
        return false;
    }

    const float scale = stbtt_ScaleForPixelHeight(&font, raster_height);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    out_ascent = static_cast<float>(ascent) * scale;
    out_line_height =
        static_cast<float>(ascent - descent + line_gap) * scale;
    if (!(out_line_height > 0.0f)) return false;

    for (int glyph_index = 0; glyph_index < kCharCount; ++glyph_index) {
        const int codepoint = kFirstChar + glyph_index;
        int advance = 0;
        int left_bearing = 0;
        stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &left_bearing);
        (void)left_bearing;

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetCodepointBitmapBox(&font, codepoint, scale, scale,
                                    &x0, &y0, &x1, &y1);
        const int width = x1 - x0;
        const int height = y1 - y0;
        if (width > kUiFontCellW - kUiCellMargin * 2 ||
            height > kUiFontCellH - kUiCellMargin * 2) {
            AP_WARN("hud: %s glyph %d does not fit atlas cell", face_name,
                    codepoint);
            return false;
        }

        const int cell = first_cell + glyph_index;
        const int cell_x = (cell % kUiFontCols) * kUiFontCellW;
        const int cell_y = (cell / kUiFontCols) * kUiFontCellH;
        const int bitmap_x = cell_x + kUiCellMargin;
        const int bitmap_y = cell_y + kUiCellMargin;
        if (width > 0 && height > 0) {
            unsigned char* destination =
                pixels.data() +
                static_cast<std::size_t>(bitmap_y) *
                    static_cast<std::size_t>(kUiFontAtlasW) +
                static_cast<std::size_t>(bitmap_x);
            stbtt_MakeCodepointBitmap(&font, destination, width, height,
                                      kUiFontAtlasW, scale, scale, codepoint);
        }

        UiGlyph& glyph = glyphs[static_cast<std::size_t>(glyph_index)];
        glyph.x_offset = static_cast<float>(x0);
        glyph.y_offset = static_cast<float>(y0);
        glyph.width = static_cast<float>(std::max(width, 0));
        glyph.height = static_cast<float>(std::max(height, 0));
        glyph.advance = static_cast<float>(advance) * scale;
        glyph.uv.u0 = static_cast<float>(bitmap_x) /
                      static_cast<float>(kUiFontAtlasW);
        glyph.uv.v0 = static_cast<float>(bitmap_y) /
                      static_cast<float>(kUiFontAtlasH);
        glyph.uv.u1 = static_cast<float>(bitmap_x + std::max(width, 1)) /
                      static_cast<float>(kUiFontAtlasW);
        glyph.uv.v1 = static_cast<float>(bitmap_y + std::max(height, 1)) /
                      static_cast<float>(kUiFontAtlasH);
    }
    return true;
}

}  // namespace

bool build_ui_font_atlas(const std::string& path,
                         const std::string& title_path,
                         const std::string& symbols_path,
                         UiFontAtlas& out) {
    UiFontAtlas atlas;
    atlas.pixels.assign(static_cast<std::size_t>(kUiFontAtlasW) *
                            static_cast<std::size_t>(kUiFontAtlasH),
                        uint8_t{0});

    if (!rasterize_ascii(path, "UI", kUiFontRasterHeight, 0,
                         atlas.glyphs, atlas.ascent, atlas.line_height,
                         atlas.pixels)) {
        return false;
    }
    atlas.title_available = rasterize_ascii(
        title_path, "title", kUiTitleFontRasterHeight, kUiFirstTitleCell,
        atlas.title_glyphs, atlas.title_ascent, atlas.title_line_height,
        atlas.pixels);

    std::vector<unsigned char> symbol_font_bytes;
    if (!read_binary(symbols_path, symbol_font_bytes)) {
        AP_WARN("hud: could not read Material Symbols font '%s'",
                symbols_path.c_str());
    } else {
        const int symbol_offset =
            stbtt_GetFontOffsetForIndex(symbol_font_bytes.data(), 0);
        stbtt_fontinfo symbol_font{};
        if (symbol_offset < 0 ||
            static_cast<std::size_t>(symbol_offset) >=
                symbol_font_bytes.size() ||
            stbtt_InitFont(&symbol_font, symbol_font_bytes.data(),
                           symbol_offset) == 0) {
            AP_WARN("hud: Material Symbols font '%s' is not valid TrueType data",
                    symbols_path.c_str());
        } else {
            const float symbol_scale =
                stbtt_ScaleForPixelHeight(&symbol_font,
                                          kUiSymbolRasterHeight);
            bool complete = true;
            for (std::size_t i = 0; i < kUiSymbolCount; ++i) {
                const int codepoint = kUiSymbolCodepoints[i];
                if (stbtt_FindGlyphIndex(&symbol_font, codepoint) == 0) {
                    AP_WARN("hud: Material Symbols codepoint U+%04X is missing",
                            codepoint);
                    complete = false;
                    continue;
                }

                int x0 = 0;
                int y0 = 0;
                int x1 = 0;
                int y1 = 0;
                stbtt_GetCodepointBitmapBox(&symbol_font, codepoint,
                                            symbol_scale, symbol_scale,
                                            &x0, &y0, &x1, &y1);
                const int width = x1 - x0;
                const int height = y1 - y0;
                if (width <= 0 || height <= 0 ||
                    width > kUiFontCellW - kUiCellMargin * 2 ||
                    height > kUiFontCellH - kUiCellMargin * 2) {
                    AP_WARN("hud: Material Symbols codepoint U+%04X does not fit atlas cell",
                            codepoint);
                    complete = false;
                    continue;
                }

                const int cell = kUiFirstSymbolCell + static_cast<int>(i);
                const int cell_x = (cell % kUiFontCols) * kUiFontCellW;
                const int cell_y = (cell / kUiFontCols) * kUiFontCellH;
                const int bitmap_x = cell_x + (kUiFontCellW - width) / 2;
                const int bitmap_y = cell_y + (kUiFontCellH - height) / 2;
                unsigned char* destination =
                    atlas.pixels.data() +
                    static_cast<std::size_t>(bitmap_y) *
                        static_cast<std::size_t>(kUiFontAtlasW) +
                    static_cast<std::size_t>(bitmap_x);
                stbtt_MakeCodepointBitmap(&symbol_font, destination,
                                          width, height, kUiFontAtlasW,
                                          symbol_scale, symbol_scale,
                                          codepoint);

                UiGlyph& glyph = atlas.symbols[i];
                glyph.width = static_cast<float>(width);
                glyph.height = static_cast<float>(height);
                glyph.advance = static_cast<float>(width);
                glyph.uv.u0 = static_cast<float>(bitmap_x) /
                              static_cast<float>(kUiFontAtlasW);
                glyph.uv.v0 = static_cast<float>(bitmap_y) /
                              static_cast<float>(kUiFontAtlasH);
                glyph.uv.u1 = static_cast<float>(bitmap_x + width) /
                              static_cast<float>(kUiFontAtlasW);
                glyph.uv.v1 = static_cast<float>(bitmap_y + height) /
                              static_cast<float>(kUiFontAtlasH);
            }
            atlas.symbols_available = complete;
        }
    }

    const int solid_x = (kUiSolidCell % kUiFontCols) * kUiFontCellW;
    const int solid_y = (kUiSolidCell / kUiFontCols) * kUiFontCellH;
    for (int y = solid_y; y < solid_y + kUiFontCellH; ++y) {
        for (int x = solid_x; x < solid_x + kUiFontCellW; ++x) {
            atlas.pixels[static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(kUiFontAtlasW) +
                         static_cast<std::size_t>(x)] = uint8_t{255};
        }
    }
    const float solid_u =
        static_cast<float>(solid_x + kUiFontCellW / 2) /
        static_cast<float>(kUiFontAtlasW);
    const float solid_v =
        static_cast<float>(solid_y + kUiFontCellH / 2) /
        static_cast<float>(kUiFontAtlasH);
    atlas.solid = GlyphUV{solid_u, solid_v, solid_u, solid_v};

    out = std::move(atlas);
    return true;
}

}  // namespace apricot
