#pragma once

#include <vector>

#include "city/hospital_campus.h"
#include "city/hospital_detail_reception.h"
#include "city/hospital_detail_pharmacy.h"
#include "city/hospital_detail_surfaces.h"
#include "city/hospital_detail_waiting.h"
#include "city/hospital_detail_emergency.h"
#include "city/hospital_detail_ward.h"
#include "city/hospital_detail_diagnostics.h"

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
    out.reserve(850);

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
        add("hospital interior waiting chair seat tex upholstery", x, z, 0.66f, 0.92f,
            0.12f, 0.72f, StartFinish::TealDoor, true);
        add("hospital interior waiting chair back tex upholstery", x, z + 0.28f, 0.76f,
            0.92f, 0.72f, 0.14f, StartFinish::Steel, true);
        for (float dx : {-0.34f, 0.34f}) {
            for (float dz : {-0.25f, 0.25f}) {
                add("hospital interior waiting chair leg tex steel", x + dx, z + dz,
                    0.315f, 0.12f, 0.36f, 0.12f, StartFinish::Steel, true);
            }
        }
    };

    const auto add_lobby_bench = [&](float x, float z, float width) {
        add("hospital interior lobby bench seat tex upholstery", x, z, 0.66f, width, 0.14f,
            0.72f, StartFinish::WarmWall, true);
        add("hospital interior lobby bench back tex upholstery", x, z + 0.29f, 0.78f,
            width, 0.70f, 0.14f, StartFinish::TealDoor, true);
        for (float end : {-0.42f, 0.42f}) {
            add("hospital interior lobby bench pedestal tex steel", x + end * width,
                z, 0.315f, 0.18f, 0.355f, 0.56f, StartFinish::Steel, true);
        }
    };

    // Main public lobby: the reception counter sits west of the straight
    // entrance axis, with a lowered accessible section at its east end.
    // Nothing solid occupies x[-4.5,4.5] from the front doors to the rear hall.
    add("hospital interior main reception counter base tex laminate", -11.4f, 6.8f,
        0.30f, 6.5f, 0.88f, 1.08f, StartFinish::TealDoor, true);
    add("hospital interior main reception counter worktop tex laminate", -11.4f, 6.8f,
        1.16f, 6.9f, 0.12f, 1.34f, StartFinish::White, true);
    add("hospital interior accessible reception counter base tex laminate", -6.3f, 6.8f,
        0.30f, 2.9f, 0.60f, 1.08f, StartFinish::WarmWall, true);
    add("hospital interior accessible reception counter worktop tex laminate", -6.3f,
        6.8f, 0.88f, 3.0f, 0.12f, 1.34f, StartFinish::White, true);
    add("hospital interior reception desk teal front rail", -11.4f, 6.21f,
        0.48f, 6.3f, 0.20f, 0.06f, StartFinish::TealDoor);
    for (float x : {-13.5f, -11.3f, -9.1f}) {
        add("hospital interior reception terminal base tex steel", x, 6.35f, 1.28f,
            0.42f, 0.42f, 0.12f, StartFinish::Steel);
        add("hospital interior reception terminal screen tex records-screen", x, 6.38f, 1.68f,
            0.56f, 0.42f, 0.08f, StartFinish::Glass);
    }
    add("hospital interior main lobby reception sign tex reception-sign",
        -10.0f, 5.90f, 2.48f, 3.2f, 0.80f, 0.035f, StartFinish::White);

    // Three self-service check-in kiosks face the west-side reception desk.
    // Their bases are compact and leave the central aisle and the east lounge
    // walk clear.
    for (float x : {12.0f, 17.0f, 22.0f}) {
        add("hospital interior lobby check-in kiosk pedestal tex steel", x, 5.2f,
            0.30f, 0.72f, 1.05f, 0.62f, StartFinish::Steel, true);
        add("hospital interior lobby check-in kiosk screen tex records-screen", x, 4.86f,
            1.34f, 0.72f, 0.54f, 0.10f, StartFinish::Glass);
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
            z, 0.325f, 1.35f, 0.025f, 1.55f, StartFinish::Yellow);
        add("hospital interior lobby wheelchair bay side stripe", 22.55f,
            z, 0.327f, 0.08f, 0.026f, 1.30f, StartFinish::White);
        add("hospital interior lobby wheelchair bay side stripe", 23.85f,
            z, 0.327f, 0.08f, 0.026f, 1.30f, StartFinish::White);
    }
    add_lobby_bench(34.0f, 13.2f, 7.2f);
    add_lobby_bench(34.0f, 20.2f, 7.2f);

    // Pharmacy pick-up and hydration sit against the east edge of the lobby.
    // The low queue rail guides visitors without fencing the room into lanes.
    add("hospital interior pharmacy pickup counter base tex laminate", 34.4f, 5.8f,
        0.30f, 8.4f, 0.92f, 1.00f, StartFinish::WarmWall, true);
    add("hospital interior pharmacy pickup counter cap tex laminate", 34.4f, 5.8f,
        1.20f, 8.8f, 0.12f, 1.22f, StartFinish::White, true);
    add("hospital interior pharmacy pickup teal fascia", 34.4f, 5.25f,
        0.52f, 8.0f, 0.28f, 0.06f, StartFinish::TealDoor);
    add("hospital interior pharmacy sign tex pharmacy-sign", 34.4f, 5.45f,
        2.48f, 3.2f, 0.80f, 0.035f, StartFinish::White);
    add("hospital interior lobby water refill station body tex steel", 39.4f, 22.8f,
        0.30f, 1.35f, 1.80f, 0.62f, StartFinish::Steel, true);
    add("hospital interior lobby water refill station face", 39.4f, 22.46f,
        0.85f, 0.92f, 0.90f, 0.08f, StartFinish::Glass);
    add("hospital interior lobby water refill drip tray", 39.4f, 22.42f,
        0.62f, 0.72f, 0.05f, 0.38f, StartFinish::TealDoor);

    // The hanging directory keeps more than two metres of head clearance.
    add("hospital interior lobby suspended directory rail tex steel", 0.0f, 23.0f,
        3.34f, 4.2f, 0.08f, 0.16f, StartFinish::White);
    add("hospital interior lobby directory tex directory-sign", 0.0f, 22.90f,
        2.32f, 4.0f, 1.0f, 0.035f, StartFinish::White);

    // Diagnostics has its own scale of check-in and waiting. The north door
    // at x=60 has a full 8 m approach aisle, with registration to the east.
    add("hospital interior diagnostic registration counter base tex laminate", 78.0f,
        7.0f, 0.30f, 13.0f, 0.86f, 1.08f, StartFinish::Steel, true);
    add("hospital interior diagnostic registration counter cap tex laminate", 78.0f,
        7.0f, 1.14f, 13.5f, 0.12f, 1.34f, StartFinish::White, true);
    add("hospital interior diagnostic registration teal fascia", 78.0f,
        6.42f, 0.48f, 12.5f, 0.20f, 0.06f, StartFinish::TealDoor);
    for (float x : {74.0f, 78.0f, 82.0f}) {
        add("hospital interior diagnostic terminal base tex steel", x, 6.50f, 1.25f,
            0.38f, 0.38f, 0.12f, StartFinish::Steel);
        add("hospital interior diagnostic terminal screen tex records-screen", x, 6.53f, 1.62f,
            0.56f, 0.42f, 0.08f, StartFinish::Glass);
    }
    add("hospital interior diagnostic registration sign tex diagnostics-sign", 78.0f,
        5.98f, 2.48f, 3.2f, 0.80f, 0.035f, StartFinish::White);

    for (float x : {98.0f, 101.0f, 104.0f, 107.0f}) {
        add_waiting_chair(x, 15.2f);
        add_waiting_chair(x, 22.2f);
    }
    add("hospital interior diagnostic waiting wheelchair bay outline", 112.0f,
        15.2f, 0.325f, 1.35f, 0.025f, 1.55f, StartFinish::Yellow);
    add("hospital interior diagnostic waiting wheelchair bay outline", 112.0f,
        22.2f, 0.325f, 1.35f, 0.025f, 1.55f, StartFinish::Yellow);
    add_lobby_bench(129.0f, 15.2f, 7.2f);
    add_lobby_bench(129.0f, 22.2f, 7.2f);

    // A quiet waiting-room backdrop gives the public lounge a room boundary.
    // Two broad gaps connect it to the rear hall; the main entry axis is open.
    for (const auto span : {Vec2{8.0f, 20.0f}, Vec2{24.0f, 36.0f},
                            Vec2{39.0f, 43.0f}}) {
        const float centre = (span.x + span.z) * 0.5f;
        const float width = span.z - span.x;
        add("hospital interior waiting room partition tex wallpaint",
            centre, 27.5f, 0.315f, width, 3.115f, 0.18f, StartFinish::White, true);
        add("hospital interior waiting room protection rail tex steel",
            centre, 27.37f, 0.93f, width, 0.12f, 0.08f, StartFinish::White);
        add("hospital interior waiting room skirting",
            centre, 27.39f, 0.315f, width, 0.14f, 0.04f, StartFinish::TealDoor);
    }

    for (const auto centre : {Vec2{-10.0f, 5.90f}, Vec2{34.4f, 5.45f},
                              Vec2{78.0f, 5.98f}}) {
        for (const float dx : {-1.1f, 1.1f}) {
            add("hospital interior department sign hanger tex steel",
                centre.x + dx, centre.z, 3.28f, 0.035f, 0.15f, 0.035f,
                StartFinish::White);
        }
    }

    const auto reception = bake_hospital_detail_reception();
    out.insert(out.end(), reception.begin(), reception.end());
    const auto pharmacy = bake_hospital_detail_pharmacy();
    out.insert(out.end(), pharmacy.begin(), pharmacy.end());
    const auto surfaces = bake_hospital_detail_surfaces();
    out.insert(out.end(), surfaces.begin(), surfaces.end());
    const auto waiting = bake_hospital_detail_waiting();
    out.insert(out.end(), waiting.begin(), waiting.end());
    const auto emergency = bake_hospital_detail_emergency();
    out.insert(out.end(), emergency.begin(), emergency.end());
    const auto ward = bake_hospital_detail_ward();
    out.insert(out.end(), ward.begin(), ward.end());
    const auto diagnostics = bake_hospital_detail_diagnostics();
    out.insert(out.end(), diagnostics.begin(), diagnostics.end());
    return out;
}

}  // namespace apricot::city
