#pragma once

#include <array>
#include <vector>

#include "city/start_area.h"

namespace apricot::city {

// Exterior-detail sidecar for the 146 x 38 m Vellum Regional Hospital garage.
// Coordinates are local to kHospitalSite. The parent owns the main decks and
// road access; this bake only adds purposeful structure and set dressing.
inline std::vector<StartPart> bake_hospital_exterior_garage() {
    std::vector<StartPart> out;
    out.reserve(150);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float pitch = 0.0f, float yaw = 0.0f,
                         float roll = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.pitch_deg = pitch;
        part.yaw_deg = yaw;
        part.roll_deg = roll;
        out.push_back(part);
    };

    constexpr float kGarageMinX = -27.0f;
    constexpr float kGarageMaxX = 119.0f;
    constexpr float kGarageNorthZ = 167.5f;
    constexpr float kGarageSouthZ = 204.5f;
    constexpr std::array<float, 3> kFloorBases{{0.30f, 4.08f, 7.78f}};

    // Open P2 facade screens add depth without becoming fake collision walls.
    // The north rails break around the bridge core; the south rails break
    // around the Sixth Street exit throat.
    add("hospital garage open facade rail", 6.25f, 167.32f, 5.26f,
        66.5f, 0.13f, 0.12f, StartFinish::Steel);
    add("hospital garage open facade rail", 85.75f, 167.32f, 5.26f,
        66.5f, 0.13f, 0.12f, StartFinish::Steel);
    for (float x : {-2.0f, 76.0f, 108.0f}) {
        add("hospital garage open facade mullion", x, 167.32f, 4.34f,
            0.14f, 2.82f, 0.14f, StartFinish::Steel);
    }
    add("hospital garage open facade rail", 31.75f, 204.68f, 5.26f,
        117.5f, 0.13f, 0.12f, StartFinish::Steel);
    add("hospital garage open facade rail", 108.25f, 204.68f, 5.26f,
        21.5f, 0.13f, 0.12f, StartFinish::Steel);
    for (float x : {4.0f, 58.0f, 110.0f}) {
        add("hospital garage open facade mullion", x, 204.68f, 4.34f,
            0.14f, 2.82f, 0.14f, StartFinish::Steel);
    }

    // Collision-bearing parapets follow every exposed P2/P3 edge. The north
    // edge stays split at the bridge landing; the grade-level driveways remain
    // entirely open.
    for (float base : {4.08f, 7.78f}) {
        add("hospital garage rhythmic parapet", 6.25f, kGarageNorthZ,
            base, 66.5f, 0.90f, 0.32f, StartFinish::Concrete, true);
        add("hospital garage rhythmic parapet", 85.75f, kGarageNorthZ,
            base, 66.5f, 0.90f, 0.32f, StartFinish::Concrete, true);
        add("hospital garage rhythmic parapet", 46.0f, kGarageSouthZ,
            base, 146.0f, 0.90f, 0.32f, StartFinish::Concrete, true);
        add("hospital garage rhythmic parapet", kGarageMinX, 186.0f,
            base, 0.32f, 0.90f, 37.0f, StartFinish::Concrete, true);
        add("hospital garage rhythmic parapet", kGarageMaxX, 186.0f,
            base, 0.32f, 0.90f, 37.0f, StartFinish::Concrete, true);
    }

    // Main lift core. Every south floor and the P1/P2 north faces use actual
    // 4.0 m wall gaps, not door stickers. P2's north gap meets the bridge.
    add("hospital garage main lift core side wall", 40.0f, 174.6f, 0.30f,
        0.32f, 10.70f, 14.0f, StartFinish::Concrete, true);
    add("hospital garage main lift core side wall", 52.0f, 174.6f, 0.30f,
        0.32f, 10.70f, 14.0f, StartFinish::Concrete, true);
    for (float base : kFloorBases) {
        for (float x : {42.0f, 50.0f}) {
            add("hospital garage main lift core south door pier", x,
                181.6f, base, 4.00f, 3.18f, 0.32f,
                StartFinish::Concrete, true);
        }
        add("hospital garage main lift core south door lintel", 46.0f,
            181.6f, base + 2.45f, 4.00f, 0.73f, 0.32f,
            StartFinish::Concrete, true);
    }
    for (float base : {kFloorBases[0], kFloorBases[1]}) {
        for (float x : {42.0f, 50.0f}) {
            add("hospital garage main lift core north door pier", x,
                167.6f, base, 4.00f, 3.18f, 0.32f,
                StartFinish::Concrete, true);
        }
        add("hospital garage main lift core north door lintel", 46.0f,
            167.6f, base + 2.45f, 4.00f, 0.73f, 0.32f,
            StartFinish::Concrete, true);
    }
    add("hospital garage main lift core north upper wall", 46.0f, 167.6f,
        kFloorBases[2], 12.0f, 3.18f, 0.32f,
        StartFinish::Concrete, true);

