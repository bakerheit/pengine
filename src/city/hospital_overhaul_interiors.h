#pragma once

#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Fine-grain public interior fixtures sit behind the north-facing glass.
// Circulation remains open through the x=0 lobby axis and the diagnostic door
// at x=60; these pieces dress the waiting and service bays beside those paths.
inline constexpr float kHospitalLobbyClearAisleMinX = -4.5f;
inline constexpr float kHospitalLobbyClearAisleMaxX = 4.5f;
inline constexpr float kHospitalDiagnosticClearAisleMinX = 56.0f;
inline constexpr float kHospitalDiagnosticClearAisleMaxX = 64.0f;

static_assert(kHospitalLobbyClearAisleMinX < 0.0f &&
                  kHospitalLobbyClearAisleMaxX > 0.0f &&
                  kHospitalDiagnosticClearAisleMinX < 60.0f &&
                  kHospitalDiagnosticClearAisleMaxX > 60.0f,
              "hospital public aisles must stay centred on their entrances");

inline std::vector<StartPart> bake_hospital_overhaul_interiors() {
    std::vector<StartPart> out;
    out.reserve(150);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };

    const auto add_waiting_chair = [&](float x, float z) {
        // The chair backs face south, toward the room. Their low, separated
        // frames give the public lounge a readable silhouette through glass.
        add("hospital interior waiting chair seat", x, z, 0.48f, 0.92f,
            0.12f, 0.72f, StartFinish::TealDoor, true);
        add("hospital interior waiting chair back", x, z + 0.28f, 0.59f,
            0.92f, 0.72f, 0.14f, StartFinish::Steel, true);
        for (float dx : {-0.34f, 0.34f}) {
            for (float dz : {-0.25f, 0.25f}) {
                add("hospital interior waiting chair leg", x + dx, z + dz,
                    0.30f, 0.12f, 0.22f, 0.12f, StartFinish::Steel, true);
            }
        }
    };

    const auto add_lobby_bench = [&](float x, float z, float width) {
        add("hospital interior lobby bench seat", x, z, 0.48f, width, 0.14f,
            0.72f, StartFinish::WarmWall, true);
        add("hospital interior lobby bench back", x, z + 0.29f, 0.60f,
            width, 0.70f, 0.14f, StartFinish::TealDoor, true);
        for (float end : {-0.42f, 0.42f}) {
            add("hospital interior lobby bench pedestal", x + end * width,
                z, 0.30f, 0.18f, 0.20f, 0.56f, StartFinish::Steel, true);
        }
    };

    // Main public lobby: the reception counter sits west of the straight
    // entrance axis, with a lowered accessible section at its east end.
    // Nothing solid occupies x[-4.5,4.5] from the front doors to the rear hall.
    add("hospital interior main reception counter base", -11.4f, 6.8f,
        0.30f, 6.5f, 0.88f, 1.08f, StartFinish::TealDoor, true);
    add("hospital interior main reception counter worktop", -11.4f, 6.8f,
        1.16f, 6.9f, 0.12f, 1.34f, StartFinish::White, true);
    add("hospital interior accessible reception counter base", -6.3f, 6.8f,
        0.30f, 2.9f, 0.60f, 1.08f, StartFinish::WarmWall, true);
    add("hospital interior accessible reception counter worktop", -6.3f,
        6.8f, 0.88f, 3.0f, 0.12f, 1.34f, StartFinish::White, true);
    add("hospital interior reception desk teal front rail", -11.4f, 6.21f,
        0.48f, 6.3f, 0.20f, 0.06f, StartFinish::TealDoor);
    for (float x : {-13.5f, -11.3f, -9.1f}) {
        add("hospital interior reception terminal base", x, 6.35f, 1.28f,
            0.42f, 0.42f, 0.12f, StartFinish::Steel);
        add("hospital interior reception terminal screen", x, 6.38f, 1.68f,
            0.58f, 0.40f, 0.08f, StartFinish::Glass);
    }
    add("hospital interior main lobby reception sign backing", -11.4f,
        5.98f, 2.18f, 5.4f, 0.48f, 0.12f, StartFinish::WarmWall);
    add("hospital interior main lobby reception sign band", -11.4f, 5.90f,
        2.20f, 4.5f, 0.12f, 0.04f, StartFinish::TealDoor);

    // Three self-service check-in kiosks face the west-side reception desk.
    // Their bases are compact and leave the central aisle and the east lounge
    // walk clear.
    for (float x : {12.0f, 17.0f, 22.0f}) {
        add("hospital interior lobby check-in kiosk pedestal", x, 5.2f,
            0.30f, 0.72f, 1.05f, 0.62f, StartFinish::Steel, true);
        add("hospital interior lobby check-in kiosk screen", x, 4.86f,
            1.34f, 0.66f, 0.70f, 0.10f, StartFinish::Glass);
        add("hospital interior lobby check-in kiosk teal bezel", x, 4.79f,
            1.30f, 0.76f, 0.08f, 0.06f, StartFinish::TealDoor);
    }

    // Two short waiting rows sit off the door-to-core route. Paired wheelchair
    // bays are marked at the aisle ends instead of taking chair positions.
    for (float x : {11.0f, 14.0f, 17.0f}) {
        add_waiting_chair(x, 13.2f);
        add_waiting_chair(x, 20.2f);
    }
    for (float z : {13.2f, 20.2f}) {
        add("hospital interior lobby wheelchair waiting bay outline", 23.2f,
            z, 0.305f, 1.35f, 0.025f, 1.55f, StartFinish::Yellow);
        add("hospital interior lobby wheelchair bay side stripe", 22.55f,
            z, 0.307f, 0.08f, 0.026f, 1.30f, StartFinish::White);
        add("hospital interior lobby wheelchair bay side stripe", 23.85f,
            z, 0.307f, 0.08f, 0.026f, 1.30f, StartFinish::White);
    }
    add_lobby_bench(34.0f, 13.2f, 7.2f);
    add_lobby_bench(34.0f, 20.2f, 7.2f);

    // Pharmacy pick-up and hydration sit against the east edge of the lobby.
    // The low queue rail guides visitors without fencing the room into lanes.
    add("hospital interior pharmacy pickup counter base", 34.4f, 5.8f,
        0.30f, 8.4f, 0.92f, 1.00f, StartFinish::WarmWall, true);
    add("hospital interior pharmacy pickup counter cap", 34.4f, 5.8f,
        1.20f, 8.8f, 0.12f, 1.22f, StartFinish::White, true);
    add("hospital interior pharmacy pickup teal fascia", 34.4f, 5.25f,
        0.52f, 8.0f, 0.28f, 0.06f, StartFinish::TealDoor);
    add("hospital interior pharmacy sign backing", 34.4f, 5.52f, 2.16f,
        4.6f, 0.42f, 0.10f, StartFinish::Steel);
    add("hospital interior pharmacy sign teal rule", 34.4f, 5.45f, 2.17f,
        3.8f, 0.10f, 0.04f, StartFinish::TealDoor);
    add("hospital interior lobby water refill station body", 39.4f, 22.8f,
        0.30f, 1.35f, 1.80f, 0.62f, StartFinish::Steel, true);
    add("hospital interior lobby water refill station face", 39.4f, 22.46f,
        0.85f, 0.92f, 0.90f, 0.08f, StartFinish::Glass);
    add("hospital interior lobby water refill drip tray", 39.4f, 22.42f,
        0.62f, 0.72f, 0.05f, 0.38f, StartFinish::TealDoor);

    // A suspended directory marks the rear hall. Its underside stays above
    // 3 m, so it reads from the street but never crowds pedestrian clearance.
    add("hospital interior lobby suspended directory rail", 0.0f, 23.0f,
        3.42f, 8.2f, 0.08f, 0.22f, StartFinish::Steel);
    add("hospital interior lobby suspended directory panel", 0.0f, 23.0f,
        2.86f, 7.6f, 0.56f, 0.16f, StartFinish::WarmWall);
    add("hospital interior lobby directory emergency marker", -2.3f, 22.90f,
        2.95f, 0.22f, 0.28f, 0.035f, StartFinish::RedTrim);
    add("hospital interior lobby directory clinic marker", 0.0f, 22.90f,
        2.95f, 0.22f, 0.28f, 0.035f, StartFinish::TealDoor);
    add("hospital interior lobby directory elevator marker", 2.3f, 22.90f,
        2.95f, 0.22f, 0.28f, 0.035f, StartFinish::Steel);

    // Diagnostics has its own scale of check-in and waiting. The north door
    // at x=60 has a full 8 m approach aisle, with registration to the east.
    add("hospital interior diagnostic registration counter base", 78.0f,
        7.0f, 0.30f, 13.0f, 0.86f, 1.08f, StartFinish::Steel, true);
    add("hospital interior diagnostic registration counter cap", 78.0f,
        7.0f, 1.14f, 13.5f, 0.12f, 1.34f, StartFinish::White, true);
    add("hospital interior diagnostic registration teal fascia", 78.0f,
        6.42f, 0.48f, 12.5f, 0.20f, 0.06f, StartFinish::TealDoor);
    for (float x : {74.0f, 78.0f, 82.0f}) {
        add("hospital interior diagnostic terminal base", x, 6.50f, 1.25f,
            0.38f, 0.38f, 0.12f, StartFinish::Steel);
        add("hospital interior diagnostic terminal screen", x, 6.53f, 1.62f,
            0.54f, 0.36f, 0.08f, StartFinish::Glass);
    }
    add("hospital interior diagnostic registration sign backing", 78.0f,
        6.05f, 2.08f, 5.4f, 0.42f, 0.10f, StartFinish::WarmWall);
    add("hospital interior diagnostic registration sign teal rule", 78.0f,
        5.98f, 2.09f, 4.3f, 0.10f, 0.04f, StartFinish::TealDoor);

    for (float x : {98.0f, 101.0f, 104.0f, 107.0f}) {
        add_waiting_chair(x, 15.2f);
        add_waiting_chair(x, 22.2f);
    }
    add("hospital interior diagnostic waiting wheelchair bay outline", 112.0f,
        15.2f, 0.305f, 1.35f, 0.025f, 1.55f, StartFinish::Yellow);
    add("hospital interior diagnostic waiting wheelchair bay outline", 112.0f,
        22.2f, 0.305f, 1.35f, 0.025f, 1.55f, StartFinish::Yellow);
    add_lobby_bench(129.0f, 15.2f, 7.2f);
    add_lobby_bench(129.0f, 22.2f, 7.2f);

    return out;
}

}  // namespace apricot::city
