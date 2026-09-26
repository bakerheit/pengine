#pragma once

#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Public circulation is authored entirely in kHospitalSite local space.
// These anchors are shared with the north lot, the replacement massing, and
// the garage so parent-side tests can pin the actual routes instead of names.
inline constexpr float kHospitalOverhaulLobbyAxisX = 0.0f;
inline constexpr float kHospitalOverhaulArrivalLaneZ = -14.7f;
inline constexpr float kHospitalOverhaulArrivalLaneMinX = -41.0f;
inline constexpr float kHospitalOverhaulArrivalLaneMaxX = 42.0f;
inline constexpr float kHospitalOverhaulArrivalLaneMinZ = -18.2f;
inline constexpr float kHospitalOverhaulArrivalLaneMaxZ = -11.2f;
inline constexpr float kHospitalOverhaulArrivalEntryMinX = -39.0f;
inline constexpr float kHospitalOverhaulArrivalEntryMaxX = -22.5f;
inline constexpr float kHospitalOverhaulArrivalEntryMinZ = -31.0f;
inline constexpr float kHospitalOverhaulArrivalEntryMaxZ = -10.7f;
inline constexpr float kHospitalOverhaulArrivalExitMinX = 35.5f;
inline constexpr float kHospitalOverhaulArrivalExitMaxX = 42.5f;
inline constexpr float kHospitalOverhaulArrivalExitMinZ = -31.0f;
inline constexpr float kHospitalOverhaulArrivalExitMaxZ = -18.2f;
inline constexpr float kHospitalOverhaulGarageWalkAxisX = 46.0f;
inline constexpr float kHospitalOverhaulGarageWalkWidthM = 3.2f;

constexpr bool hospital_overhaul_public_vehicle_sweep_contains(float x,
                                                                 float z) {
    const bool curbside_lane =
        x >= kHospitalOverhaulArrivalLaneMinX &&
        x <= kHospitalOverhaulArrivalLaneMaxX &&
        z >= kHospitalOverhaulArrivalLaneMinZ &&
        z <= kHospitalOverhaulArrivalLaneMaxZ;
    const bool tenth_entry =
        x >= kHospitalOverhaulArrivalEntryMinX &&
        x <= kHospitalOverhaulArrivalEntryMaxX &&
        z >= kHospitalOverhaulArrivalEntryMinZ &&
        z <= kHospitalOverhaulArrivalEntryMaxZ;
    const bool tenth_exit =
        x >= kHospitalOverhaulArrivalExitMinX &&
        x <= kHospitalOverhaulArrivalExitMaxX &&
        z >= kHospitalOverhaulArrivalExitMinZ &&
        z <= kHospitalOverhaulArrivalExitMaxZ;
    return curbside_lane || tenth_entry || tenth_exit;
}

