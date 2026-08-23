// The procedural glyph atlas.
//
// The failure this suite exists to catch: an atlas that generates without
// complaint, uploads without complaint, and is BLANK. On screen a blank atlas
// is indistinguishable from a HUD that was never drawn, from a HUD drawn off
// the edge of the viewport, and from a HUD drawn in the background colour. It
// is one bug wearing four disguises, and you cannot tell them apart by looking.
//
// So: build the real atlas and check that every printable character actually
// put ink in its own cell, and nowhere else.

#include <cstdio>

#include "gfx/glyph_atlas.h"
#include "test_assert.h"

using namespace apricot;

namespace {

uint8_t texel(const std::vector<uint8_t>& atlas, int x, int y) {
    return atlas[static_cast<std::size_t>(y) * static_cast<std::size_t>(kAtlasW) +
                 static_cast<std::size_t>(x)];
}

// Highest coverage inside one cell's glyph rect.
int cell_peak(const std::vector<uint8_t>& atlas, int cell) {
    const int cx = (cell % kAtlasCols) * kCellPx + kCellMargin;
    const int cy = (cell / kAtlasCols) * kCellPx + kCellMargin;
    int peak = 0;
    for (int y = 0; y < kGlyphPxH; ++y) {
        for (int x = 0; x < kGlyphPxW; ++x) {
            const int v = static_cast<int>(texel(atlas, cx + x, cy + y));
            if (v > peak) peak = v;
        }
    }
    return peak;
}

void the_atlas_is_the_size_it_says_it_is() {
    const std::vector<uint8_t> atlas = build_glyph_atlas();
    REQUIRE(atlas.size() == static_cast<std::size_t>(kAtlasW) *
                                static_cast<std::size_t>(kAtlasH));
    // 95 printable glyphs plus the solid block must fit in the cell grid with
    // nothing spilling off the end.
    REQUIRE(kCharCount + 1 <= kAtlasCols * kAtlasRows);
    REQUIRE(kSolidCell == kAtlasCols * kAtlasRows - 1);
    REQUIRE(kSolidCell >= kCharCount);
    apricot_test::pass("atlas dimensions and cell budget");
}

void every_printable_character_has_ink() {
    const std::vector<uint8_t> atlas = build_glyph_atlas();

    for (int c = kFirstChar; c < kFirstChar + kCharCount; ++c) {
        const int cell = c - kFirstChar;
        const int peak = cell_peak(atlas, cell);

        if (c == ' ') {
            REQUIRE_MSG(peak == 0, "space must be blank", "space");
            continue;
        }
        // Every other printable character must have put something down. A
        // single missing row in the font table shows up here as one silent
        // blank glyph, which on screen just looks like a typo.
        REQUIRE_MSG(peak > 0, "printable character produced a blank cell",
                    "coverage");
        // Softening must not have washed the glyph out to a grey smear.
        REQUIRE_MSG(peak > 100, "glyph coverage is too faint to read",
                    "coverage");
    }
    apricot_test::pass("all 94 non-space glyphs have readable ink");
}

void softening_does_not_leak_between_cells() {
    const std::vector<uint8_t> atlas = build_glyph_atlas();

    // Every cell's margin column and row must be untouched, so one letter's
    // antialiasing can never bleed into the letter next door. That bleed shows
    // up as a hairline of an unrelated character down the side of every glyph.
    for (int cell = 0; cell < kCharCount; ++cell) {
        const int x0 = (cell % kAtlasCols) * kCellPx;
        const int y0 = (cell / kAtlasCols) * kCellPx;
        for (int y = 0; y < kCellPx; ++y) {
            REQUIRE_MSG(texel(atlas, x0, y0 + y) == 0,
                        "cell margin column must stay empty", "bleed");
        }
        for (int x = 0; x < kCellPx; ++x) {
            REQUIRE_MSG(texel(atlas, x0 + x, y0) == 0,
                        "cell margin row must stay empty", "bleed");
        }
    }
    apricot_test::pass("no glyph bleeds outside its own cell");
}

void the_solid_block_is_actually_solid() {
    const std::vector<uint8_t> atlas = build_glyph_atlas();

    // This is what lets a filled HUD panel and a letter be the same draw call.
    // If its centre is anything but full coverage, every panel in the game
    // comes out semi-transparent and nobody knows why.
    const GlyphUV s = solid_uv();
    const int px = static_cast<int>(s.u0 * static_cast<float>(kAtlasW));
    const int py = static_cast<int>(s.v0 * static_cast<float>(kAtlasH));
    REQUIRE(texel(atlas, px, py) == 255);

    // A point sample, so all four corners share it and filtering cannot pull
    // in a neighbouring texel.
    REQUIRE(s.u0 == s.u1);
    REQUIRE(s.v0 == s.v1);
    apricot_test::pass("the solid block reads full coverage at its sample point");
}

void every_uv_stays_on_the_atlas() {
    for (int c = 0; c < 256; ++c) {
        const GlyphUV uv = glyph_uv(static_cast<char>(c));
        REQUIRE_MSG(uv.u0 >= 0.0f && uv.u1 <= 1.0f, "u outside the atlas", "uv");
        REQUIRE_MSG(uv.v0 >= 0.0f && uv.v1 <= 1.0f, "v outside the atlas", "uv");
        REQUIRE_MSG(uv.u1 > uv.u0, "glyph rect must have width", "uv");
        REQUIRE_MSG(uv.v1 > uv.v0, "glyph rect must have height", "uv");
    }
    apricot_test::pass("every char, printable or not, maps onto the atlas");
}

void unmapped_characters_become_a_visible_question_mark() {
    // Silently dropping an out-of-range byte hides a formatting bug. A '?' is
    // loud, which is the point.
    const int fallback = static_cast<int>('?') - kFirstChar;
    REQUIRE(glyph_cell('\n') == fallback);
    REQUIRE(glyph_cell('\t') == fallback);
    REQUIRE(glyph_cell(static_cast<char>(200)) == fallback);
    REQUIRE(glyph_cell('A') == static_cast<int>('A') - kFirstChar);
    apricot_test::pass("unmapped bytes render as '?', not as nothing");
}

void the_metrics_add_up() {
    const float h = 21.0f;
    REQUIRE_NEAR(static_cast<double>(text_unit_px(h)), 3.0, 1e-5);
    REQUIRE_NEAR(static_cast<double>(glyph_width_px(h)), 15.0, 1e-5);
    REQUIRE_NEAR(static_cast<double>(glyph_advance_px(h)), 18.0, 1e-5);

    REQUIRE(text_width_px("", h) == 0.0f);
    REQUIRE(text_width_px(nullptr, h) == 0.0f);
    // One glyph is exactly one glyph wide — no trailing spacing, or a centred
    // label sits half a space to the left of centre.
    REQUIRE_NEAR(static_cast<double>(text_width_px("X", h)), 15.0, 1e-5);
    REQUIRE_NEAR(static_cast<double>(text_width_px("XX", h)), 33.0, 1e-5);

    // And width grows with the string, which is all a centring caller needs.
    REQUIRE(text_width_px("LAP 1/3", h) > text_width_px("LAP", h));
    apricot_test::pass("text metrics are exact and have no trailing slop");
}

}  // namespace

int main() {
    the_atlas_is_the_size_it_says_it_is();
    every_printable_character_has_ink();
    softening_does_not_leak_between_cells();
    the_solid_block_is_actually_solid();
    every_uv_stays_on_the_atlas();
    unmapped_characters_become_a_visible_question_mark();
    the_metrics_add_up();
    return apricot_test::done("glyph_atlas_tests");
}
