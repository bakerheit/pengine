#pragma once

#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Exterior-only polish in hospital-site local coordinates. The campus shell
// remains the source of collision for the occupied wings; this sidecar adds
// facade depth and the few roof objects that need their own visible collision.
inline std::vector<StartPart> bake_hospital_exterior_facade() {
    std::vector<StartPart> out;
    out.reserve(180);
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    const auto block = [](int column, int row) {
        return hospital_block_centre(column, row);
    };

    // Every wing keeps the same four-floor cadence. The spandrels project just
    // beyond the glazing but remain behind the existing north-west mural.
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const Vec2 c = block(column, row);
            for (int floor = 1; floor < kHospitalFloorCount; ++floor) {
                const float band_bottom =
                    floor * kHospitalFloorHeightM - 0.40f;
                add("hospital facade front layered spandrel", c.x,
                    c.z - 17.965f, band_bottom, 49.0f, 0.42f, 0.08f,
                    StartFinish::WarmWall);
                add("hospital facade rear layered spandrel", c.x,
                    c.z + 17.965f, band_bottom, 49.0f, 0.42f, 0.08f,
                    StartFinish::TealDoor);
            }

            // Front mullions respect the broad paired glazing and avoid the
            // existing main-entry mural on the north-west wing.
            for (float offset : {-21.0f, 15.0f}) {
                add("hospital facade front vertical mullion", c.x + offset,
                    c.z - 18.025f, 0.92f, 0.22f, 12.15f, 0.14f,
                    StartFinish::Steel);
            }
            for (float offset : {-15.0f, 15.0f}) {
                add("hospital facade rear vertical mullion", c.x + offset,
                    c.z + 18.025f, 0.92f, 0.22f, 12.15f, 0.14f,
                    StartFinish::Steel);
            }

            // Deep outer reveals sharpen the pavilion silhouette without
            // widening the collision shell into perimeter streets or courts.
            for (float offset : {-26.98f, 26.98f}) {
                add("hospital facade north corner trim", c.x + offset,
                    c.z - 17.98f, 0.28f, 0.34f, 13.85f, 0.42f,
                    StartFinish::TealDoor);
            }

            // The local +Z face is the hotter/southern exposure. One real
            // projecting shade per wing breaks the upper silhouette while
            // staying well above every service and pedestrian route.
            add("hospital facade south upper sunshade", c.x, c.z + 18.45f,
                10.62f, 47.0f, 0.20f, 1.10f, StartFinish::Steel);
        }
    }

    // Visible fixture lenses give the parent exact anchors for real runtime
    // night lights. The shared name below is an intentional routing contract.
    for (int column = 0; column < 3; ++column) {
        const Vec2 c = block(column, 0);
        for (float offset : {-8.0f, 8.0f}) {
            add("hospital facade wall light lens", c.x + offset,
                c.z - 18.16f, 3.25f, 0.38f, 0.18f, 0.10f,
                StartFinish::White);
        }
    }
    for (int row : {1, 2}) {
        const Vec2 c = block(0, row);
        add("hospital facade wall light lens", c.x - 27.14f, c.z,
            3.25f, 0.10f, 0.18f, 0.38f, StartFinish::White);
    }

    // The former east-west streets are readable as glazed links. A teal fin
    // on each long face marks the seam without closing the open courts.
    for (int row = 0; row < 3; ++row) {
        for (int seam = 0; seam < 2; ++seam) {
            const float x = 46.0f + seam * kHospitalBlockStepXM;
            const float z = row * kHospitalBlockStepZM;
            add("hospital east west connector north fin", x, z - 6.28f,
                0.42f, 0.48f, 13.55f, 0.65f,
                StartFinish::TealDoor);
            add("hospital east west connector south fin", x, z + 6.28f,
                0.42f, 0.48f, 13.55f, 0.65f,
                StartFinish::TealDoor);
        }
    }

    // The north-south links receive the same seam marker on their exposed
    // east and west faces. These are visual fins, not oversized colliders.
    for (int row_seam = 0; row_seam < 2; ++row_seam) {
        for (int column = 0; column < 3; ++column) {
            const float x = column * kHospitalBlockStepXM;
            const float z = 31.0f + row_seam * kHospitalBlockStepZM;
            add("hospital north south connector west fin", x - 6.28f, z,
                0.42f, 0.65f, 13.55f, 0.48f,
                StartFinish::TealDoor);
            add("hospital north south connector east fin", x + 6.28f, z,
                0.42f, 0.65f, 13.55f, 0.48f,
                StartFinish::TealDoor);
        }
    }

    // Two concentrated plant yards make the roof believable without
    // scattering noise across all nine pavilions. They stay away from the
    // north-east helipad and sit on the middle-east and south-east roofs.
    for (const Vec2 plant : {block(2, 1), block(2, 2)}) {
        constexpr float kPlantBottom = 14.15f;
        add("hospital roof plant north louver screen", plant.x,
            plant.z - 6.0f, kPlantBottom, 18.0f, 2.65f, 0.25f,
            StartFinish::TealDoor, true);
        add("hospital roof plant south louver screen", plant.x,
            plant.z + 6.0f, kPlantBottom, 18.0f, 2.65f, 0.25f,
            StartFinish::TealDoor, true);
        add("hospital roof plant west louver screen", plant.x - 9.0f,
            plant.z, kPlantBottom, 0.25f, 2.65f, 12.0f,
            StartFinish::TealDoor, true);
        add("hospital roof plant east louver screen", plant.x + 9.0f,
            plant.z, kPlantBottom, 0.25f, 2.65f, 12.0f,
            StartFinish::TealDoor, true);
        add("hospital roof air handler west", plant.x - 4.7f, plant.z,
            kPlantBottom + 0.03f, 6.5f, 1.60f, 4.2f,
            StartFinish::Steel, true);
        add("hospital roof air handler east", plant.x + 4.7f, plant.z,
            kPlantBottom + 0.03f, 6.5f, 1.60f, 4.2f,
            StartFinish::Steel, true);
        add("hospital roof plant exhaust stack", plant.x + 4.7f, plant.z,
            kPlantBottom + 1.63f, 0.85f, 2.20f, 0.85f,
            StartFinish::Steel, true);
    }

    // A small glazed stair lantern and guard rail establish an occupied,
    // maintained roof above the main lobby. The rail has four real corner
    // posts and four bars rather than a solid invisible blocking rectangle.
    const Vec2 lobby = block(0, 0);
    add("hospital main lobby roof stair lantern", lobby.x, lobby.z - 2.0f,
        14.15f, 7.0f, 2.60f, 5.0f, StartFinish::Glass);
    add("hospital main lobby roof stair lantern cap", lobby.x,
        lobby.z - 2.0f, 16.75f, 7.5f, 0.24f, 5.5f,
        StartFinish::DarkRoof, true);
    add("hospital main lobby roof north guard rail", lobby.x,
        lobby.z - 6.6f, 15.24f, 12.0f, 0.10f, 0.10f,
        StartFinish::Steel, true);
    add("hospital main lobby roof south guard rail", lobby.x,
        lobby.z + 2.6f, 15.24f, 12.0f, 0.10f, 0.10f,
        StartFinish::Steel, true);
    add("hospital main lobby roof west guard rail", lobby.x - 6.0f,
        lobby.z - 2.0f, 15.24f, 0.10f, 0.10f, 9.2f,
        StartFinish::Steel, true);
    add("hospital main lobby roof east guard rail", lobby.x + 6.0f,
        lobby.z - 2.0f, 15.24f, 0.10f, 0.10f, 9.2f,
        StartFinish::Steel, true);
    for (float x : {-6.0f, 6.0f}) {
        for (float z : {-6.6f, 2.6f}) {
            add("hospital main lobby roof guard post", lobby.x + x,
                lobby.z + z, 14.18f, 0.12f, 1.16f, 0.12f,
                StartFinish::Steel, true);
        }
    }

    // Main-entry depth. Tall fins flank the clear 7 m walking route and the
    // two canopy ribs stay over 5 m high. The west-return art panel is a
    // unique 3:2 receiver: its full generated image maps once, without repeat.
    for (float x : {-5.25f, 5.25f}) {
        add("hospital main entry identity fin", lobby.x + x,
            lobby.z - 18.28f, 0.30f, 0.34f, 14.10f, 0.72f,
            StartFinish::TealDoor, true);
    }
    for (float x : {-3.0f, 3.0f}) {
        add("hospital main entry canopy rib", lobby.x + x,
            lobby.z - 20.0f, 5.28f, 0.16f, 0.20f, 5.6f,
            StartFinish::Steel);
    }
    add("hospital northwest healing art panel backing", lobby.x - 27.00f,
        lobby.z - 8.0f, 4.82f, 0.10f, 4.36f, 6.36f,
        StartFinish::Steel, true);
    add("hospital northwest healing art glass face", lobby.x - 27.08f,
        lobby.z - 8.0f, 5.00f, 0.06f, 4.00f, 6.00f,
        StartFinish::Glass);

    return out;
}

}  // namespace apricot::city