    // West public stair core. Its alternating real stair flights meet wall
    // openings on opposite sides at P1, P2 and P3.
    add("hospital garage public stair core side wall", -18.5f, 173.8f,
        0.30f, 0.30f, 10.70f, 11.6f, StartFinish::Concrete, true);
    add("hospital garage public stair core side wall", -9.5f, 173.8f,
        0.30f, 0.30f, 10.70f, 11.6f, StartFinish::Concrete, true);

    const auto add_stair_door = [&](const char* pier_name,
                                    const char* lintel_name, float z,
                                    float base) {
        for (float x : {-17.05f, -10.95f}) {
            add(pier_name, x, z, base, 2.90f, 3.18f, 0.30f,
                StartFinish::Concrete, true);
        }
        add(lintel_name, -14.0f, z, base + 2.45f, 3.20f, 0.73f, 0.30f,
            StartFinish::Concrete, true);
    };
    add("hospital garage public stair core south wall", -14.0f, 179.6f,
        kFloorBases[0], 9.0f, 3.18f, 0.30f,
        StartFinish::Concrete, true);
    add_stair_door("hospital garage public stair core south door pier",
                   "hospital garage public stair core south door lintel",
                   179.6f, kFloorBases[1]);
    add("hospital garage public stair core south wall", -14.0f, 179.6f,
        kFloorBases[2], 9.0f, 3.18f, 0.30f,
        StartFinish::Concrete, true);
    add_stair_door("hospital garage public stair core north door pier",
                   "hospital garage public stair core north door lintel",
                   168.0f, kFloorBases[0]);
    add("hospital garage public stair core north wall", -14.0f, 168.0f,
        kFloorBases[1], 9.0f, 3.18f, 0.30f,
        StartFinish::Concrete, true);
    add_stair_door("hospital garage public stair core north door pier",
                   "hospital garage public stair core north door lintel",
                   168.0f, kFloorBases[2]);

    constexpr int kStairSteps = 22;
    constexpr float kStepDepth = 0.50f;
    constexpr float kStoreyRise = 3.70f;
    for (int step = 0; step < kStairSteps; ++step) {
        const float rise = static_cast<float>(step + 1) *
                           kStoreyRise / static_cast<float>(kStairSteps);
        add("hospital garage public stair tread", -14.0f,
            168.25f + static_cast<float>(step) * kStepDepth,
            kFloorBases[0], 3.20f, rise, kStepDepth,
            StartFinish::Concrete, true);
        add("hospital garage public stair tread", -14.0f,
            178.75f - static_cast<float>(step) * kStepDepth,
            kFloorBases[1], 3.20f, rise, kStepDepth,
            StartFinish::Concrete, true);
    }

    // Gate furniture hugs the driver's side of each lane. Both arms are
    // visibly raised and non-solid, so the authored route remains usable.
    add("hospital garage payment island", -14.0f, 195.3f, 0.30f,
        12.0f, 0.14f, 1.40f, StartFinish::Concrete, true);
    add("hospital garage west entry control body", -15.0f, 195.3f, 0.44f,
        0.74f, 1.35f, 0.74f, StartFinish::TealDoor, true);
    add("hospital garage west entry control face", -15.39f, 195.3f, 0.67f,
        0.04f, 0.90f, 0.60f, StartFinish::White);
    for (float x : {-16.5f, -13.5f}) {
        add("hospital garage payment island bollard", x, 195.3f, 0.44f,
            0.18f, 0.95f, 0.18f, StartFinish::Yellow, true);
    }
    add("hospital garage west entry gate pedestal", -10.0f, 195.3f,
        0.44f, 0.38f, 1.05f, 0.38f, StartFinish::Steel, true);
    add("hospital garage west entry raised gate arm", -10.0f, 197.2f,
        1.25f, 0.15f, 0.12f, 3.80f, StartFinish::RedTrim, false,
        -68.0f);
    add("hospital garage south exit gate pedestal", 91.6f, 199.8f,
        0.30f, 0.38f, 1.05f, 0.38f, StartFinish::Steel, true);
    add("hospital garage south exit raised gate arm", 93.55f, 199.8f,
        1.25f, 3.90f, 0.12f, 0.15f, StartFinish::RedTrim, false,
        0.0f, 0.0f, -68.0f);
    for (float z : {195.2f, 200.8f}) {
        add("hospital garage west entry clearance post", -22.0f, z,
            0.30f, 0.22f, 2.60f, 0.22f, StartFinish::Steel, true);
    }
    add("hospital garage west entry clearance bar", -22.0f, 198.0f,
        2.75f, 0.22f, 0.16f, 5.60f, StartFinish::Yellow, true);
    for (float x : {90.8f, 97.2f}) {
        add("hospital garage south exit clearance post", x, 201.6f,
            0.30f, 0.22f, 2.60f, 0.22f, StartFinish::Steel, true);
    }
    add("hospital garage south exit clearance bar", 94.0f, 201.6f,
        2.75f, 6.40f, 0.16f, 0.22f, StartFinish::Yellow, true);

