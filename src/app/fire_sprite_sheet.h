#pragma once

#include <cstddef>

namespace apricot {

// The flame atlas cooked by tools/cook_fire_sprites.py from the supplied fire
// spritesheet. GENERATED — edit the cooker, not this file.
//
// These three numbers live here rather than beside the drawing code because
// the cooker and the renderer must agree about them exactly. An atlas read
// with the wrong column count does not fail: it plays a fire made of halves of
// two frames, which looks like a shader bug and is not one.
//
// Frames run LEFT TO RIGHT, TOP TO BOTTOM. gfx/texture.h flips images
// vertically on load (the UV convention the imported model paint was authored
// in), so row 0 of the file is the TOP of the texture in UV space and the V
// range of a row has to be worked out from the flip — see fire_sprite_uv().
inline constexpr std::size_t kFireSheetColumns = 12;
inline constexpr std::size_t kFireSheetRows = 11;
inline constexpr std::size_t kFireSheetFrames = 132;
inline constexpr int kFireSheetCellPixels = 128;
inline constexpr const char* kFireSheetAsset = "textures/effects/fire_sheet.png";

// Half a texel in, so a bilinear tap at a frame's edge cannot reach into the
// next one. The mip chain still averages further than this at coarse levels,
// which is what the cooker's RGB dilation is for; the inset handles level 0.
inline constexpr float kFireSheetInset =
    0.5f / static_cast<float>(kFireSheetCellPixels);

}  // namespace apricot
