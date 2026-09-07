#pragma once

#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Exterior landscape sidecar for Vellum Regional Hospital. Coordinates are
// local to kHospitalSite: +x follows grid east and +z follows grid south.
// Four 38 x 25 m open-air courts sit between the occupied pavilion links.
inline std::vector<StartPart> bake_hospital_exterior_grounds() {
    std::vector<StartPart> out;
    out.reserve(180);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };

    const auto add_planter = [&](float x, float z, float yaw_deg) {
        // The low planter volume is its collider. Soil and foliage remain
        // visual-only so the collision never grows beyond what is visible.
        add("hospital grounds courtyard planter", x, z, 0.16f, 8.5f, 0.30f,
            4.5f, StartFinish::Concrete, true, yaw_deg);
        add("hospital grounds planter soil", x, z, 0.47f, 7.9f, 0.05f,
            3.9f, StartFinish::DarkRoof, false, yaw_deg);
        add("hospital grounds shrub cluster", x - 1.8f, z, 0.52f, 2.2f,
            0.72f, 1.25f, StartFinish::TealDoor, false, yaw_deg - 18.0f);
        add("hospital grounds shrub cluster", x + 1.8f, z, 0.52f, 2.0f,
            0.86f, 1.35f, StartFinish::TealDoor, false, yaw_deg + 22.0f);
    };

    const auto add_tree = [&](float x, float z, float scale,
                              float crown_yaw_deg) {
        add("hospital grounds tree trunk", x, z, 0.50f, 0.42f * scale,
            3.25f * scale, 0.42f * scale, StartFinish::Brick, true);
        // Two crossed, non-solid crown masses create a readable low-poly
        // silhouette without turning foliage into an invisible obstacle.
        add("hospital grounds tree canopy", x, z, 2.92f * scale,
            3.65f * scale, 1.75f * scale, 1.45f * scale,
            StartFinish::TealDoor, false, crown_yaw_deg);
        add("hospital grounds tree canopy", x, z, 3.10f * scale,
            3.25f * scale, 1.45f * scale, 1.35f * scale,
            StartFinish::TealDoor, false, crown_yaw_deg + 90.0f);
    };

    const auto add_bench = [&](float x, float z, float yaw_deg) {
        const bool turned = yaw_deg > 45.0f && yaw_deg < 135.0f;
        const float back_x = x + (turned ? -0.27f : 0.0f);
        const float back_z = z + (turned ? 0.0f : 0.27f);
        add("hospital grounds bench plinth", x, z, 0.16f, 2.90f, 0.24f,
            0.72f, StartFinish::Concrete, true, yaw_deg);
        add("hospital grounds bench seat", x, z, 0.40f, 2.65f, 0.16f,
            0.58f, StartFinish::WarmWall, true, yaw_deg);
        add("hospital grounds bench back", back_x, back_z, 0.52f, 2.65f,
            0.78f, 0.12f, StartFinish::WarmWall, true, yaw_deg);
    };

    const auto add_bin = [&](float x, float z) {
        add("hospital grounds litter bin body", x, z, 0.16f, 0.58f, 0.90f,
            0.58f, StartFinish::Steel, true);
        add("hospital grounds litter bin lid", x, z, 1.06f, 0.66f, 0.10f,
            0.66f, StartFinish::DarkRoof);
    };

    const auto add_path_light = [&](float x, float z) {
        add("hospital grounds path light base", x, z, 0.16f, 0.52f, 0.18f,
            0.52f, StartFinish::Concrete, true);
        add("hospital grounds path light pole", x, z, 0.34f, 0.16f, 3.08f,
            0.16f, StartFinish::Steel, true);
        // Parent routes this exact receiver name to emissive geometry and a
        // real runtime light. The lens itself must never block a pedestrian.
        add("hospital grounds path light lens", x, z, 3.42f, 0.54f, 0.22f,
            0.54f, StartFinish::White);
    };

    const auto add_bike_rack = [&](float x, float z) {
        // One simple inverted-U stand: two visible posts and a connecting
        // rail. Three stands make real bike parking without a giant collider.
        add("hospital grounds bike rack post", x, z - 0.58f, 0.16f, 0.12f,
            0.84f, 0.12f, StartFinish::Steel, true);
        add("hospital grounds bike rack post", x, z + 0.58f, 0.16f, 0.12f,
            0.84f, 0.12f, StartFinish::Steel, true);
        add("hospital grounds bike rack top rail", x, z, 0.94f, 0.12f,
            0.12f, 1.28f, StartFinish::Steel, true);
    };

    // Supported perimeter walks stay in the narrow gap between pavilion
    // walls and Bellweather/Juniper. They stop short of every carriageway and
    // do not recreate Rook Lane, Vellum Row, Seventh, Eighth, or Ninth.
    add("hospital grounds west perimeter walk", -31.5f, 26.0f, 0.11f, 3.2f,
        0.08f, 88.0f, StartFinish::Concrete);
    add("hospital grounds west perimeter walk", -31.5f, 106.0f, 0.11f,
        3.2f, 0.08f, 70.0f, StartFinish::Concrete);
    add("hospital grounds east perimeter walk", 215.5f, 26.0f, 0.11f,
        3.2f, 0.08f, 88.0f, StartFinish::Concrete);
    add("hospital grounds east perimeter walk", 215.5f, 106.0f, 0.11f,
        3.2f, 0.08f, 70.0f, StartFinish::Concrete);

    // The former Seventh Street corridor is pedestrian ground between the
    // hospital and garage. The two east-west slabs stop at the skybridge axis;
    // a 3.2 m north-south walk remains clear below the elevated bridge.
    add("hospital grounds south green walk west", 3.5f, 152.5f, 0.11f,
        61.0f, 0.08f, 3.0f, StartFinish::Concrete);
    add("hospital grounds south green walk east", 85.0f, 152.5f, 0.11f,
        68.0f, 0.08f, 3.0f, StartFinish::Concrete);
    add("hospital grounds skybridge ground walk", 46.0f, 155.0f, 0.11f,
        3.2f, 0.08f, 24.0f, StartFinish::Concrete);

    // Three planter islands per court leave the fourth quadrant open for
    // seating. Every court keeps a continuous four-metre cross route.
    constexpr Vec2 court_centres[] = {
        {46.0f, 31.0f}, {138.0f, 31.0f},
        {46.0f, 93.0f}, {138.0f, 93.0f},
    };
    for (int index = 0; index < 4; ++index) {
        const Vec2 c = court_centres[index];
        add("hospital grounds courtyard walk north south", c.x, c.z, 0.11f,
            4.0f, 0.08f, 25.0f, StartFinish::Concrete);
        add("hospital grounds courtyard walk west", c.x - 11.0f, c.z,
            0.11f, 18.0f, 0.08f, 4.0f, StartFinish::Concrete);
        add("hospital grounds courtyard walk east", c.x + 11.0f, c.z,
            0.11f, 18.0f, 0.08f, 4.0f, StartFinish::Concrete);

        add_planter(c.x - 10.0f, c.z - 6.4f, -4.0f);
        add_planter(c.x + 10.0f, c.z - 6.4f, 4.0f);
        add_planter(c.x + 10.0f, c.z + 6.4f, -5.0f);

        add_tree(c.x - 10.0f, c.z - 6.4f, 0.96f,
                 24.0f + index * 7.0f);
        add_tree(c.x + 10.0f, c.z + 6.4f, 0.88f,
                 -18.0f - index * 8.0f);

        if (index == 1) {
            // The healing court moves its benches beside the focal panel,
            // facing the quiet southwest planting room.
            add_bench(c.x - 5.5f, c.z + 3.6f, 90.0f);
            add_bench(c.x - 5.5f, c.z + 8.0f, 90.0f);
        } else {
            add_bench(c.x - 10.0f, c.z + 5.4f, 0.0f);
            add_bench(c.x - 10.0f, c.z + 9.1f, 0.0f);
        }

        add_bin(c.x - 16.0f, c.z + 9.3f);
        add_path_light(c.x - 3.5f, c.z - 8.8f);
        add_path_light(c.x + 3.5f, c.z + 8.8f);
    }

    // Quiet healing-garden focal wall in the northeast court. The generated
    // 3:2 mosaic maps exactly once to the 3.6 x 2.4 m face; all other surfaces
    // use ordinary project materials.
    add("hospital grounds healing mosaic backing", 128.0f, 40.65f, 0.22f,
        4.10f, 2.80f, 0.22f, StartFinish::Steel, true);
    add("hospital grounds healing mosaic face", 128.0f, 40.50f, 0.44f,
        3.60f, 2.40f, 0.05f, StartFinish::WarmWall);
    add("hospital grounds healing water rill basin", 128.0f, 37.80f, 0.16f,
        4.00f, 0.24f, 1.40f, StartFinish::Concrete, true);
    add("hospital grounds healing water rill", 128.0f, 37.80f, 0.41f,
        3.60f, 0.025f, 1.00f, StartFinish::PoolWater);

    // Two shallow rain gardens break up the reclaimed street surface without
    // pinching the bridge walk, east-west walk, garage throat, or sight lines.
    for (const Vec2 bed : {Vec2{18.0f, 159.0f}, Vec2{83.0f, 159.0f}}) {
        const float width = bed.x < 40.0f ? 30.0f : 52.0f;
        add("hospital grounds south rain garden curb", bed.x, bed.z, 0.16f,
            width, 0.24f, 7.5f, StartFinish::Concrete, true);
        add("hospital grounds south rain garden soil", bed.x, bed.z, 0.41f,
            width - 0.7f, 0.05f, 6.8f, StartFinish::DarkRoof);
        add("hospital grounds rain garden rushes", bed.x - width * 0.24f,
            bed.z, 0.46f, width * 0.30f, 0.70f, 1.15f,
            StartFinish::TealDoor, false, -8.0f);
        add("hospital grounds rain garden rushes", bed.x + width * 0.24f,
            bed.z, 0.46f, width * 0.30f, 0.82f, 1.15f,
            StartFinish::TealDoor, false, 8.0f);
    }
    add_path_light(34.0f, 159.0f);
    add_path_light(56.0f, 159.0f);

    // Three inverted-U stands form bike parking north of the garage. They sit
    // west of the rain beds and clear of the Bellweather entry lane.
    add_bike_rack(-19.0f, 159.0f);
    add_bike_rack(-16.0f, 159.0f);
    add_bike_rack(-13.0f, 159.0f);

    // Flush visual slot drains sit at the low edge of each court and south
    // green. They are deliberately non-solid trip-free receiver planes.
    add("hospital grounds slot drain", 46.0f, 42.7f, 0.205f, 16.0f, 0.025f,
        0.18f, StartFinish::Steel);
    add("hospital grounds slot drain", 138.0f, 42.7f, 0.205f, 16.0f, 0.025f,
        0.18f, StartFinish::Steel);
    add("hospital grounds slot drain", 46.0f, 104.7f, 0.205f, 16.0f,
        0.025f, 0.18f, StartFinish::Steel);
    add("hospital grounds slot drain", 138.0f, 104.7f, 0.205f, 16.0f,
        0.025f, 0.18f, StartFinish::Steel);

    return out;
}

}  // namespace apricot::city
