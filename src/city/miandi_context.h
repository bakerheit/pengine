#pragma once

#include <cstddef>
#include <vector>

#include "city/building_creator.h"
#include "city/start_area.h"

namespace apricot {
namespace city {

inline constexpr StartSite kMiandiContextSite{
    "Miandi context infill", {7500.0f, 8400.0f}, 1.0f, 0.0f,
    {0.0f, 0.0f}, 1800.0f, 1400.0f, 8.0f, 900.0f};

// One record per occupied 200 m grid block. These are intentionally separate
// from the four finished hero parcels: this package is the cheap city wall
// around them, not a second description of those buildings.
inline constexpr Vec2 kMiandiContextBlockCenters[] = {
    {-300.0f, -100.0f}, {-100.0f, -100.0f}, {300.0f, -100.0f},
    {-500.0f, 100.0f},  {-300.0f, 100.0f},  {-100.0f, 100.0f},
    {100.0f, 100.0f},   {300.0f, 100.0f},   {500.0f, 100.0f},
    {-500.0f, 300.0f},  {-300.0f, 300.0f},  {300.0f, 300.0f},
};

inline constexpr std::size_t kMiandiContextBlockCount =
    sizeof(kMiandiContextBlockCenters) /
    sizeof(kMiandiContextBlockCenters[0]);

// Second-wave nightlife packages retain the stable block registry above, but
// replace these coarse shells outright. Keeping the identity list stable lets
// map/integration tests reason about the original grid without ever drawing two
// buildings on the same parcel.
inline constexpr bool miandi_context_block_is_replaced(std::size_t index) {
    return index == 3u || index == 4u || index == 7u || index == 8u;
}

inline std::vector<BuildingPiece> bake_miandi_context() {
    std::vector<BuildingPiece> out;
    out.reserve(430);

    const auto add = [&](const char* name, Vec2 centre, float bottom,
                         float width, float height, float depth,
                         BuildingFinish finish, bool solid) {
        out.push_back({name, centre, bottom, width, height, depth, finish,
                       solid});
    };
    const auto at = [&](std::size_t i, float dx, float dz) {
        return Vec2{kMiandiContextBlockCenters[i].x + dx,
                    kMiandiContextBlockCenters[i].z + dz};
    };

    // The west and central blocks keep their original massing, but get a
    // modest, repeatable facade kit: real room-window rhythm above sealed
    // storefront glazing, then roof and rain details. These remain visual-only
    // because the coarse shells deliberately stay the whole collision story.
    for (std::size_t i : {0u, 1u, 2u}) {
        const float left_h = 8.0f + static_cast<float>(i % 3u) * 1.5f;
        const float right_h = 7.0f + static_cast<float>((i + 1u) % 3u);
        const BuildingFinish wall = i % 2u == 0u ? BuildingFinish::WarmWall
                                                  : BuildingFinish::Brick;
        add("miandi infill low-rise lot", at(i, 0.0f, 0.0f), 0.0f, 150.0f,
            0.12f, 140.0f, BuildingFinish::Asphalt, false);
        add("miandi infill low-rise shell west", at(i, -29.0f, 2.0f), 0.12f,
            52.0f, left_h, 82.0f, wall, true);
        add("miandi infill low-rise shell east", at(i, 29.0f, 8.0f), 0.12f,
            52.0f, right_h, 68.0f,
            i % 2u == 0u ? BuildingFinish::White : BuildingFinish::TealDoor,
            true);
        add("miandi infill low-rise rear service", at(i, 0.0f, 47.0f), 0.12f,
            82.0f, 5.0f, 18.0f, BuildingFinish::Concrete, true);
        add("miandi infill low-rise front awning", at(i, -29.0f, -43.0f),
            4.2f, 44.0f, 0.22f, 4.5f, BuildingFinish::RedTrim, false);
        add("miandi infill low-rise side awning", at(i, 29.0f, -30.0f), 3.8f,
            42.0f, 0.22f, 4.0f, BuildingFinish::Yellow, false);
        add("miandi infill low-rise roof vent", at(i, -39.0f, 28.0f),
            0.12f + left_h, 5.0f, 1.4f, 5.0f, BuildingFinish::Steel, false);
        add("miandi infill low-rise parapet trim", at(i, 26.0f, 40.0f),
            0.12f + right_h, 40.0f, 0.24f, 0.5f, BuildingFinish::White,
            false);

        // West shopfront: six evenly-spaced apartment windows are set proud
        // of the front wall with their own heads, sills, and jambs. The broad
        // sill and slightly deeper frame make the flat shell read at an angle.
        for (float x : {-49.0f, -41.0f, -33.0f, -25.0f, -17.0f, -9.0f}) {
            add("miandi infill low-rise room window glass", at(i, x, -39.18f),
                5.0f, 5.8f, 1.85f, 0.16f, BuildingFinish::Glass, false);
            add("miandi infill low-rise window sill", at(i, x, -39.37f),
                4.78f, 6.5f, 0.18f, 0.38f, BuildingFinish::White, false);
            add("miandi infill low-rise window head", at(i, x, -39.37f),
                6.98f, 6.5f, 0.18f, 0.38f, BuildingFinish::White, false);
            add("miandi infill low-rise window frame jamb", at(i, x - 3.15f,
                -39.37f), 4.78f, 0.24f, 2.38f, 0.38f,
                BuildingFinish::White, false);
            add("miandi infill low-rise window frame jamb", at(i, x + 3.15f,
                -39.37f), 4.78f, 0.24f, 2.38f, 0.38f,
                BuildingFinish::White, false);
        }

        // These are intentionally sealed glass bays, not doors: the context
        // buildings are closed and must never imply an entrance into a shell.
        for (float x : {-48.0f, -36.0f, -24.0f, -12.0f}) {
            add("miandi infill low-rise sealed storefront glazing",
                at(i, x, -39.22f), 0.62f, 9.4f, 3.05f, 0.18f,
                BuildingFinish::Glass, false);
            add("miandi infill low-rise storefront sill", at(i, x, -39.41f),
                0.38f, 10.0f, 0.18f, 0.4f, BuildingFinish::Steel, false);
            add("miandi infill low-rise storefront frame", at(i, x, -39.41f),
                3.92f, 10.0f, 0.22f, 0.4f, BuildingFinish::Steel, false);
            add("miandi infill low-rise shaded storefront valance",
                at(i, x, -39.76f), 4.13f, 10.4f, 0.38f, 0.9f,
                i == 1u ? BuildingFinish::Yellow : BuildingFinish::RedTrim,
                false);
        }
        for (float x : {-53.0f, -42.0f, -30.0f, -18.0f, -6.0f})
            add("miandi infill low-rise storefront mullion", at(i, x, -39.43f),
                0.38f, 0.28f, 3.72f, 0.42f, BuildingFinish::Steel, false);

        // The east shell has its own, shorter street face at z=-26. Layer it
        // like a small apartment-over-shops building instead of leaving the
        // balcony fins against a blank wall. Its bays are sealed glazing, not
        // implied doors, so these remain clearly closed context buildings.
        for (float x : {8.0f, 17.0f, 26.0f, 35.0f, 44.0f}) {
            add("miandi infill low-rise east front room window glass",
                at(i, x, -26.18f), 4.75f, 6.5f, 1.7f, 0.16f,
                BuildingFinish::Glass, false);
            add("miandi infill low-rise east front window sill",
                at(i, x, -26.37f), 4.53f, 7.1f, 0.18f, 0.38f,
                BuildingFinish::White, false);
            add("miandi infill low-rise east front window head",
                at(i, x, -26.37f), 6.58f, 7.1f, 0.18f, 0.38f,
                BuildingFinish::White, false);
            add("miandi infill low-rise east front window frame",
                at(i, x - 3.45f, -26.37f), 4.53f, 0.24f, 2.22f, 0.38f,
                BuildingFinish::White, false);
            add("miandi infill low-rise east front window frame",
                at(i, x + 3.45f, -26.37f), 4.53f, 0.24f, 2.22f, 0.38f,
                BuildingFinish::White, false);
        }
        for (float x : {8.0f, 18.0f, 28.0f, 38.0f, 48.0f}) {
            add("miandi infill low-rise east front sealed storefront glazing",
                at(i, x, -26.22f), 0.62f, 8.4f, 3.05f, 0.18f,
                BuildingFinish::Glass, false);
            add("miandi infill low-rise east front shaded storefront valance",
                at(i, x, -26.76f), 4.13f, 9.0f, 0.38f, 0.9f,
                i == 1u ? BuildingFinish::RedTrim : BuildingFinish::Yellow,
                false);
        }
        add("miandi infill low-rise east front storefront sill", at(i, 29.0f,
            -26.41f), 0.38f, 51.0f, 0.18f, 0.4f, BuildingFinish::Steel,
            false);
        add("miandi infill low-rise east front storefront frame", at(i, 29.0f,
            -26.41f), 3.92f, 51.0f, 0.22f, 0.4f, BuildingFinish::Steel,
            false);
        for (float x : {3.6f, 13.0f, 23.0f, 33.0f, 43.0f, 52.4f})
            add("miandi infill low-rise east front storefront mullion",
                at(i, x, -26.43f), 0.38f, 0.28f, 3.72f, 0.42f,
                BuildingFinish::Steel, false);

        // Outer sides are visible from the cross streets, so each gets a
        // smaller run of framed windows rather than a warehouse-like blank
        // elevation. Frames sit farther out than the glazing for readable
        // depth without changing the shell footprint.
        for (float z : {-28.0f, -12.0f, 4.0f, 20.0f}) {
            add("miandi infill low-rise side room window glass",
                at(i, -55.18f, z), 4.7f, 0.16f, 1.9f, 8.6f,
                BuildingFinish::Glass, false);
            add("miandi infill low-rise side window sill", at(i, -55.37f, z),
                4.48f, 0.38f, 0.18f, 9.3f, BuildingFinish::White, false);
            add("miandi infill low-rise side window head", at(i, -55.37f, z),
                6.72f, 0.38f, 0.18f, 9.3f, BuildingFinish::White, false);
            add("miandi infill low-rise side window frame", at(i, -55.37f,
                z - 4.55f), 4.48f, 0.38f, 2.42f, 0.24f,
                BuildingFinish::White, false);
            add("miandi infill low-rise side window frame", at(i, -55.37f,
                z + 4.55f), 4.48f, 0.38f, 2.42f, 0.24f,
                BuildingFinish::White, false);
        }
        for (float z : {-17.0f, -1.0f, 15.0f, 31.0f}) {
            add("miandi infill low-rise side room window glass",
                at(i, 55.18f, z), 4.45f, 0.16f, 1.9f, 8.6f,
                BuildingFinish::Glass, false);
            add("miandi infill low-rise side window sill", at(i, 55.37f, z),
                4.23f, 0.38f, 0.18f, 9.3f, BuildingFinish::White, false);
            add("miandi infill low-rise side window head", at(i, 55.37f, z),
                6.45f, 0.38f, 0.18f, 9.3f, BuildingFinish::White, false);
            add("miandi infill low-rise side window frame", at(i, 55.37f,
                z - 4.55f), 4.23f, 0.38f, 2.42f, 0.24f,
                BuildingFinish::White, false);
            add("miandi infill low-rise side window frame", at(i, 55.37f,
                z + 4.55f), 4.23f, 0.38f, 2.42f, 0.24f,
                BuildingFinish::White, false);
        }

        add("miandi infill low-rise parapet coping front", at(i, -29.0f,
            -39.38f), 0.12f + left_h, 53.0f, 0.3f, 0.55f,
            BuildingFinish::White, false);
        add("miandi infill low-rise parapet coping side west", at(i, -55.3f,
            2.0f), 0.12f + left_h, 0.55f, 0.3f, 83.0f,
            BuildingFinish::White, false);
        add("miandi infill low-rise parapet coping side east", at(i, 55.3f,
            8.0f), 0.12f + right_h, 0.55f, 0.3f, 69.0f,
            BuildingFinish::White, false);
        for (float x : {-42.0f, -28.0f, 18.0f})
            add("miandi infill low-rise roof vent cap", at(i, x, 24.0f),
                0.12f + (x < 0.0f ? left_h : right_h), 3.2f, 1.0f, 3.2f,
                BuildingFinish::Steel, false);
        add("miandi infill low-rise downpipe west", at(i, -55.45f, -36.0f),
            0.2f, 0.32f, left_h - 0.4f, 0.32f, BuildingFinish::Steel, false);
        add("miandi infill low-rise downpipe east", at(i, 55.45f, -23.0f),
            0.2f, 0.32f, right_h - 0.4f, 0.32f, BuildingFinish::Steel, false);
        for (float x : {13.0f, 21.0f, 29.0f, 37.0f, 45.0f, 51.0f})
            add("miandi infill low-rise apartment decorative fin",
                at(i, x, -26.36f), 4.2f, 0.34f, 2.9f, 0.72f,
                BuildingFinish::TealDoor, false);
        add("miandi infill low-rise apartment balcony slab", at(i, 31.0f,
            -26.72f), 5.05f, 36.0f, 0.2f, 1.6f, BuildingFinish::Concrete,
            false);
        add("miandi infill low-rise apartment balcony rail", at(i, 31.0f,
            -27.38f), 5.25f, 35.0f, 0.72f, 0.16f, BuildingFinish::Steel,
            false);
    }

    // Four downtown parcels use different shaft proportions and heights. A
    // podium, setback, shaft, crown, balcony bands, and roof plant keep this
    // from becoming twelve copies of one tower.
    constexpr float shaft_widths[] = {38.0f, 46.0f, 34.0f, 52.0f};
    constexpr float shaft_depths[] = {52.0f, 44.0f, 58.0f, 48.0f};
    constexpr float shaft_heights[] = {44.0f, 58.0f, 68.0f, 50.0f};
    constexpr BuildingFinish shaft_finishes[] = {
        BuildingFinish::Glass, BuildingFinish::White, BuildingFinish::Glass,
        BuildingFinish::WarmWall};
    for (std::size_t n = 0; n < 4u; ++n) {
        const std::size_t i = 5u + n;
        if (miandi_context_block_is_replaced(i)) continue;
        const float w = shaft_widths[n];
        const float d = shaft_depths[n];
        const float h = shaft_heights[n];
        add("miandi infill downtown lot", at(i, 0.0f, 0.0f), 0.0f, 150.0f,
            0.12f, 140.0f, BuildingFinish::Asphalt, false);
        add("miandi infill downtown podium", at(i, 0.0f, 2.0f), 0.12f,
            112.0f, 8.0f, 92.0f,
            n % 2u == 0u ? BuildingFinish::Concrete : BuildingFinish::White,
            true);
        add("miandi infill downtown setback", at(i, 0.0f, 5.0f), 8.12f,
            w + 10.0f, 3.0f, d + 10.0f, BuildingFinish::White, true);
        add("miandi infill downtown glass shaft", at(i, 0.0f, 2.0f), 11.12f,
            w, h, d, shaft_finishes[n], true);
        add("miandi infill downtown crown", at(i, 0.0f, 2.0f), 11.12f + h,
            w + 8.0f, 3.0f, d + 8.0f, BuildingFinish::Yellow, false);
        add("miandi infill downtown balcony front", at(i, 0.0f, 2.0f),
            22.0f, w + 10.0f, 0.22f, d + 10.0f, BuildingFinish::Steel,
            false);
        add("miandi infill downtown balcony mid", at(i, 0.0f, 2.0f),
            36.0f, w + 10.0f, 0.22f, d + 10.0f, BuildingFinish::Steel,
            false);
        add("miandi infill downtown roof plant", at(i, -w * 0.25f, 2.0f),
            11.12f + h + 3.0f, 7.0f, 2.0f, 7.0f, BuildingFinish::Steel,
            false);
    }

    // A low civic/plaza block breaks the skyline rhythm at the south edge of
    // the grid and leaves a readable public forecourt.
    {
        constexpr std::size_t i = 9u;
        add("miandi infill civic lot", at(i, 0.0f, 0.0f), 0.0f, 150.0f,
            0.12f, 140.0f, BuildingFinish::Asphalt, false);
        add("miandi infill civic hall", at(i, 0.0f, 5.0f), 0.12f, 104.0f,
            12.0f, 76.0f, BuildingFinish::WarmWall, true);
        add("miandi infill civic wing west", at(i, -39.0f, 10.0f), 0.12f,
            24.0f, 6.0f, 48.0f, BuildingFinish::White, true);
        add("miandi infill civic wing east", at(i, 39.0f, 10.0f), 0.12f,
            24.0f, 6.0f, 48.0f, BuildingFinish::Concrete, true);
        add("miandi infill civic plaza", at(i, 0.0f, -48.0f), 0.13f, 116.0f,
            0.08f, 28.0f, BuildingFinish::Concrete, false);
        add("miandi infill civic canopy", at(i, 0.0f, -31.0f), 5.5f, 48.0f,
            0.22f, 4.0f, BuildingFinish::TealDoor, false);
        add("miandi infill civic column west", at(i, -22.0f, -31.0f), 0.12f,
            0.7f, 5.5f, 0.7f, BuildingFinish::Steel, false);
        add("miandi infill civic column east", at(i, 22.0f, -31.0f), 0.12f,
            0.7f, 5.5f, 0.7f, BuildingFinish::Steel, false);
        add("miandi infill civic fountain", at(i, 0.0f, -51.0f), 0.14f,
            8.0f, 0.25f, 5.0f, BuildingFinish::PoolWater, false);
    }

    // Port Sol gets broad, low sheds, open yards, roof vents, and one extra
    // gantry silhouette. The gantry is visual-only until a later port package
    // owns its operational collision and vehicle clearances.
    for (std::size_t n = 0; n < 2u; ++n) {
        const std::size_t i = 10u + n;
        add("miandi infill port lot", at(i, 0.0f, 0.0f), 0.0f, 150.0f,
            0.12f, 140.0f, BuildingFinish::Asphalt, false);
        add("miandi infill port warehouse", at(i, -27.0f, 5.0f), 0.12f,
            66.0f, 9.0f, 82.0f, BuildingFinish::Concrete, true);
        add("miandi infill port loading shed", at(i, 29.0f, 8.0f), 0.12f,
            54.0f, 7.0f, 70.0f,
            n == 0u ? BuildingFinish::Steel : BuildingFinish::Brick, true);
        add("miandi infill port service yard", at(i, 0.0f, 51.0f), 0.13f,
            120.0f, 0.08f, 26.0f, BuildingFinish::Concrete, false);
        add("miandi infill port roof vent", at(i, -42.0f, 25.0f), 9.12f,
            6.0f, 1.8f, 8.0f, BuildingFinish::Steel, false);
        add("miandi infill port roof vent 2", at(i, 14.0f, 28.0f), 7.12f,
            6.0f, 1.8f, 8.0f, BuildingFinish::Steel, false);
        add("miandi infill port gantry leg", at(i, -42.0f, -38.0f), 0.12f,
            1.2f, 10.0f, 1.2f, BuildingFinish::Steel, false);
        add("miandi infill port gantry leg 2", at(i, 42.0f, -38.0f), 0.12f,
            1.2f, 10.0f, 1.2f, BuildingFinish::Steel, false);
        add("miandi infill port gantry beam", at(i, 0.0f, -38.0f), 9.2f,
            86.0f, 1.2f, 1.2f, BuildingFinish::Steel, false);
        add("miandi infill port barrier", at(i, 0.0f, 35.0f), 0.12f,
            70.0f, 0.8f, 0.5f, BuildingFinish::Yellow, false);
    }

    return out;
}

static_assert(kMiandiContextBlockCount == 12,
              "Miandi context must occupy exactly twelve blocks");

}  // namespace city
}  // namespace apricot
