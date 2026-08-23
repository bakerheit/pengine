#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace apricot {

// A procedurally generated glyph atlas. NO FONT FILES.
//
// The engine ships no .ttf and links no font library. The glyphs are a 5x7
// bitmap table compiled into the binary, upscaled and softened at startup into
// a single-channel coverage atlas. That is a deliberate trade: a bitmap font
// cannot be as pretty as a hinted vector one, and in exchange the HUD can never
// fail to load, can never render as boxes because a font went missing from a
// package, and costs one dependency fewer.
//
// GL-free and header-only so the headless suite can build the real atlas and
// check it — the failure this guards against is an atlas that uploads
// successfully and is entirely blank, which on screen is indistinguishable from
// a HUD that was never drawn.

inline constexpr int kFirstChar = 32;    // space
inline constexpr int kCharCount = 95;    // through '~' (126)

// Source bitmap size, in font units. Every metric below is expressed in these,
// so a caller only ever picks a pixel height and the rest follows.
inline constexpr int kGlyphUnitsW = 5;
inline constexpr int kGlyphUnitsH = 7;
inline constexpr int kAdvanceUnits = 6;  // 5 wide plus one column of spacing

// Atlas layout. Each glyph is upscaled by kScale and softened, then blitted
// into its own cell with a one-texel margin so the softening cannot bleed
// across a cell boundary into the letter next door.
inline constexpr int kScale = 3;
inline constexpr int kGlyphPxW = kGlyphUnitsW * kScale;  // 15
inline constexpr int kGlyphPxH = kGlyphUnitsH * kScale;  // 21
inline constexpr int kCellPx = 24;
inline constexpr int kCellMargin = 1;
inline constexpr int kAtlasCols = 16;
inline constexpr int kAtlasRows = 6;                     // 96 cells for 95 glyphs
inline constexpr int kAtlasW = kAtlasCols * kCellPx;     // 384
inline constexpr int kAtlasH = kAtlasRows * kCellPx;     // 144

// The spare 96th cell is filled solid. Sampling its centre gives coverage 1.0,
// which is what lets a filled HUD panel and a letter be the SAME draw call: a
// panel is just a quad whose UVs point at the white block. Without it the HUD
// needs a mode uniform, and a mode uniform means a flush every time a widget
// alternates between a background and a label — which is every widget.
inline constexpr int kSolidCell = kAtlasCols * kAtlasRows - 1;  // 95

// 5x7 bitmap font, column-major, bit 0 = top row. One entry per printable
// ASCII character from 32 to 126 inclusive.
inline constexpr uint8_t kFont5x7[kCharCount][kGlyphUnitsW] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // (space)
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // !
    {0x00, 0x07, 0x00, 0x07, 0x00},  // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08},  // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // +
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x60, 0x60, 0x00, 0x00},  // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},  // :
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ;
    {0x00, 0x08, 0x14, 0x22, 0x41},  // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0x41, 0x22, 0x14, 0x08, 0x00},  // >
    {0x02, 0x01, 0x51, 0x09, 0x06},  // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E},  // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x01, 0x01},  // F
    {0x3E, 0x41, 0x41, 0x51, 0x32},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x03, 0x04, 0x78, 0x04, 0x03},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
    {0x00, 0x00, 0x7F, 0x41, 0x41},  // [
    {0x02, 0x04, 0x08, 0x10, 0x20},  // backslash
    {0x41, 0x41, 0x7F, 0x00, 0x00},  // ]
    {0x04, 0x02, 0x01, 0x02, 0x04},  // ^
    {0x40, 0x40, 0x40, 0x40, 0x40},  // _
    {0x00, 0x01, 0x02, 0x04, 0x00},  // `
    {0x20, 0x54, 0x54, 0x54, 0x78},  // a
    {0x7F, 0x48, 0x44, 0x44, 0x38},  // b
    {0x38, 0x44, 0x44, 0x44, 0x20},  // c
    {0x38, 0x44, 0x44, 0x48, 0x7F},  // d
    {0x38, 0x54, 0x54, 0x54, 0x18},  // e
    {0x08, 0x7E, 0x09, 0x01, 0x02},  // f
    {0x08, 0x14, 0x54, 0x54, 0x3C},  // g
    {0x7F, 0x08, 0x04, 0x04, 0x78},  // h
    {0x00, 0x44, 0x7D, 0x40, 0x00},  // i
    {0x20, 0x40, 0x44, 0x3D, 0x00},  // j
    {0x00, 0x7F, 0x10, 0x28, 0x44},  // k
    {0x00, 0x41, 0x7F, 0x40, 0x00},  // l
    {0x7C, 0x04, 0x18, 0x04, 0x78},  // m
    {0x7C, 0x08, 0x04, 0x04, 0x78},  // n
    {0x38, 0x44, 0x44, 0x44, 0x38},  // o
    {0x7C, 0x14, 0x14, 0x14, 0x08},  // p
    {0x08, 0x14, 0x14, 0x18, 0x7C},  // q
    {0x7C, 0x08, 0x04, 0x04, 0x08},  // r
    {0x48, 0x54, 0x54, 0x54, 0x20},  // s
    {0x04, 0x3F, 0x44, 0x40, 0x20},  // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C},  // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C},  // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C},  // w
    {0x44, 0x28, 0x10, 0x28, 0x44},  // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C},  // y
    {0x44, 0x64, 0x54, 0x4C, 0x44},  // z
    {0x00, 0x08, 0x36, 0x41, 0x00},  // {
    {0x00, 0x00, 0x7F, 0x00, 0x00},  // |
    {0x00, 0x41, 0x36, 0x08, 0x00},  // }
    {0x08, 0x04, 0x08, 0x10, 0x08},  // ~
};

