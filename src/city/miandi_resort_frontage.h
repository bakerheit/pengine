#pragma once

#include <array>
#include <vector>

#include "city/start_area.h"

namespace apricot::city {

// The renderer owns the actual palm prototypes.  These anchors deliberately
// keep the forecourt readable without baking box-shaped stand-ins for trees.
struct MiandiResortPalm {
    Vec2 centre{};
    float height_m = 8.0f;
    float yaw_deg = 0.0f;
};

inline constexpr std::array<MiandiResortPalm, 6> kMiandiResortPalms{{
    {{83.0f, -50.0f}, 9.5f, 18.0f},
    {{83.0f, -10.0f}, 8.0f, 132.0f},
    {{83.0f, 10.0f}, 10.5f, 246.0f},
    {{83.0f, 49.0f}, 8.5f, 308.0f},
    {{82.0f, 62.0f}, 9.0f, 74.0f},
    {{45.0f, -65.0f}, 7.5f, 196.0f},
}};

inline void resort_frontage_box(std::vector<BuildingPiece>& out,
                                const char* name, Vec2 centre, float bottom,
                                float width, float height, float depth,
                                BuildingFinish finish, bool solid = false,
                                float yaw = 0.0f) {
    out.push_back({name, centre, bottom, width, height, depth, finish, solid,
                   0.0f, yaw, 0.0f});
}

// OD-1 local coordinates. Compact terraces run from the wall skin at x=78.15
// to the sidewalk at x=86. Panels meet the 4.8 m lobby corridors edge-to-edge,
// avoiding coplanar overlap. Keep the outer walking strip free of furniture.
inline std::vector<BuildingPiece> bake_miandi_resort_frontage() {
    std::vector<BuildingPiece> out;
    out.reserve(40);

    constexpr float kPavingBottom = .12f;
    constexpr float kPavingHeight = .08f;
    resort_frontage_box(out, "Miandi resort connecting terrace", {82.075f, 0.0f},
                        kPavingBottom, 7.85f, kPavingHeight, 8.0f,
                        BuildingFinish::Concrete);
    resort_frontage_box(out, "Miandi Bellmar south terrace island",
                        {82.075f, -44.2f}, kPavingBottom, 7.85f,
                        kPavingHeight, 25.6f, BuildingFinish::White);
    resort_frontage_box(out, "Miandi Bellmar north terrace island",
                        {82.075f, -15.3f}, kPavingBottom, 7.85f,
                        kPavingHeight, 22.6f, BuildingFinish::White);
    resort_frontage_box(out, "Miandi Maravelle south terrace island",
                        {82.075f, 14.8f}, kPavingBottom, 7.85f,
                        kPavingHeight, 21.6f, BuildingFinish::Concrete);
    resort_frontage_box(out, "Miandi Maravelle north terrace island",
                        {82.075f, 47.7f}, kPavingBottom, 7.85f,
                        kPavingHeight, 34.6f, BuildingFinish::Concrete);

    // Alternating valances sit just above the already-authored cafe canopy.
    // Their ends stay within the compact corner canopy.
    for (std::size_t i = 0; i < 4; ++i) {
        const float x = 59.0f + static_cast<float>(i) * 4.0f;
        resort_frontage_box(
            out, i % 2 == 0 ? "Miandi cafe shade stripe buttercream"
                             : "Miandi cafe shade stripe coral",
            {x, -65.0f}, 4.31f, 3.0f, .06f, 9.0f,
            i % 2 == 0 ? BuildingFinish::Yellow : BuildingFinish::RedTrim);
    }

    const auto add_planter_bench_group = [&](const char* name, Vec2 planter,
                                             Vec2 bench) {
        // A real planter reads as a raised rim around recessed soil, rather
        // than a colored foliage block.  Live foliage comes from the parent
        // only when a proper mesh is available.
        resort_frontage_box(out, name, {planter.x - 1.01f, planter.z}, .20f,
                            .18f, .58f, 2.8f, BuildingFinish::WarmWall, true);
        resort_frontage_box(out, "Miandi resort planter rim", {planter.x + 1.01f, planter.z},
                            .20f, .18f, .58f, 2.8f, BuildingFinish::WarmWall, true);
        for (float z : {-1.31f, 1.31f})
            resort_frontage_box(out, "Miandi resort planter rim", {planter.x, planter.z + z},
                                .20f, 1.84f, .58f, .18f, BuildingFinish::WarmWall, true);
        resort_frontage_box(out, "Miandi resort planter soil", planter, .70f,
                            1.84f, .06f, 2.28f, BuildingFinish::DarkRoof);
        for (float x : {-1.55f, 1.55f})
            resort_frontage_box(out, "Miandi resort entry bench support", {bench.x + x, bench.z},
                                .20f, .18f, .25f, .42f, BuildingFinish::Steel, true);
        resort_frontage_box(out, "Miandi resort entry bench seat", bench,
                            .45f, 4.2f, .10f, .58f, BuildingFinish::Steel, true);
        resort_frontage_box(out, "Miandi resort entry bench back",
                            {bench.x, bench.z + .22f}, .55f, 4.2f, .45f, .12f,
                            BuildingFinish::Steel);
    };
    add_planter_bench_group("Miandi Bellmar south planter grouping",
                            {80.0f, -46.0f}, {81.0f, -39.0f});
    add_planter_bench_group("Miandi Bellmar north planter grouping",
                            {80.0f, -13.0f}, {81.0f, -20.0f});
    add_planter_bench_group("Miandi Maravelle paired planter grouping",
                            {80.0f, 18.5f}, {81.0f, 39.0f});

    // Cafe furniture is substantial enough to collide; table tops, chair
    // backs and awning fabric remain visual detail at integration.
    for (float table_z : {-68.0f, -62.0f}) {
      for (float x : {60.0f, 72.0f}) {
        resort_frontage_box(out, "Miandi cafe table pedestal", {x, table_z},
                            .20f, .32f, .62f, .32f, BuildingFinish::Steel, true);
        resort_frontage_box(out, "Miandi cafe table top", {x, table_z}, .80f,
                            1.25f, .10f, 1.25f, BuildingFinish::Yellow);
        for (float side : {-1.0f, 1.0f}) {
            const float z = table_z + side * 1.25f;
            resort_frontage_box(out, "Miandi cafe chair seat", {x, z}, .52f,
                                .62f, .08f, .62f, BuildingFinish::Steel, true);
            resort_frontage_box(out, "Miandi cafe chair back", {x, z + side * .25f},
                                .60f, .62f, .42f, .10f, BuildingFinish::Steel);
            for (float leg_x : {-.22f, .22f})
              for (float leg_z : {-.22f, .22f})
                resort_frontage_box(out, "Miandi cafe chair leg", {x + leg_x, z + leg_z},
                                    .20f, .08f, .32f, .08f, BuildingFinish::Steel, true);
        }
      }
    }

    return out;
}

}  // namespace apricot::city
