#pragma once

#include <cstdint>

namespace apricot::city {

// The physical receivers and their diffuse canvases are one contract. Keep
// them here so ImageGen prompts cannot quietly target an aspect ratio that the
// restaurant geometry does not have.
struct RestaurantMenuBoardDimensions {
    const char* receiver = nullptr;
    float width_m = 0.0f;
    float height_m = 0.0f;
    uint32_t canvas_width_px = 0;
    uint32_t canvas_height_px = 0;
    uint32_t safe_inset_px = 0;
    float backing_extra_width_m = 0.0f;
    float backing_extra_height_m = 0.0f;
};

// Two copies sit above the counter. Each is its own artwork receiver even
// while the current restaurant textures are shared, so a later menu refresh
// can split the left/right content without changing collision geometry.
inline constexpr RestaurantMenuBoardDimensions kInteriorMenuBoard{
    "quickbite interior menu sign face left/right", 4.80f, 1.20f, 2048u, 512u, 64u,
    0.0f, 0.10f};

// The driver sees this board head-on from the order lane. It must remain a
// portrait canvas rather than stretching a counter-board strip down its face.
inline constexpr RestaurantMenuBoardDimensions kDriveThroughMenuBoard{
    "quickbite drive-through menu sign face", 2.00f, 3.00f, 1024u, 1536u, 64u,
    0.22f, 0.22f};

constexpr float menu_board_backing_width_m(
        const RestaurantMenuBoardDimensions& board) {
    return board.width_m + board.backing_extra_width_m;
}

constexpr float menu_board_backing_height_m(
        const RestaurantMenuBoardDimensions& board) {
    return board.height_m + board.backing_extra_height_m;
}

// Each canvas has square physical pixels after its full image is mapped once
// over the face. These assertions catch a geometry or prompt-size edit that
// would otherwise distort type in one direction.
static_assert(4800u * 512u == 1200u * 2048u,
              "interior menu physical and raster aspects must match");
static_assert(2000u * 1536u == 3000u * 1024u,
              "drive-through menu physical and raster aspects must match");

}  // namespace apricot::city