inline std::vector<StartPart> bake_hospital_overhaul_mobility() {
    std::vector<StartPart> out;
    out.reserve(220);

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

    const auto add_east_arrow = [&](const char* name, float x, float z) {
        add(name, x, z, 0.215f, 2.35f, 0.025f, 0.18f,
            StartFinish::White);
        add(name, x + 0.85f, z - 0.42f, 0.215f, 1.15f, 0.025f, 0.18f,
            StartFinish::White, false, 0.0f, 42.0f);
        add(name, x + 0.85f, z + 0.42f, 0.215f, 1.15f, 0.025f, 0.18f,
            StartFinish::White, false, 0.0f, -42.0f);
    };

    const auto add_north_arrow = [&](const char* name, float x, float z) {
        add(name, x, z, 0.215f, 0.18f, 0.025f, 2.35f,
            StartFinish::White);
        add(name, x - 0.42f, z - 0.85f, 0.215f, 1.15f, 0.025f, 0.18f,
            StartFinish::White, false, 0.0f, 48.0f);
        add(name, x + 0.42f, z - 0.85f, 0.215f, 1.15f, 0.025f, 0.18f,
            StartFinish::White, false, 0.0f, 132.0f);
    };

    const auto add_bollard = [&](const char* name, float x, float z,
                                 StartFinish accent) {
        add(name, x, z, 0.16f, 0.24f, 0.94f, 0.24f,
            StartFinish::Steel, true);
        add("hospital mobility bollard cap", x, z, 1.10f, 0.31f, 0.10f,
            0.31f, accent);
    };

    const auto add_path_light = [&](float x, float z) {
        add("hospital mobility path light base", x, z, 0.16f, 0.52f,
            0.18f, 0.52f, StartFinish::Concrete, true);
        add("hospital mobility path light pole", x, z, 0.34f, 0.16f,
            3.08f, 0.16f, StartFinish::Steel, true);
        // This existing receiver name is already used by the hospital's real
        // dusk-light route; it is not a substitute for the runtime light.
        add("hospital grounds path light lens", x, z, 3.42f, 0.54f,
            0.22f, 0.54f, StartFinish::White);
    };

    const auto add_bike_stand = [&](float x, float z) {
        add("hospital mobility bike rack post", x, z - 0.62f, 0.16f,
            0.12f, 0.86f, 0.12f, StartFinish::Steel, true);
        add("hospital mobility bike rack post", x, z + 0.62f, 0.16f,
            0.12f, 0.86f, 0.12f, StartFinish::Steel, true);
        add("hospital mobility bike rack crown", x, z, 0.96f, 0.12f,
            0.12f, 1.36f, StartFinish::Steel, true);
    };

    const auto add_wayfinding_pylon = [&](float x, float z, float yaw,
                                          StartFinish band) {
        add("hospital mobility wayfinding pylon footing", x, z, 0.16f,
            1.55f, 0.18f, 0.90f, StartFinish::Concrete, true, 0.0f, yaw);
        add("hospital mobility wayfinding pylon body", x, z, 0.34f,
            1.26f, 3.35f, 0.40f, StartFinish::Steel, true, 0.0f, yaw);
        add("hospital mobility wayfinding pylon color band", x, z - 0.23f,
            2.78f, 1.10f, 0.55f, 0.035f, band, false, 0.0f, yaw);
        // A material-built hospital cross stays readable without adding or
        // repeating a raster sign texture.
        add("hospital mobility wayfinding hospital cross", x, z - 0.255f,
            1.32f, 0.64f, 0.17f, 0.035f, StartFinish::White, false, 0.0f,
            yaw);
        add("hospital mobility wayfinding hospital cross", x, z - 0.255f,
            1.08f, 0.17f, 0.65f, 0.035f, StartFinish::White, false, 0.0f,
            yaw);
    };

    // One-way public pull-through: enter from a western Tenth Street curb cut,
    // load under the porte cochere, and leave through a separate eastern Tenth
    // curb cut. Spacing both mouths away from the Bellweather and Juniper
    // junctions gives full-sized sight triangles. The route crosses the lobby
    // walk once, on a raised-looking but flush support table. No solid props
    // are placed inside the clear sweep constants above.
    add("hospital mobility public arrival asphalt lane", 7.75f,
        kHospitalOverhaulArrivalLaneZ, 0.10f, 68.5f, 0.10f, 7.5f,
        StartFinish::Asphalt);
    add("hospital mobility Tenth Street entry throat", -35.0f, -27.1f,
        0.10f, 7.5f, 0.10f, 7.8f, StartFinish::Asphalt);
    add("hospital mobility Tenth Street exit throat", 39.0f, -24.6f, 0.10f,
        7.0f, 0.10f, 12.8f, StartFinish::Asphalt);

    // Nine overlapping 18-sided paving discs trace a broad quarter-circle
    // from the northbound street throat into the eastbound passenger lane.
    // Their union reads as one smooth 7.5 m carriageway instead of a square
    // T-junction, while the shared support/collision path remains drivable.
    constexpr Vec2 kArrivalCurve[] = {
        {-35.0000f, -23.2000f}, {-34.8367f, -21.5417f},
        {-34.3530f, -19.9472f}, {-33.5675f, -18.4777f},
        {-32.5104f, -17.1896f}, {-31.2223f, -16.1175f},
        {-29.7528f, -15.3470f}, {-28.1583f, -14.8633f},
        {-26.5000f, -14.7000f},
    };
    for (const Vec2 centre : kArrivalCurve) {
        add("hospital mobility curved arrival apron", centre.x, centre.z,
            0.10f, 7.5f, 0.10f, 7.5f, StartFinish::Asphalt);
    }

    add_east_arrow("hospital mobility public arrival direction arrow",
                   -25.0f, -14.7f);
    add_east_arrow("hospital mobility public arrival direction arrow",
                   25.0f, -14.7f);
    add_north_arrow("hospital mobility public arrival direction arrow",
                    39.0f, -22.5f);

    // Passenger curb and clear bay divisions. The central 5.2 m gap is the
    // only pedestrian crossing. Short-stay blue-green and taxi yellow bands
    // are geometry, not repeated sign art.
    add("hospital mobility public arrival passenger curb west", -9.35f,
        -10.92f, 0.12f, 13.3f, 0.22f, 0.34f,
        StartFinish::Concrete, true);
    add("hospital mobility public arrival passenger curb east", 17.35f,
        -10.92f, 0.12f, 28.7f, 0.22f, 0.34f,
        StartFinish::Concrete, true);
    add("hospital mobility accessible short stay curb band", -7.0f,
        -11.12f, 0.34f, 8.0f, 0.10f, 0.08f, StartFinish::TealDoor);
    add("hospital mobility short stay curb band", 7.0f, -11.12f, 0.34f,
        8.0f, 0.10f, 0.08f, StartFinish::TealDoor);
    add("hospital mobility taxi curb band", 21.0f, -11.12f, 0.34f,
        18.0f, 0.10f, 0.08f, StartFinish::Yellow);
    for (float x : {-11.0f, -3.0f, 3.0f, 11.0f, 20.0f, 30.0f}) {
        add("hospital mobility public arrival bay divider", x, -14.7f,
            0.215f, 0.14f, 0.025f, 6.2f, StartFinish::White);
    }
    // An unmistakable wheelchair-style bay mark assembled from simple shapes.
    add("hospital mobility accessible bay marker", -7.0f, -14.7f, 0.218f,
        0.70f, 0.025f, 0.70f, StartFinish::White);
    add("hospital mobility accessible bay marker", -7.0f, -15.55f, 0.218f,
        0.18f, 0.025f, 1.15f, StartFinish::White);
    add("hospital mobility accessible bay marker", -6.52f, -15.92f, 0.218f,
        0.95f, 0.025f, 0.18f, StartFinish::White, false, 0.0f, -28.0f);

    // The protected north-lot axis continues the existing x=0 parking spine
    // over Tenth Street, through a refuge gap, across the one-way lane, and to
    // the massing contract's main lobby opening at z=-8.
    add("hospital mobility north crossing receiving landing", 0.0f, -21.5f,
        0.11f, 5.2f, 0.08f, 7.0f, StartFinish::Concrete);
    add("hospital mobility protected lobby walk", 0.0f, -13.1f, 0.11f,
        5.2f, 0.08f, 10.2f, StartFinish::Concrete);
    add("hospital mobility lobby threshold landing", 0.0f, -8.9f, 0.11f,
        7.0f, 0.08f, 2.0f, StartFinish::Concrete);
    for (float z : {-17.75f, -16.50f, -15.25f, -14.00f, -12.75f,
                    -11.50f}) {
        add("hospital mobility protected arrival crosswalk stripe", 0.0f,
            z, 0.225f, 5.0f, 0.025f, 0.48f, StartFinish::White);
    }
    for (float z : {-18.65f, -10.55f}) {
        for (float x : {-1.80f, -0.60f, 0.60f, 1.80f}) {
            add("hospital mobility accessible tactile paving", x, z,
                0.205f, 1.05f, 0.025f, 0.66f, StartFinish::Yellow);
        }
    }
    // At-grade curb cuts keep the route step-free. They are broad support
    // planes, not solid boxes or fake curbs across the travel line.
    add("hospital mobility north crossing flush curb ramp", 0.0f, -18.65f,
        0.105f, 5.2f, 0.055f, 1.60f, StartFinish::Concrete);
    add("hospital mobility lobby flush curb ramp", 0.0f, -10.55f, 0.105f,
        5.2f, 0.055f, 1.60f, StartFinish::Concrete);
    // Paired edge inlays keep the full protected width visually obvious from
    // the lot landing, across the arrival lane, and through the lobby landing.
    // They are flush paint; tactile warning pads remain at both curb lines.
    for (float x : {-2.55f, 2.55f}) {
        add("hospital mobility protected lobby walk edge inlay", x, -16.4f,
            0.205f, 0.10f, 0.025f, 16.8f, StartFinish::TealDoor);
    }
    // A material-built destination mark makes the lobby threshold legible
    // from the crossing without putting a post or sign in the door approach.
    add("hospital mobility lobby threshold hospital cross field", 0.0f,
        -8.9f, 0.205f, 1.60f, 0.025f, 1.60f, StartFinish::TealDoor);
    add("hospital mobility lobby threshold hospital cross", 0.0f, -8.9f,
        0.218f, 0.92f, 0.025f, 0.18f, StartFinish::White);
    add("hospital mobility lobby threshold hospital cross", 0.0f, -8.9f,
        0.218f, 0.18f, 0.025f, 0.92f, StartFinish::White);
    for (float x : {-3.5f, 3.5f}) {
        add_bollard("hospital mobility north crossing guard bollard", x,
                    -19.35f, StartFinish::TealDoor);
        add_bollard("hospital mobility lobby guard bollard", x, -9.5f,
                    StartFinish::TealDoor);
    }

    // Split refuge planters make the single vehicle conflict self-evident.
    // Their tall elements stay outside both Tenth Street sight fans.
    for (float x : {-9.25f, 17.75f}) {
        const float width = x < 0.0f ? 13.0f : 28.5f;
        add("hospital mobility arrival refuge planter curb", x, -21.2f,
            0.12f, width, 0.26f, 3.3f, StartFinish::Concrete, true);
        add("hospital mobility arrival refuge planter soil", x, -21.2f,
            0.39f, width - 0.7f, 0.05f, 2.65f,
            StartFinish::DarkRoof);
        add("hospital mobility arrival refuge low planting", x - width * 0.23f,
            -21.2f, 0.44f, width * 0.32f, 0.55f, 1.40f,
            StartFinish::TealDoor);
        add("hospital mobility arrival refuge low planting", x + width * 0.23f,
            -21.2f, 0.44f, width * 0.32f, 0.68f, 1.30f,
            StartFinish::TealDoor);
    }

    // A civic-scale porte cochere covers the passenger curb and vehicle lane.
    // Supports sit on the two refuge islands and behind the passenger curb,
    // outside all clear vehicle sweep envelopes and the 5.2 m lobby axis.
    add("hospital mobility public arrival canopy roof", 9.5f, -14.2f,
        5.25f, 47.0f, 0.24f, 12.4f, StartFinish::WarmWall, true);
    add("hospital mobility public arrival canopy north fascia", 9.5f,
        -20.28f, 4.82f, 47.0f, 0.43f, 0.24f,
        StartFinish::TealDoor, true);
    add("hospital mobility public arrival canopy south fascia", 9.5f,
        -8.02f, 4.82f, 47.0f, 0.43f, 0.24f,
        StartFinish::TealDoor, true);
    for (float x : {-11.5f, 30.5f}) {
        for (float z : {-19.3f, -8.9f}) {
            add("hospital mobility public arrival canopy column", x, z,
                0.16f, 0.42f, 4.66f, 0.42f, StartFinish::Steel, true);
        }
    }
    for (float x : {-8.0f, 0.0f, 8.0f, 16.0f, 24.0f}) {
        add("hospital mobility public arrival canopy underside rib", x,
            -14.2f, 4.68f, 0.14f, 0.12f, 11.4f,
            StartFinish::Steel);
        add("hospital arrival canopy light lens", x, -14.2f, 4.58f,
            0.48f, 0.08f, 0.48f, StartFinish::White);
    }

    // A connected public-side perimeter route wraps the north, west, and
    // southwest clinical bars, then feeds the garage. It deliberately stops
    // at the x=144 operational boundary instead of crossing ED or service.
    add("hospital mobility north perimeter walk", 64.5f, -9.6f, 0.11f,
        165.0f, 0.08f, 3.2f, StartFinish::Concrete);
    add("hospital mobility west perimeter walk", -18.2f, 65.5f, 0.11f,
        3.2f, 0.08f, 150.2f, StartFinish::Concrete);
    add("hospital mobility southwest perimeter walk", 13.9f, 141.5f,
        0.11f, 67.4f, 0.08f, 3.2f, StartFinish::Concrete);
    add("hospital mobility garage ground walk", 46.0f, 154.3f, 0.11f,
        kHospitalOverhaulGarageWalkWidthM, 0.08f, 25.6f,
        StartFinish::Concrete);
    add("hospital mobility garage north-door landing", 46.0f, 166.6f,
        0.11f, 6.0f, 0.08f, 2.2f, StartFinish::Concrete);
    add("hospital mobility hospital south-door landing", 46.0f, 140.1f,
        0.11f, 6.0f, 0.08f, 2.4f, StartFinish::Concrete);
    for (float z : {145.0f, 153.0f, 161.0f}) {
        add("hospital mobility garage walk edge marker", 44.25f, z,
            0.205f, 0.12f, 0.025f, 3.0f, StartFinish::TealDoor);
        add("hospital mobility garage walk edge marker", 47.75f, z,
            0.205f, 0.12f, 0.025f, 3.0f, StartFinish::TealDoor);
    }

    // Bellweather transit stop: boarding pad nearest the street, furniture
    // behind it, and a direct transverse walk to the west perimeter route.
    // Nothing enters the western Tenth Street arrival sight triangle.
    add("hospital mobility Bellweather transit boarding pad", -31.0f,
        50.0f, 0.11f, 6.8f, 0.08f, 17.0f, StartFinish::Concrete);
    add("hospital mobility transit accessible connector", -24.6f, 50.0f,
        0.11f, 9.8f, 0.08f, 3.2f, StartFinish::Concrete);
    add("hospital mobility transit boarding curb band", -34.48f, 50.0f,
        0.20f, 0.18f, 0.08f, 14.0f, StartFinish::TealDoor);
    add("hospital mobility transit shelter roof", -28.8f, 50.0f, 3.30f,
        3.8f, 0.24f, 11.5f, StartFinish::WarmWall, true);
    add("hospital mobility transit shelter fascia", -30.62f, 50.0f,
        2.92f, 0.16f, 0.38f, 11.5f, StartFinish::TealDoor, true);
    for (float z : {45.0f, 55.0f}) {
        add("hospital mobility transit shelter column", -27.25f, z, 0.16f,
            0.34f, 3.14f, 0.34f, StartFinish::Steel, true);
        add("hospital mobility transit shelter column", -30.35f, z, 0.16f,
            0.34f, 3.14f, 0.34f, StartFinish::Steel, true);
    }
    // Keep the solid bench at the north end so the transverse accessible
    // connector at z=50 passes the shelter without a collision pinch point.
    add("hospital mobility transit shelter bench seat", -28.2f, 46.0f,
        0.54f, 0.70f, 0.16f, 3.2f, StartFinish::WarmWall, true);
    add("hospital mobility transit shelter bench back", -27.88f, 46.0f,
        0.70f, 0.14f, 0.86f, 3.2f, StartFinish::WarmWall, true);
    add("hospital mobility transit stop pole", -33.2f, 42.6f, 0.16f,
        0.18f, 3.5f, 0.18f, StartFinish::Steel, true);
    add("hospital mobility transit stop marker", -33.2f, 42.6f, 2.90f,
        0.75f, 0.75f, 0.12f, StartFinish::TealDoor);
    // A compact, high-contrast bus pictogram identifies the boarding pole at
    // street distance without adding a new fitted texture receiver.
    add("hospital mobility transit bus pictogram body", -33.2f, 42.53f,
        3.17f, 0.38f, 0.33f, 0.035f, StartFinish::White);
    add("hospital mobility transit bus pictogram windows", -33.2f, 42.505f,
        3.34f, 0.25f, 0.08f, 0.015f, StartFinish::Steel);
    for (float x : {-33.32f, -33.08f}) {
        add("hospital mobility transit bus pictogram wheel", x, 42.505f,
            3.11f, 0.08f, 0.12f, 0.02f, StartFinish::Steel);
    }
    add("hospital mobility transit tactile boarding strip", -33.7f, 50.0f,
        0.205f, 0.72f, 0.025f, 12.0f, StartFinish::Yellow);
    for (float z : {48.56f, 51.44f}) {
        add("hospital mobility transit connector edge inlay", -24.6f, z,
            0.205f, 9.4f, 0.025f, 0.10f, StartFinish::TealDoor);
    }
    add_east_arrow("hospital mobility transit clinic direction arrow",
                   -24.6f, 50.0f);

    // Four inverted-U stands sit on a dedicated west-side pad. Bikes never
    // narrow the lobby route, transit boarding strip, or arrival lane.
    add("hospital mobility bicycle parking pad", -22.4f, 18.5f, 0.11f,
        5.2f, 0.08f, 12.0f, StartFinish::Concrete);
    for (float z : {14.5f, 17.2f, 19.9f, 22.6f}) {
        add_bike_stand(-22.4f, z);
    }

    // Material-built pylons reuse the same family at the arrival, transit,
    // and garage decision points. The arrival marker sits beside the crossing
    // on the forecourt apron, clear of the refuge island's planted soil.
    add_wayfinding_pylon(3.85f, -23.65f, 0.0f, StartFinish::TealDoor);
    add_wayfinding_pylon(-22.4f, 29.0f, 90.0f, StartFinish::TealDoor);
    add_wayfinding_pylon(39.5f, 145.5f, 0.0f, StartFinish::Yellow);

    // Existing real-light receiver names keep the new routes usable at night.
    // Poles sit in furniture zones at least 1.6 m off each clear walk edge.
    add_path_light(-20.0f, -26.0f);
    add_path_light(5.0f, -23.8f);
    add_path_light(-22.2f, 38.0f);
    add_path_light(-22.2f, 66.0f);
    add_path_light(-22.2f, 112.0f);
    add_path_light(40.8f, 151.0f);
    add_path_light(40.8f, 163.0f);

    return out;
}

}  // namespace apricot::city
