#pragma once

#include <cmath>
#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Two downtown blocks directly north of Tenth Street. The east edge stops
// before the diagonal arterial. The lot shares the
// hospital basis so its paint, signs, map footprint, and light anchors line up
// with the campus without pretending the lot is part of the clinical parcel.
inline constexpr Vec2 kHospitalNorthParkingCentre{46.0f, -62.0f};
inline constexpr float kHospitalNorthParkingWidthM = 146.0f;
inline constexpr float kHospitalNorthParkingDepthM = 38.0f;
inline constexpr StartSite kHospitalNorthParkingSite{
    "Vellum Regional Hospital North Visitor Parking",
    kHospitalSite.origin, kHospitalSite.cos_yaw, kHospitalSite.sin_yaw,
    kHospitalNorthParkingCentre, kHospitalNorthParkingWidthM,
    kHospitalNorthParkingDepthM, 12.0f, 2200.0f};

inline std::vector<StartPart> bake_hospital_north_parking() {
    std::vector<StartPart> out;
    out.reserve(280);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw;
        out.push_back(part);
    };

    add("hospital north visitor parking lot", 46.0f, -62.0f, 0.0f,
        146.0f, 0.10f, 38.0f, StartFinish::Asphalt);

    // The south curb leaves two deliberate gaps: a pedestrian crossing at
    // the main lobby axis and the existing Rook Lane north-stub throat.
    add("hospital north parking north curb", 46.0f, -80.82f, 0.10f,
        146.0f, 0.24f, 0.36f, StartFinish::Concrete, true);
    add("hospital north parking west curb", -26.82f, -62.0f, 0.10f,
        0.36f, 0.24f, 37.6f, StartFinish::Concrete, true);
    add("hospital north parking east curb", 118.82f, -62.0f, 0.10f,
        0.36f, 0.24f, 37.6f, StartFinish::Concrete, true);
    const struct CurbRun { float x; float width; } south_curbs[] = {
        {-15.0f, 24.0f}, {22.0f, 38.0f}, {85.0f, 68.0f},
    };
    for (const CurbRun& curb : south_curbs) {
        add("hospital north parking south curb", curb.x, -43.18f, 0.10f,
            curb.width, 0.24f, 0.36f, StartFinish::Concrete, true);
    }

    // A protected spine points straight at the main hospital doors. Its
    // crosswalk paint continues over Tenth Street without creating a raised
    // collision strip in the carriageway.
    add("hospital north parking pedestrian spine", 0.0f, -62.0f, 0.10f,
        5.0f, 0.06f, 37.2f, StartFinish::Concrete);
    for (int stripe = 0; stripe < 12; ++stripe) {
        add("hospital north parking Tenth Street crosswalk",
            0.0f, -41.0f + static_cast<float>(stripe) * 1.72f, 0.18f,
            5.0f, 0.025f, 0.62f, StartFinish::White);
    }

    // Three long parking bands make this read as a genuinely large surface
    // lot. Keep the cross aisle, light islands, and pedestrian spine
    // clear instead of drawing stall paint through them.
    const auto near = [](float x, float centre, float half_width) {
        return std::fabs(x - centre) < half_width;
    };
    const float row_z[] = {-49.0f, -62.0f, -75.0f};
    for (float z : row_z) {
        for (int line = 0; line < 44; ++line) {
            const float x = -22.0f + static_cast<float>(line) * 3.15f;
            const bool cross_aisle = near(x, 46.0f, 6.0f);
            const bool pedestrian = near(x, 0.0f, 4.5f);
            const bool light_island =
                ((z < -55.0f && near(x, 15.0f, 2.8f)) ||
                 near(x, 92.0f, 2.8f));
            if (cross_aisle || pedestrian || light_island) continue;
            add("hospital north parking stall stripe", x, z, 0.205f,
                0.12f, 0.025f, 5.4f, StartFinish::White);
        }
    }

    for (float x : {40.4f, 51.6f}) {
        add("hospital north parking cross aisle edge", x, -62.0f, 0.205f,
            0.14f, 0.025f, 27.0f, StartFinish::Yellow);
    }

    const auto add_east_arrow = [&](float x, float z) {
        add("hospital north parking direction arrow", x, z, 0.205f,
            2.2f, 0.025f, 0.18f, StartFinish::White);
        add("hospital north parking direction arrow", x + 0.82f, z - 0.42f,
            0.205f, 1.15f, 0.025f, 0.18f, StartFinish::White, false, 42.0f);
        add("hospital north parking direction arrow", x + 0.82f, z + 0.42f,
            0.205f, 1.15f, 0.025f, 0.18f, StartFinish::White, false, -42.0f);
    };
    add_east_arrow(45.0f, -54.8f);
    add_east_arrow(45.0f, -69.2f);

    // Six slim light islands sit between spaces, not in either access throat.
    // Each visible lens is consumed by the real tiled-light path after dusk.
    for (float z : {-45.4f, -78.6f}) {
        for (float x : {15.0f, 92.0f}) {
            add("hospital north parking light island", x, z, 0.10f,
                4.4f, 0.22f, 1.45f, StartFinish::Concrete, true);
            add("hospital north parking light island planting", x, z, 0.32f,
                3.8f, 0.38f, 0.95f, StartFinish::TealDoor);
            add("hospital north parking light pole", x, z, 0.32f,
                0.24f, 7.1f, 0.24f, StartFinish::Steel, true);
            add("hospital north parking light crossarm", x, z, 7.30f,
                3.0f, 0.16f, 0.20f, StartFinish::Steel);
            for (float dx : {-1.28f, 1.28f}) {
                add("hospital parking lot light lens", x + dx, z, 7.20f,
                    0.48f, 0.08f, 0.34f, StartFinish::White);
            }
        }
    }

    // One project-bound, one-shot sign face. It is large enough to read from
    // Tenth Street and points visitor traffic away from emergency receiving.
    for (float x : {-17.1f, -12.9f}) {
        add("hospital north parking wayfinding sign post", x, -44.6f,
            0.18f, 0.18f, 4.3f, 0.18f, StartFinish::Steel, true);
    }
    add("hospital north parking wayfinding sign backing", -15.0f, -44.55f,
        1.28f, 6.2f, 3.35f, 0.16f, StartFinish::Steel, true);
    add("hospital north parking wayfinding fitted face", -15.0f, -44.66f,
        1.38f, 6.0f, 3.15f, 0.04f, StartFinish::White);

    for (float x : {-3.8f, 3.8f}) {
        add("hospital north parking pedestrian bollard", x, -44.6f, 0.12f,
            0.24f, 0.92f, 0.24f, StartFinish::Steel, true);
    }

    return out;
}

}  // namespace apricot::city