// UV rectangle of one atlas cell's glyph area. v0 is the TOP of the glyph and
// v1 the bottom, matching the HUD's top-left-origin pixel space, so a caller
// never has to flip anything.
struct GlyphUV {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

// Cell index for a character. Anything outside the printable range maps to
// '?', which is loud on screen — silently dropping it would make a formatting
// bug invisible.
inline int glyph_cell(char c) {
    const int code = static_cast<int>(static_cast<unsigned char>(c));
    if (code < kFirstChar || code >= kFirstChar + kCharCount) {
        return static_cast<int>('?') - kFirstChar;
    }
    return code - kFirstChar;
}

inline GlyphUV cell_uv(int cell) {
    const int cx = cell % kAtlasCols;
    const int cy = cell / kAtlasCols;
    const float x0 = static_cast<float>(cx * kCellPx + kCellMargin);
    const float y0 = static_cast<float>(cy * kCellPx + kCellMargin);
    GlyphUV uv;
    uv.u0 = x0 / static_cast<float>(kAtlasW);
    uv.v0 = y0 / static_cast<float>(kAtlasH);
    uv.u1 = (x0 + static_cast<float>(kGlyphPxW)) / static_cast<float>(kAtlasW);
    uv.v1 = (y0 + static_cast<float>(kGlyphPxH)) / static_cast<float>(kAtlasH);
    return uv;
}

inline GlyphUV glyph_uv(char c) { return cell_uv(glyph_cell(c)); }

// A single point in the middle of the solid cell. All four corners of a solid
// panel use it, so the quad samples coverage 1.0 everywhere with no filtering
// slop at the edges.
inline GlyphUV solid_uv() {
    const int cx = kSolidCell % kAtlasCols;
    const int cy = kSolidCell / kAtlasCols;
    const float u = (static_cast<float>(cx * kCellPx) + static_cast<float>(kCellPx) * 0.5f) /
                    static_cast<float>(kAtlasW);
    const float v = (static_cast<float>(cy * kCellPx) + static_cast<float>(kCellPx) * 0.5f) /
                    static_cast<float>(kAtlasH);
    return GlyphUV{u, v, u, v};
}

// --- metrics ---------------------------------------------------------------
// One "unit" is one row of the 5x7 source bitmap, so a glyph is kGlyphUnitsH
// units tall and the caller's glyph_h_px sets the scale for everything else.

inline float text_unit_px(float glyph_h_px) {
    return glyph_h_px / static_cast<float>(kGlyphUnitsH);
}
inline float glyph_advance_px(float glyph_h_px) {
    return text_unit_px(glyph_h_px) * static_cast<float>(kAdvanceUnits);
}
inline float glyph_width_px(float glyph_h_px) {
    return text_unit_px(glyph_h_px) * static_cast<float>(kGlyphUnitsW);
}

// Rendered width of a string. Trailing inter-character spacing is not counted,
// so a centred label is actually centred rather than a half-space to the left.
inline float text_width_px(const char* s, float glyph_h_px) {
    if (!s || !*s) return 0.0f;
    std::size_t n = 0;
    for (const char* p = s; *p; ++p) ++n;
    const float advance = glyph_advance_px(glyph_h_px);
    return static_cast<float>(n - 1) * advance + glyph_width_px(glyph_h_px);
}

// Build the R8 coverage atlas: kAtlasW * kAtlasH bytes, row 0 first.
//
// Each glyph is upscaled by kScale into a local buffer, softened with one 3x3
// box pass clamped to the glyph's own bounds, and only then blitted into its
// cell. Softening inside the local buffer rather than across the finished atlas
// is what keeps one letter's antialiasing out of its neighbour's cell.
inline std::vector<uint8_t> build_glyph_atlas() {
    std::vector<uint8_t> atlas(static_cast<std::size_t>(kAtlasW) *
                                   static_cast<std::size_t>(kAtlasH),
                               uint8_t{0});

    std::vector<uint8_t> hard(static_cast<std::size_t>(kGlyphPxW) *
                              static_cast<std::size_t>(kGlyphPxH));
    std::vector<uint8_t> soft(hard.size());

    for (int g = 0; g < kCharCount; ++g) {
        // Upscale the 5x7 mask.
        for (int py = 0; py < kGlyphPxH; ++py) {
            for (int px = 0; px < kGlyphPxW; ++px) {
                const int col = px / kScale;
                const int row = py / kScale;
                const bool on = (kFont5x7[g][col] >> row) & 1u;
                hard[static_cast<std::size_t>(py) *
                         static_cast<std::size_t>(kGlyphPxW) +
                     static_cast<std::size_t>(px)] = on ? uint8_t{255} : uint8_t{0};
            }
        }

        // One 3x3 box pass, reads clamped to the glyph rect.
        for (int py = 0; py < kGlyphPxH; ++py) {
            for (int px = 0; px < kGlyphPxW; ++px) {
                int sum = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int sx = px + dx;
                        int sy = py + dy;
                        if (sx < 0) sx = 0;
                        if (sy < 0) sy = 0;
                        if (sx >= kGlyphPxW) sx = kGlyphPxW - 1;
                        if (sy >= kGlyphPxH) sy = kGlyphPxH - 1;
                        sum += hard[static_cast<std::size_t>(sy) *
                                        static_cast<std::size_t>(kGlyphPxW) +
                                    static_cast<std::size_t>(sx)];
                    }
                }
                soft[static_cast<std::size_t>(py) *
                         static_cast<std::size_t>(kGlyphPxW) +
                     static_cast<std::size_t>(px)] =
                    static_cast<uint8_t>(sum / 9);
            }
        }

        // Blit into the cell.
        const int cx = (g % kAtlasCols) * kCellPx + kCellMargin;
        const int cy = (g / kAtlasCols) * kCellPx + kCellMargin;
        for (int py = 0; py < kGlyphPxH; ++py) {
            for (int px = 0; px < kGlyphPxW; ++px) {
                atlas[static_cast<std::size_t>(cy + py) *
                          static_cast<std::size_t>(kAtlasW) +
                      static_cast<std::size_t>(cx + px)] =
                    soft[static_cast<std::size_t>(py) *
                             static_cast<std::size_t>(kGlyphPxW) +
                         static_cast<std::size_t>(px)];
            }
        }
    }

    // The solid block, filled edge to edge so its centre is unambiguously 1.0.
    const int sx0 = (kSolidCell % kAtlasCols) * kCellPx;
    const int sy0 = (kSolidCell / kAtlasCols) * kCellPx;
    for (int py = 0; py < kCellPx; ++py) {
        for (int px = 0; px < kCellPx; ++px) {
            atlas[static_cast<std::size_t>(sy0 + py) *
                      static_cast<std::size_t>(kAtlasW) +
                  static_cast<std::size_t>(sx0 + px)] = 255u;
        }
    }

    return atlas;
}

}  // namespace apricot
