#pragma once

#include <cmath>
#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Public-facing architecture and landscape for the ring-and-spine hospital.
// Everything is authored in kHospitalSite-local coordinates. The replacement
// shell owns walls, floors, roofs, and openings; this layer only adds facade
// depth, exterior fixtures, supported paving skins, and honest prop colliders.
inline std::vector<StartPart> bake_hospital_overhaul_public_realm() {
    std::vector<StartPart> out;
    out.reserve(480);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };

    const auto add_tree = [&](float x, float z, float scale,
                              float crown_yaw_deg) {
        add("hospital grounds tree trunk", x, z, 0.18f, 0.40f * scale,
            3.10f * scale, 0.40f * scale, StartFinish::Brick, true);
        add("hospital grounds tree canopy", x, z, 2.55f * scale,
            3.80f * scale, 1.75f * scale, 1.55f * scale,
            StartFinish::TealDoor, false, crown_yaw_deg);
        add("hospital grounds tree canopy", x, z, 2.82f * scale,
            3.15f * scale, 1.45f * scale, 1.35f * scale,
            StartFinish::TealDoor, false, crown_yaw_deg + 78.0f);
    };

    const auto add_low_bed = [&](float x, float z, float width, float depth,
                                 float yaw_deg, bool tree) {
        add("hospital public realm low planter curb", x, z, 0.14f, width,
            0.26f, depth, StartFinish::Concrete, true, yaw_deg);
        add("hospital public realm planter soil", x, z, 0.405f,
            width - 0.55f, 0.055f, depth - 0.55f, StartFinish::DarkRoof,
            false, yaw_deg);
        const float foliage_width = tree ? width * 0.38f : width * 0.72f;
        add("hospital public realm layered planting", x, z, 0.46f,
            foliage_width, tree ? 0.52f : 0.72f, depth - 0.90f,
            StartFinish::TealDoor, false, yaw_deg + 11.0f);
    };

    const auto add_bench = [&](float x, float z, float yaw_deg) {
        constexpr float kBackOffset = 0.31f;
        constexpr float kPi = 3.14159265358979323846f;
        const float yaw_radians = yaw_deg * kPi / 180.0f;
        const float back_x = x + std::sin(yaw_radians) * kBackOffset;
        const float back_z = z + std::cos(yaw_radians) * kBackOffset;
        add("hospital public realm bench plinth", x, z, 0.14f, 3.00f,
            0.22f, 0.76f, StartFinish::Concrete, true, yaw_deg);
        add("hospital public realm bench seat", x, z, 0.36f, 2.72f,
            0.16f, 0.60f, StartFinish::WarmWall, true, yaw_deg);
        add("hospital public realm bench back", back_x, back_z, 0.49f,
            2.72f, 0.78f, 0.12f, StartFinish::WarmWall, true, yaw_deg);
        for (float offset : {-1.05f, 1.05f}) {
            const float leg_x = x + std::cos(yaw_radians) * offset;
            const float leg_z = z - std::sin(yaw_radians) * offset;
            add("hospital public realm bench leg", leg_x, leg_z, 0.18f,
                0.14f, 0.32f, 0.42f, StartFinish::Steel, true, yaw_deg);
        }
    };

    const auto add_path_light = [&](float x, float z) {
        add("hospital grounds path light base", x, z, 0.14f, 0.46f,
            0.18f, 0.46f, StartFinish::Concrete, true);
        add("hospital grounds path light pole", x, z, 0.32f, 0.14f,
            2.38f, 0.14f, StartFinish::Steel, true);
        // Runtime lighting routes this exact name. The visible lens is never
        // solid, so the light cannot become a hidden pedestrian obstacle.
        add("hospital grounds path light lens", x, z, 2.70f, 0.48f,
            0.20f, 0.48f, StartFinish::White);
    };

    const auto add_wall_light = [&](float x) {
        add("hospital facade wall light body", x, -8.20f, 2.44f, 0.64f,
            0.30f, 0.18f, StartFinish::Steel);
        // Runtime lighting also consumes this exact receiver name.
        add("hospital facade wall light lens", x, -8.31f, 2.49f, 0.46f,
            0.19f, 0.08f, StartFinish::White);
    };

    const auto add_walk_segment = [&](const char* name, float x, float z,
                                      float length, float width,
                                      float yaw_deg) {
        add(name, x, z, 0.105f, length, 0.075f, width,
            StartFinish::Concrete, false, yaw_deg);
    };

    // ---------------------------------------------------------------------
    // North civic facade. The shell behind it spans x[-15,204] at z=-8.
    // Three material zones explain the building at a glance: public/lobby,
    // diagnostics, and emergency. Thin bands and mullions sit off the shell
    // and do not refill its real door and window openings.
    // ---------------------------------------------------------------------
    constexpr float kNorthFaceZ = -8.0f;
    constexpr float kFacadeLayerZ = kNorthFaceZ - 0.19f;
    for (float band_bottom : {3.12f, 6.52f, 9.92f, 13.32f}) {
        add("hospital public facade west floor band", 15.0f, kFacadeLayerZ,
            band_bottom, 59.4f, 0.34f, 0.24f, StartFinish::WarmWall);
        add("hospital diagnostic facade floor band", 94.5f, kFacadeLayerZ,
            band_bottom, 98.2f, 0.34f, 0.24f, StartFinish::Steel);
        add("hospital emergency facade floor band", 174.0f, kFacadeLayerZ,
            band_bottom, 59.2f, 0.34f, 0.24f, StartFinish::RedTrim);
    }

    // Grounded base courses give the long bar human scale without becoming a
    // second wall. The lobby section is split around the x=0 door axis.
    add("hospital public facade stone base west", -10.0f, kFacadeLayerZ,
        0.25f, 9.4f, 0.72f, 0.26f, StartFinish::Concrete);
    add("hospital public facade stone base east", 25.0f, kFacadeLayerZ,
        0.25f, 39.4f, 0.72f, 0.26f, StartFinish::Concrete);
    add("hospital diagnostic facade stone base", 94.5f, kFacadeLayerZ,
        0.25f, 98.2f, 0.72f, 0.26f, StartFinish::Concrete);
    add("hospital emergency facade stone base", 174.0f, kFacadeLayerZ,
        0.25f, 59.2f, 0.72f, 0.26f, StartFinish::Concrete);

    constexpr float kPublicMullions[] = {
        -11.5f, 11.5f, 27.0f, 35.0f, 43.0f,
        50.0f, 58.0f, 66.0f, 74.0f, 82.0f, 90.0f,
        98.0f, 106.0f, 114.0f, 122.0f, 130.0f, 138.0f,
    };
    for (float x : kPublicMullions) {
        add("hospital north facade vertical mullion", x,
            kNorthFaceZ - 0.25f, 0.86f, 0.20f, 12.30f, 0.18f,
            StartFinish::Steel);
    }
    for (float x : {149.0f, 157.0f, 165.0f, 173.0f, 181.0f, 189.0f,
                    197.0f, 203.0f}) {
        add("hospital emergency facade vertical mullion", x,
            kNorthFaceZ - 0.25f, 0.86f, 0.20f, 12.30f, 0.18f,
            StartFinish::Steel);
    }

    // Carry the north facade's measured window rhythm around the west
    // inpatient frontage. The fitted west-return art and Bellweather portal
    // each keep a full mullion-free bay; the thin fins stay outside glazing.
    constexpr float kWestFacadeLength = 146.0f;
    constexpr int kWestFacadeBayCount = 19;
    for (int bay = 1; bay < kWestFacadeBayCount; ++bay) {
        const float z = -8.0f +
                        kWestFacadeLength * static_cast<float>(bay) /
                            static_cast<float>(kWestFacadeBayCount);
        if ((z > 13.0f && z < 21.0f) || (z > 53.0f && z < 62.0f)) {
            continue;
        }
        add("hospital west inpatient facade vertical mullion", -15.43f, z,
            0.86f, 0.18f, 12.30f, 0.18f, StartFinish::Steel);
    }

    // Independent projecting shades make the window cadence read from the
    // street. The pieces stop at department seams instead of one repeated
    // strip stretched across the full 219 m facade.
    for (float bottom : {3.00f, 6.40f, 9.80f, 13.20f}) {
        add("hospital public facade deep sunshade", 37.0f, -8.62f, bottom,
            14.0f, 0.16f, 1.18f, StartFinish::Steel);
        add("hospital diagnostic facade deep sunshade west", 68.5f, -8.62f,
            bottom, 43.0f, 0.16f, 1.18f, StartFinish::Steel);
        add("hospital diagnostic facade deep sunshade east", 118.5f,
            -8.62f, bottom, 43.0f, 0.16f, 1.18f, StartFinish::Steel);
        add("hospital emergency facade deep sunshade", 174.0f, -8.62f,
            bottom, 55.0f, 0.16f, 1.18f, StartFinish::Steel);
    }

    // Department markers are architectural color, not pasted signs. Teal
    // identifies public/diagnostic care; red is reserved for the ED end.
    for (float x : {44.8f, 143.8f}) {
        add("hospital diagnostic vertical identity blade", x, -8.72f,
            0.30f, 0.44f, 13.68f, 1.22f, StartFinish::TealDoor);
    }
    add("hospital emergency vertical identity blade", 150.0f, -8.82f,
        0.30f, 0.52f, 13.68f, 1.42f, StartFinish::RedTrim);
    add("hospital emergency roofline accent", 174.0f, -8.54f, 13.72f,
        59.0f, 0.48f, 0.86f, StartFinish::RedTrim);

    // A compact red wayfinding panel with three bright locator bars gives the
    // ED a street-level landmark without borrowing a protected emblem. Its
    // sign field fits between the entry head and first-floor glazing.
    add("hospital emergency walk-in sign field", 180.0f, -8.48f, 3.94f,
        6.20f, 0.40f, 0.12f, StartFinish::RedTrim);
    for (float x : {179.0f, 180.0f, 181.0f}) {
        add("hospital emergency walk-in sign locator bar", x, -8.57f,
            3.98f, 0.18f, 0.28f, 0.05f, StartFinish::White);
    }

    // Main lobby. The existing north-lot spine is x=0; both vestibule door
    // lines retain a 3.2 m clear centre opening. The mobility layer owns the
    // single porte cochere, so this layer adds only the civic fins and glazed
    // vestibule instead of stacking a second roof into the same volume.
    for (float x : {-10.1f, 10.1f}) {
        add("hospital main entry civic fin", x, -9.15f, 0.22f, 0.46f,
            13.80f, 2.30f, StartFinish::TealDoor, true);
    }
    add("hospital main entry vestibule floor", 0.0f, -10.75f, 0.10f,
        10.8f, 0.08f, 5.45f, StartFinish::Concrete);
    for (float x : {-5.32f, 5.32f}) {
        add("hospital main entry vestibule side glass", x, -10.75f, 0.22f,
            0.08f, 4.46f, 5.30f, StartFinish::Glass);
        add("hospital main entry vestibule side mullion", x, -10.75f,
            0.22f, 0.16f, 4.46f, 0.16f, StartFinish::Steel);
    }
    for (float door_z : {-13.38f, -8.16f}) {
        for (float x : {-2.82f, 2.82f}) {
            add("hospital main entry sliding door glass", x, door_z, 0.22f,
                2.40f, 3.30f, 0.06f, StartFinish::Glass);
            add("hospital main entry sliding door rail", x, door_z - 0.04f,
                3.48f, 2.58f, 0.16f, 0.11f, StartFinish::Steel);
        }
    }

    // The original generated mural is fitted to one 9:6 receiver beside the
    // lobby. It is intentionally not reused on another bay or prop.
    add("hospital main entry mural backing", 20.0f, -8.25f, 4.66f, 9.40f,
        6.40f, 0.14f, StartFinish::Steel, true);
    add("hospital main entry mural face", 20.0f, -8.36f, 4.86f, 9.00f,
        6.00f, 0.05f, StartFinish::WarmWall);

    // The fitted healing-art glass becomes a single west-return landmark.
    // Its local-west face remains a 6 x 4 m composition.
    add("hospital northwest healing art panel backing", -15.18f, 17.0f,
        4.72f, 0.14f, 4.40f, 6.40f, StartFinish::Steel, true);
    add("hospital northwest healing art glass face", -15.29f, 17.0f,
        4.92f, 0.05f, 4.00f, 6.00f, StartFinish::Glass);

    // Twelve real-light anchors span the public frontage. The lobby pair is
    // pushed outward so no fixture hangs over the clear x=0 entry opening.
    for (float x : {-10.0f, 10.0f, 31.0f, 53.0f, 75.0f, 97.0f,
                    119.0f, 139.0f, 153.0f, 169.0f, 185.0f, 200.0f}) {
        add_wall_light(x);
    }

    // Roof plant screens are grouped and set back from the facade. Slender
    // louvers produce a maintained roofline without reading as a fifth floor.
    for (float centre_x : {24.0f, 78.0f, 126.0f, 181.0f}) {
        add("hospital north roof plant screen sill", centre_x, 0.2f, 14.10f,
            24.0f, 0.20f, 0.42f, StartFinish::Steel, true);
        for (float offset : {-10.0f, -6.0f, -2.0f, 2.0f, 6.0f, 10.0f}) {
            add("hospital north roof vertical louver", centre_x + offset,
                0.2f, 14.28f, 0.28f, 2.18f, 0.66f,
                StartFinish::TealDoor, true, -8.0f);
        }
        add("hospital north roof plant screen cap", centre_x, 0.2f, 16.43f,
            24.0f, 0.18f, 0.52f, StartFinish::Steel, true);
    }

    // Small, paired roof screens continue the north-bar plant language over
    // the inpatient and support wings. They stay within their own roof plates
    // and top out at the same 16.61 m silhouette as the north screens; the
    // east roof stays open for the helipad.
    for (const Vec2 centre : {Vec2{14.0f, 56.0f}, Vec2{14.0f, 111.0f}}) {
        add("hospital inpatient roof plant screen sill", centre.x, centre.z,
            14.10f, 18.0f, 0.20f, 0.42f, StartFinish::Steel, true, 90.0f);
        for (float offset : {-7.2f, -3.6f, 0.0f, 3.6f, 7.2f}) {
            add("hospital inpatient roof vertical louver", centre.x,
                centre.z + offset, 14.28f, 0.28f, 2.18f, 0.66f,
                StartFinish::TealDoor, true, 90.0f);
        }
        add("hospital inpatient roof plant screen cap", centre.x, centre.z,
            16.43f, 18.0f, 0.18f, 0.52f, StartFinish::Steel, true, 90.0f);
    }
    for (const float centre_x : {70.0f, 120.0f}) {
        add("hospital support roof plant screen sill", centre_x, 119.0f,
            14.10f, 22.0f, 0.20f, 0.42f, StartFinish::Steel, true);
        for (float offset : {-9.0f, -5.4f, -1.8f, 1.8f, 5.4f, 9.0f}) {
            add("hospital support roof vertical louver", centre_x + offset,
                119.0f, 14.28f, 0.28f, 2.18f, 0.66f,
                StartFinish::TealDoor, true);
        }
        add("hospital support roof plant screen cap", centre_x, 119.0f,
            16.43f, 22.0f, 0.18f, 0.52f, StartFinish::Steel, true);
    }

    // Court-facing shades and mullions continue the exterior hierarchy into
    // both healing courts. All pieces stay above walking height or hug shell
    // faces; none creates a second structural wall around a court.
    for (float z : {34.22f, 59.78f, 78.22f, 99.78f}) {
        for (float x : {60.0f, 84.0f, 108.0f, 132.0f}) {
            add("hospital courtyard facade vertical mullion", x, z, 0.82f,
                0.18f, 12.20f, 0.18f, StartFinish::Steel);
        }
        for (float level : {3.02f, 6.42f, 9.82f}) {
            add("hospital courtyard facade sunshade west", 69.0f, z,
                level, 43.0f, 0.14f, 0.92f, StartFinish::Steel);
            add("hospital courtyard facade sunshade east", 120.0f, z,
                level, 43.0f, 0.14f, 0.92f, StartFinish::Steel);
        }
    }
    for (float z : {41.0f, 52.0f, 84.0f, 94.0f}) {
        add("hospital courtyard west identity fin", 45.22f, z, 0.42f,
            0.82f, 13.20f, 0.34f, StartFinish::WarmWall);
        add("hospital courtyard east identity fin", 143.78f, z, 0.42f,
            0.82f, 13.20f, 0.34f, StartFinish::TealDoor);
    }

    // ---------------------------------------------------------------------
    // North healing court: 99 x 26 m, bounded by x[45,144], z[34,60]. A
    // continuous perimeter loop is paired with a four-leg meandering centre
    // path. Beds alternate sides instead of repeating a four-quadrant stamp.
    // ---------------------------------------------------------------------
    add("hospital north court perimeter walk north", 94.5f, 37.0f, 0.105f,
        91.0f, 0.075f, 3.2f, StartFinish::Concrete);
    add("hospital north court perimeter walk south", 94.5f, 57.0f, 0.105f,
        91.0f, 0.075f, 3.2f, StartFinish::Concrete);
    add("hospital north court perimeter walk west", 48.0f, 47.0f, 0.105f,
        3.2f, 0.075f, 16.8f, StartFinish::Concrete);
    add("hospital north court perimeter walk east", 141.0f, 47.0f, 0.105f,
        3.2f, 0.075f, 16.8f, StartFinish::Concrete);
    add_walk_segment("hospital north court meander west", 58.0f, 45.0f,
                     18.5f, 3.2f, -12.5f);
    add_walk_segment("hospital north court meander centre west", 79.0f,
                     46.5f, 25.5f, 3.2f, 16.0f);
    add_walk_segment("hospital north court meander centre east", 103.5f,
                     46.5f, 26.0f, 3.2f, -15.5f);
    add_walk_segment("hospital north court meander east", 128.5f, 45.0f,
                     25.5f, 3.2f, 9.0f);

    add_low_bed(56.0f, 52.7f, 10.0f, 3.2f, 8.0f, true);
    add_low_bed(78.0f, 40.8f, 14.0f, 3.0f, -3.0f, false);
    add_low_bed(101.0f, 53.0f, 16.0f, 3.0f, 4.0f, true);
    add_low_bed(132.0f, 40.0f, 8.0f, 2.0f, -4.0f, false);
    add_tree(56.0f, 52.7f, 0.86f, 17.0f);
    add_tree(101.0f, 53.0f, 0.94f, -24.0f);
    add_bench(70.0f, 53.0f, 0.0f);
    add_bench(112.0f, 40.6f, 180.0f);
    add_bench(132.0f, 52.2f, 0.0f);

    // One quiet horticultural-therapy table and one water/mosaic focus give
    // this court programmed use. Both sit off the meandering travel line.
    add("hospital north court therapy table pedestal", 87.0f, 53.0f, 0.16f,
        0.72f, 0.72f, 0.72f, StartFinish::Steel, true);
    add("hospital north court therapy table top", 87.0f, 53.0f, 0.88f,
        3.20f, 0.18f, 1.25f, StartFinish::WarmWall, true);
    add("hospital north court therapy planting tray", 87.0f, 53.0f, 1.06f,
        2.75f, 0.14f, 0.92f, StartFinish::DarkRoof);
    add("hospital grounds healing mosaic backing", 122.0f, 34.34f, 0.22f,
        4.10f, 2.80f, 0.20f, StartFinish::Steel, true);
    add("hospital grounds healing mosaic face", 122.0f, 34.20f, 0.42f,
        3.60f, 2.40f, 0.05f, StartFinish::WarmWall);
    add("hospital north court water rill basin", 122.0f, 40.25f, 0.14f,
        5.2f, 0.24f, 1.45f, StartFinish::Concrete, true);
    add("hospital north court water rill", 122.0f, 40.25f, 0.385f,
        4.70f, 0.025f, 1.05f, StartFinish::PoolWater);

    for (const Vec2 light : {Vec2{52.0f, 40.0f}, Vec2{52.0f, 54.0f},
                             Vec2{74.0f, 54.0f}, Vec2{92.0f, 40.0f},
                             Vec2{113.0f, 54.0f}, Vec2{137.0f, 40.0f},
                             Vec2{137.0f, 54.0f}}) {
        add_path_light(light.x, light.z);
    }
    add("hospital north court slot drain west", 49.9f, 58.45f, 0.205f,
        3.8f, 0.025f, 0.18f, StartFinish::Steel);
    add("hospital north court slot drain centre", 94.5f, 58.45f, 0.205f,
        56.0f, 0.025f, 0.18f, StartFinish::Steel);
    add("hospital north court slot drain east", 139.1f, 58.45f, 0.205f,
        3.8f, 0.025f, 0.18f, StartFinish::Steel);

    // ---------------------------------------------------------------------
    // South healing/rehab court: slightly tighter at 99 x 22 m. Its zig-zag
    // is deliberately different from the north meander. Therapy fixtures are
    // gathered into side bays, leaving the accessible through-route open.
    // ---------------------------------------------------------------------
    add("hospital south court perimeter walk north", 94.5f, 81.1f, 0.105f,
        91.0f, 0.075f, 2.9f, StartFinish::Concrete);
    add("hospital south court perimeter walk south", 94.5f, 96.9f, 0.105f,
        91.0f, 0.075f, 2.9f, StartFinish::Concrete);
    add("hospital south court perimeter walk west", 48.0f, 89.0f, 0.105f,
        3.2f, 0.075f, 13.0f, StartFinish::Concrete);
    add("hospital south court perimeter walk east", 141.0f, 89.0f, 0.105f,
        3.2f, 0.075f, 13.0f, StartFinish::Concrete);
    add_walk_segment("hospital south court rehab zig west", 58.5f, 87.0f,
                     20.0f, 3.0f, -11.0f);
    add_walk_segment("hospital south court rehab zig centre west", 80.0f,
                     89.0f, 25.0f, 3.0f, 17.0f);
    add_walk_segment("hospital south court rehab zig centre east", 104.5f,
                     89.0f, 26.0f, 3.0f, -18.0f);
    add_walk_segment("hospital south court rehab zig east", 130.0f, 87.0f,
                     24.0f, 3.0f, 10.0f);

    add_low_bed(52.0f, 91.5f, 3.0f, 2.0f, 0.0f, false);
    add_low_bed(78.0f, 84.4f, 13.0f, 2.5f, 3.0f, true);
    add_low_bed(107.0f, 94.1f, 10.0f, 1.8f, -2.0f, false);
    add_low_bed(132.0f, 93.8f, 8.0f, 1.8f, 0.0f, true);
    add_tree(78.0f, 84.4f, 0.82f, 28.0f);
    add_tree(132.0f, 93.8f, 0.88f, -14.0f);
    add_bench(72.0f, 94.0f, 0.0f);
    add_bench(120.0f, 94.0f, 0.0f);

    // Parallel bars and flush cadence pads make the rehab identity legible.
    // The bars occupy the southwest side bay, not the central zig-zag route.
    add("hospital south court parallel bar floor", 62.0f, 92.7f, 0.18f,
        11.0f, 0.025f, 2.8f, StartFinish::DarkRoof);
    for (float z : {91.7f, 93.7f}) {
        for (float x : {57.4f, 62.0f, 66.6f}) {
            add("hospital south court parallel bar post", x, z, 0.18f,
                0.12f, 1.05f, 0.12f, StartFinish::Steel, true);
        }
        add("hospital south court parallel bar rail", 62.0f, z, 1.16f,
            9.4f, 0.14f, 0.14f, StartFinish::Steel, true);
    }
    for (int pad = 0; pad < 6; ++pad) {
        add("hospital south court therapy cadence pad",
            88.0f + static_cast<float>(pad) * 2.1f, 94.0f, 0.205f,
            1.10f, 0.025f, 0.72f, StartFinish::Yellow);
    }
    add("hospital south court therapy rest rail post", 86.0f, 84.2f,
        0.16f, 0.14f, 1.10f, 0.14f, StartFinish::Steel, true);
    add("hospital south court therapy rest rail post", 90.0f, 84.2f,
        0.16f, 0.14f, 1.10f, 0.14f, StartFinish::Steel, true);
    add("hospital south court therapy rest rail", 88.0f, 84.2f, 1.18f,
        4.2f, 0.14f, 0.14f, StartFinish::Steel, true);

    for (const Vec2 light : {Vec2{52.0f, 83.5f}, Vec2{52.0f, 94.4f},
                             Vec2{84.0f, 94.4f}, Vec2{102.0f, 83.5f},
                             Vec2{123.0f, 94.4f}, Vec2{137.0f, 83.5f},
                             Vec2{137.0f, 94.4f}}) {
        add_path_light(light.x, light.z);
    }
    add("hospital south court slot drain west", 49.9f, 98.55f, 0.205f,
        3.8f, 0.025f, 0.18f, StartFinish::Steel);
    add("hospital south court slot drain centre", 94.5f, 98.55f, 0.205f,
        56.0f, 0.025f, 0.18f, StartFinish::Steel);
    add("hospital south court slot drain east", 139.1f, 98.55f, 0.205f,
        3.8f, 0.025f, 0.18f, StartFinish::Steel);

    // Foundation planting stops well before the ED frontage at x=144. These
    // low beds soften the public bar while leaving the x=0 forecourt and all
    // ambulance sight lines completely open.
    add_low_bed(58.0f, -10.15f, 18.0f, 2.4f, 0.0f, false);
    add_low_bed(89.0f, -10.15f, 17.0f, 2.4f, 0.0f, false);
    add_low_bed(120.0f, -10.15f, 16.0f, 2.4f, 0.0f, false);
    for (const Vec2 bed : {Vec2{-18.7f, 48.0f}, Vec2{-18.7f, 70.0f},
                           Vec2{-18.7f, 92.0f}}) {
        add_low_bed(bed.x, bed.z, 3.0f, 12.0f, 0.0f, false);
    }

    // Staff respite uses the quiet southwest gap between the inpatient bar
    // and garage. It stays west of the x=46 garage walking axis and far from
    // the southeast service court and Juniper ambulance visibility envelope.
    add("hospital staff respite patio", 13.0f, 151.0f, 0.105f, 38.0f,
        0.075f, 12.0f, StartFinish::Concrete);
    add_low_bed(-1.5f, 151.0f, 6.0f, 9.0f, -3.0f, true);
    add_low_bed(27.5f, 151.0f, 6.0f, 9.0f, 4.0f, true);
    add_tree(-1.5f, 151.0f, 0.92f, 18.0f);
    add_tree(27.5f, 151.0f, 0.88f, -27.0f);
    add("hospital staff respite shade roof", 13.0f, 151.0f, 3.05f, 13.0f,
        0.24f, 6.0f, StartFinish::Steel, true);
    for (float x : {7.2f, 18.8f}) {
        for (float z : {148.5f, 153.5f}) {
            add("hospital staff respite shade column", x, z, 0.16f, 0.22f,
                2.89f, 0.22f, StartFinish::Steel, true);
        }
    }
    add_bench(9.5f, 151.0f, 90.0f);
    add_bench(16.5f, 151.0f, 90.0f);
    add_path_light(4.0f, 145.8f);
    add_path_light(22.0f, 145.8f);
    add_path_light(4.0f, 156.2f);
    add_path_light(22.0f, 156.2f);
    add("hospital staff respite slot drain", 13.0f, 157.05f, 0.205f,
        34.0f, 0.025f, 0.18f, StartFinish::Steel);

    return out;
}

}  // namespace apricot::city