    // Simple one/two/three-bar level markers stay readable at PSX distance
    // without repeating a raster sign across all three floors.
    for (std::size_t level = 0; level < kFloorBases.size(); ++level) {
        const float base = kFloorBases[level];
        add("hospital garage level marker board", 50.25f, 181.79f,
            base + 1.02f, 1.45f, 1.35f, 0.06f,
            StartFinish::TealDoor);
        for (std::size_t bar = 0; bar <= level; ++bar) {
            add("hospital garage level marker stripe", 50.25f, 181.73f,
                base + 1.35f + static_cast<float>(bar) * 0.24f,
                0.88f, 0.11f, 0.025f, StartFinish::White);
        }
    }

    // These meshes are visible fixture lenses only. The parent routes this
    // exact name to real runtime lights; bright geometry alone is not relied
    // upon to light the garage.
    constexpr std::array<Vec2, 3> kCeilingFixtures{{
        {-18.0f, 187.0f}, {72.0f, 177.0f}, {108.0f, 198.0f}}};
    for (float base : {kFloorBases[0], kFloorBases[1]}) {
        for (Vec2 position : kCeilingFixtures) {
            add("hospital garage ceiling light housing", position.x,
                position.z, base + 3.27f, 1.80f, 0.12f, 0.36f,
                StartFinish::Steel);
            add("hospital garage ceiling light lens", position.x,
                position.z, base + 3.25f, 1.50f, 0.025f, 0.24f,
                StartFinish::White);
        }
    }

    // Flush drainage has no raised collision. The east strip keeps water and
    // planting out of the Vellum Row sidewalk; the exit trench catches the
    // southward garage fall before Sixth Street.
    add("hospital garage east slot drain", 121.0f, 186.0f, 0.305f,
        0.22f, 0.025f, 27.0f, StartFinish::Steel);
    add("hospital garage south exit trench drain", 94.0f, 203.2f, 0.305f,
        8.0f, 0.025f, 0.20f, StartFinish::Steel);

    // Two low rain-garden beds reclaim the old Seventh Street corridor while
    // leaving the x=46 bridge walk and both vehicle sight triangles open.
    for (const std::array<float, 2> bed :
         {std::array<float, 2>{9.0f, 18.0f},
          std::array<float, 2>{91.0f, 17.0f}}) {
        add("hospital garage rain garden soil", bed[0], 153.0f, 0.12f,
            bed[1], 0.08f, 4.0f, StartFinish::DarkRoof);
        for (float z : {151.0f, 155.0f}) {
            add("hospital garage rain garden curb", bed[0], z, 0.12f,
                bed[1] + 0.30f, 0.24f, 0.22f,
                StartFinish::Concrete, true);
        }
        for (float x_offset : {-0.24f, 0.24f}) {
            add("hospital garage low planting", bed[0] +
                x_offset * bed[1], 153.0f, 0.20f, 2.60f, 0.48f, 1.70f,
                StartFinish::TealDoor);
        }
    }

    // P2 landing and portal frame finish the parent skybridge without placing
    // a wall across its 4.0 m clear route.
    add("hospital garage pedestrian bridge landing", 46.0f, 168.4f,
        3.93f, 10.0f, 0.15f, 3.20f, StartFinish::Concrete, true);
    for (float x : {41.15f, 50.85f}) {
        add("hospital garage pedestrian bridge landing guard", x, 168.4f,
            4.08f, 0.18f, 1.05f, 3.20f, StartFinish::Steel, true);
    }
    for (float x : {43.65f, 48.35f}) {
        add("hospital garage pedestrian bridge portal upright", x,
            167.38f, 4.08f, 0.28f, 3.02f, 0.30f,
            StartFinish::TealDoor, true);
    }
    add("hospital garage pedestrian bridge portal lintel", 46.0f,
        167.38f, 6.68f, 5.00f, 0.42f, 0.30f,
        StartFinish::TealDoor, true);

    return out;
}

}  // namespace apricot::city
