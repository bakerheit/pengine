#pragma once

#include <initializer_list>
#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Ground-floor finish, selective wall protection, and fixture lenses for the
// hospital's five occupied bars. Courtyard rectangles stay open to the sky.
inline std::vector<StartPart> bake_hospital_detail_surfaces() {
    std::vector<StartPart> out;
    out.reserve(104);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    // The five footprints only meet at their edges. Their top is exactly the
    // 0.315 m furniture datum, and all finish layers remain non-solid.
    add("hospital detail north bar floor skin tex terrazzo", 94.5f, 13.0f,
        0.30f, 219.0f, 0.015f, 42.0f, StartFinish::White);
    add("hospital detail west bar floor skin tex terrazzo", 15.0f, 86.0f,
        0.30f, 60.0f, 0.015f, 104.0f, StartFinish::White);
    add("hospital detail east bar floor skin tex terrazzo", 174.0f, 86.0f,
        0.30f, 60.0f, 0.015f, 104.0f, StartFinish::White);
    add("hospital detail south support floor skin tex terrazzo", 94.5f,
        119.0f, 0.30f, 99.0f, 0.015f, 38.0f, StartFinish::White);
    add("hospital detail clinical spine floor skin tex terrazzo", 94.5f,
        69.0f, 0.30f, 99.0f, 0.015f, 18.0f, StartFinish::White);

    // Ceiling tiles follow those same five bars; neither healing court gets a
    // ceiling panel, so both remain genuine open-air volumes.
    add("hospital detail north bar ceiling tex ceiling", 94.5f, 13.0f,
        3.43f, 219.0f, 0.04f, 42.0f, StartFinish::White);
    add("hospital detail west bar ceiling tex ceiling", 15.0f, 86.0f,
        3.43f, 60.0f, 0.04f, 104.0f, StartFinish::White);
    add("hospital detail east bar ceiling tex ceiling", 174.0f, 86.0f,
        3.43f, 60.0f, 0.04f, 104.0f, StartFinish::White);
    add("hospital detail south support ceiling tex ceiling", 94.5f, 119.0f,
        3.43f, 99.0f, 0.04f, 38.0f, StartFinish::White);
    add("hospital detail clinical spine ceiling tex ceiling", 94.5f, 69.0f,
        3.43f, 99.0f, 0.04f, 18.0f, StartFinish::White);

    struct Gap {
        float start;
        float end;
    };

    // Split the paint liner and steel crash rail at every portal. A short
    // extra margin keeps rail ends clear of door swings and approach lanes.
    const auto add_wall_run = [&](bool horizontal, float start, float end,
                                  float liner_coord, float rail_coord,
                                  std::initializer_list<Gap> openings) {
        const auto emit_segment = [&](float u0, float u1) {
            if (u1 - u0 < 0.45f) return;
            const float length = u1 - u0;
            const float mid = (u0 + u1) * 0.5f;
            if (horizontal) {
                add("hospital detail interior wallpaint liner tex wallpaint",
                    mid, liner_coord, 0.315f, length, 1.20f, 0.04f,
                    StartFinish::White);
                add("hospital detail interior crash rail tex steel", mid,
                    rail_coord, 0.88f, length, 0.14f, 0.14f,
                    StartFinish::White);
            } else {
                add("hospital detail interior wallpaint liner tex wallpaint",
                    liner_coord, mid, 0.315f, 0.04f, 1.20f, length,
                    StartFinish::White);
                add("hospital detail interior crash rail tex steel",
                    rail_coord, mid, 0.88f, 0.14f, 0.14f, length,
                    StartFinish::White);
            }
        };

        float cursor = start;
        for (const Gap& opening : openings) {
            float gap_start = opening.start - 0.18f;
            float gap_end = opening.end + 0.18f;
            if (gap_start < start) gap_start = start;
            if (gap_end > end) gap_end = end;
            if (gap_start > cursor) emit_segment(cursor, gap_start);
            if (gap_end > cursor) cursor = gap_end;
        }
        if (cursor < end) emit_segment(cursor, end);
    };

    // Main north frontage: main, diagnostic, and ED walk-in portals, plus a
    // wider opening at x[56,64] to protect the diagnostic approach axis.
    add_wall_run(true, -15.0f, 204.0f, -7.80f, -7.75f,
                 {{-6.0f, 6.0f}, {56.0f, 64.0f}, {177.25f, 182.75f}});
    // Bellweather public door through the west inpatient frontage.
    add_wall_run(false, -8.0f, 138.0f, -14.80f, -14.75f,
                 {{55.4f, 60.6f}});

    // Public lobby's south edge and the two healing-court loops. These runs
    // stay against their wall faces, with every court portal left open.
    add_wall_run(true, 45.0f, 144.0f, 33.80f, 33.75f,
                 {{70.2f, 73.8f}, {116.2f, 119.8f}});
    add_wall_run(false, 34.0f, 60.0f, 44.80f, 44.75f,
                 {{45.2f, 48.8f}});
    add_wall_run(false, 34.0f, 60.0f, 144.20f, 144.25f,
                 {{45.2f, 48.8f}});
    add_wall_run(true, 45.0f, 144.0f, 60.20f, 60.25f,
                 {{70.1f, 73.9f}, {116.1f, 119.9f}});
    add_wall_run(true, 45.0f, 144.0f, 77.80f, 77.75f,
                 {{70.1f, 73.9f}, {116.1f, 119.9f}});
    add_wall_run(false, 78.0f, 100.0f, 44.80f, 44.75f,
                 {{87.2f, 90.8f}});
    add_wall_run(false, 78.0f, 100.0f, 144.20f, 144.25f,
                 {{87.2f, 90.8f}});
    add_wall_run(true, 45.0f, 144.0f, 100.20f, 100.25f,
                 {{70.1f, 73.9f}, {116.1f, 119.9f}});

    // Flat, low-profile lenses sit just below the ceiling slab, leaving a
    // small vertical gap to avoid coplanar faces. Parent wiring supplies light.
    constexpr Vec2 light_points[] = {
        {-10.0f, 4.0f}, {14.0f, 4.0f}, {34.0f, 3.0f}, {80.0f, 7.0f},
        {-9.0f, 14.0f}, {35.0f, 8.0f}, {14.0f, 16.0f}, {34.0f, 20.0f},
        {80.0f, 24.0f}, {104.0f, 18.0f},
        {161.0f, 57.0f}, {184.0f, 57.0f}, {160.0f, 68.0f}, {188.0f, 68.0f},
        {160.0f, 91.0f}, {172.0f, 97.0f}, {160.0f, 108.0f}, {188.0f, 108.0f},
        {-6.25f, 110.0f}, {3.25f, 110.0f}, {12.75f, 110.0f},
        {22.25f, 110.0f}, {-4.0f, 118.0f}, {22.0f, 118.0f},
        {60.0f, -1.0f}, {90.0f, -1.0f}, {125.0f, -1.0f}, {153.0f, -1.0f},
        {-8.0f, 42.0f}, {-8.0f, 70.0f}, {-8.0f, 89.0f},
        {26.0f, 45.0f}, {26.0f, 70.0f}, {26.0f, 90.0f},
        {63.0f, 110.0f}, {94.0f, 110.0f}, {129.0f, 110.0f},
        {60.0f, 69.0f}, {94.0f, 69.0f}, {130.0f, 69.0f},
    };
    for (const Vec2& point : light_points) {
        add("hospital interior ceiling light lens", point.x, point.z, 3.30f,
            1.00f, 0.10f, 0.32f, StartFinish::White);
    }

    return out;
}

}  // namespace apricot::city
