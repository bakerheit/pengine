#pragma once

#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Arrival polish is authored in kHospitalSite local space. These explicit
// clear zones are also useful to parent-side tests when the sidecar is wired.
inline constexpr float kHospitalMainArrivalClearHalfWidthM = 2.4f;
inline constexpr float kHospitalMainArrivalClearMinZ = -21.0f;
inline constexpr float kHospitalMainArrivalClearMaxZ = -17.6f;
inline constexpr float kHospitalEmergencyDoorClearMinX = 88.6f;
inline constexpr float kHospitalEmergencyDoorClearMaxX = 95.4f;
inline constexpr float kHospitalEmergencyVehicleSweepMinZ = -26.5f;
inline constexpr float kHospitalEmergencyVehicleSweepMaxZ = -22.3f;

inline std::vector<StartPart> bake_hospital_exterior_arrivals() {
    std::vector<StartPart> out;
    out.reserve(155);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    const auto add_canopy_light = [&](float x, float z, float bottom) {
        // Keep this exact name. Parent integration routes every visible lens
        // to a real downward light; the white finish is not fake illumination.
        add("hospital arrival canopy light lens", x, z, bottom, 0.42f,
            0.08f, 0.42f, StartFinish::White);
    };

    const auto add_bollard = [&](const char* body_name, float x, float z) {
        add(body_name, x, z, 0.16f, 0.24f, 0.96f, 0.24f,
            StartFinish::Steel, true);
        add("hospital arrival bollard cap", x, z, 1.12f, 0.31f, 0.10f,
            0.31f, StartFinish::TealDoor);
    };

    const auto add_bench = [&](float x, float z) {
        add("hospital main arrival bench seat", x, z, 0.54f, 3.8f, 0.18f,
            0.72f, StartFinish::WarmWall, true);
        add("hospital main arrival bench back", x, z + 0.34f, 0.72f, 3.8f,
            0.88f, 0.14f, StartFinish::WarmWall, true);
        add("hospital main arrival bench leg", x - 1.25f, z, 0.16f, 0.18f,
            0.38f, 0.52f, StartFinish::Steel);
        add("hospital main arrival bench leg", x + 1.25f, z, 0.16f, 0.18f,
            0.38f, 0.52f, StartFinish::Steel);
    };

    // Main arrival: extend the small base canopy into a layered civic-scale
    // shelter. Columns sit outside the 4.8 m-wide centre walk and the 18 m
    // roof stops well short of the Tenth Street centreline at local z=-31.
    add("hospital main arrival canopy roof cap", 0.0f, -22.6f, 5.28f,
        18.0f, 0.22f, 7.2f, StartFinish::WarmWall, true);
    add("hospital main arrival canopy front fascia", 0.0f, -26.18f, 4.82f,
        18.0f, 0.46f, 0.22f, StartFinish::TealDoor, true);
    add("hospital main arrival canopy west fascia", -8.89f, -22.65f,
        4.82f, 0.22f, 0.46f, 6.85f, StartFinish::TealDoor, true);
    add("hospital main arrival canopy east fascia", 8.89f, -22.65f,
        4.82f, 0.22f, 0.46f, 6.85f, StartFinish::TealDoor, true);
    for (float x : {-7.7f, 7.7f}) {
        add("hospital main arrival canopy column", x, -20.30f, 0.16f,
            0.42f, 4.66f, 0.42f, StartFinish::Steel, true);
        add("hospital main arrival canopy column capital", x, -20.30f,
            4.66f, 0.72f, 0.20f, 0.72f, StartFinish::TealDoor);
    }
    for (float x : {-6.0f, -3.0f, 0.0f, 3.0f, 6.0f}) {
        add("hospital main arrival canopy underside rib", x, -22.75f,
            4.68f, 0.13f, 0.12f, 6.45f, StartFinish::Steel);
        add_canopy_light(x, -23.55f, 4.57f);
    }

    // Door leaves and mullions sit on the existing real entrance opening.
    // They remain visual so the 4.8 m approach never gains thin snag boxes.
    for (float x : {-3.15f, -1.55f, 1.55f, 3.15f}) {
        add("hospital main arrival vestibule mullion", x, -18.11f, 0.25f,
            0.16f, 4.15f, 0.16f, StartFinish::Steel);
    }
    add("hospital main arrival vestibule transom", 0.0f, -18.11f, 3.42f,
        6.45f, 0.18f, 0.16f, StartFinish::Steel);
    for (float x : {-1.52f, 1.52f}) {
        add("hospital main arrival sliding glass door", x, -18.13f, 0.26f,
            2.85f, 3.12f, 0.05f, StartFinish::Glass);
        add("hospital main arrival door kick plate", x, -18.17f, 0.28f,
            2.65f, 0.32f, 0.035f, StartFinish::Steel);
    }

    // The centre is a flush, supported pedestrian crossing. Street furniture
    // stays on the flanks, leaving a 4.8 m axis from Tenth Street to the doors.
    add("hospital main arrival pedestrian table", 0.0f, -19.75f, 0.10f,
        4.8f, 0.10f, 2.5f, StartFinish::Concrete);
    for (float x : {-1.8f, -0.6f, 0.6f, 1.8f}) {
        add("hospital main arrival tactile paving tile", x, -20.67f, 0.205f,
            1.05f, 0.025f, 0.62f, StartFinish::Yellow);
    }
    for (float x : {-7.0f, -5.0f, 5.0f, 7.0f}) {
        add_bollard("hospital main arrival protective bollard", x, -19.40f);
    }

    // Seating, planters, and cycle parking create useful dwell edges without
    // stealing the door axis or the short drop-off throat.
    add_bench(-14.0f, -19.65f);
    add_bench(14.0f, -19.65f);
    for (float x : {-21.0f, -19.0f, -17.0f}) {
        add("hospital main arrival bike rack upright", x, -20.35f, 0.16f,
            0.10f, 0.92f, 0.10f, StartFinish::Steel);
        add("hospital main arrival bike rack upright", x, -19.45f, 0.16f,
            0.10f, 0.92f, 0.10f, StartFinish::Steel);
        add("hospital main arrival bike rack crown", x, -19.90f, 1.02f,
            0.10f, 0.10f, 1.0f, StartFinish::Steel);
    }
    add("hospital main arrival bike rack anchor rail", -19.0f, -20.35f,
        0.16f, 4.4f, 0.10f, 0.10f, StartFinish::Steel, true);
    add("hospital main arrival bike rack anchor rail", -19.0f, -19.45f,
        0.16f, 4.4f, 0.10f, 0.10f, StartFinish::Steel, true);
    for (float x : {-22.3f, 22.3f}) {
        add("hospital main arrival planter", x, -19.75f, 0.16f, 2.6f, 0.70f,
            1.35f, StartFinish::Concrete, true);
        add("hospital main arrival planter foliage", x, -19.75f, 0.86f,
            2.1f, 0.65f, 0.95f, StartFinish::TealDoor);
    }
    add("hospital main arrival west curb", -17.25f, -20.84f, 0.12f,
        13.5f, 0.20f, 0.32f, StartFinish::Concrete, true);
    add("hospital main arrival east curb", 17.25f, -20.84f, 0.12f,
        13.5f, 0.20f, 0.32f, StartFinish::Concrete, true);
    for (float x : {-7.5f, 7.5f}) {
        add("hospital main arrival trench drain", x, -20.70f, 0.205f,
            4.0f, 0.025f, 0.34f, StartFinish::DarkRoof);
    }

    // Emergency arrival: a full four-bay canopy, not a small door awning.
    // The outer columns stay at the ends so every ambulance can open up under
    // cover and the middle trauma-door approach remains clear.
    add("hospital emergency arrival canopy roof cap", 92.0f, -23.0f, 6.0f,
        64.8f, 0.22f, 10.9f, StartFinish::RedTrim, true);
    add("hospital emergency arrival canopy front fascia", 92.0f, -28.38f,
        5.52f, 64.8f, 0.48f, 0.24f, StartFinish::RedTrim, true);
    add("hospital emergency arrival canopy west fascia", 59.72f, -23.0f,
        5.52f, 0.24f, 0.48f, 10.55f, StartFinish::RedTrim, true);
    add("hospital emergency arrival canopy east fascia", 124.28f, -23.0f,
        5.52f, 0.24f, 0.48f, 10.55f, StartFinish::RedTrim, true);
    for (float x : {64.0f, 74.0f, 84.0f, 100.0f, 110.0f, 120.0f}) {
        add("hospital emergency arrival canopy underside rib", x, -23.0f,
            5.38f, 0.13f, 0.12f, 10.1f, StartFinish::Steel);
    }
    for (float x : {68.0f, 78.0f, 88.0f, 96.0f, 106.0f, 116.0f}) {
        add_canopy_light(x, -23.8f, 5.30f);
    }

    // A divided trauma-door face adds close-range hardware while preserving
    // the existing collision opening and a continuous 6.8 m clear landing.
    for (float x : {88.72f, 90.95f, 93.05f, 95.28f}) {
        add("hospital emergency arrival door mullion", x, -18.11f, 0.25f,
            0.15f, 4.10f, 0.15f, StartFinish::Steel);
    }
    add("hospital emergency arrival door transom", 92.0f, -18.11f, 3.45f,
        6.45f, 0.18f, 0.15f, StartFinish::Steel);
    for (float x : {90.0f, 94.0f}) {
        add("hospital emergency arrival trauma door glass", x, -18.13f,
            0.26f, 1.85f, 3.15f, 0.05f, StartFinish::Glass);
        add("hospital emergency arrival trauma door kick plate", x,
            -18.17f, 0.28f, 1.65f, 0.34f, 0.035f,
            StartFinish::Steel);
    }

    // Four full-size perpendicular receiving bays. They fit the longest
    // ambulance mesh, rear-door work space, and a continuous bypass at the
    // Tenth Street edge.
    for (float x : {68.5f, 82.5f, 96.5f, 110.5f}) {
        for (float side : {-2.0f, 2.0f}) {
            add("hospital emergency ambulance bay boundary", x + side,
                -23.2f, 0.205f, 0.14f, 0.025f, 9.6f,
                StartFinish::White);
        }
        add("hospital emergency ambulance bay stop bar", x, -18.55f,
            0.205f, 4.0f, 0.025f, 0.18f, StartFinish::Yellow);
    }

    // The asphalt lane and two flush throats finally make the drop-off read as
    // a one-way ambulance loop instead of parking paint beside the street.
    add("hospital emergency ambulance loop lot", 92.0f, -27.1f, 0.10f,
        88.0f, 0.10f, 6.0f, StartFinish::Asphalt);
    add("hospital emergency ambulance entrance throat", 48.0f, -25.2f,
        0.12f, 6.0f, 0.06f, 9.8f, StartFinish::Concrete);
    add("hospital emergency ambulance exit throat", 136.0f, -25.2f,
        0.12f, 6.0f, 0.06f, 9.8f, StartFinish::Concrete);
    const auto add_east_arrow = [&](float x, float z) {
        add("hospital emergency ambulance direction arrow", x, z, 0.215f,
            2.4f, 0.025f, 0.18f, StartFinish::White);
        StartPart upper{"hospital emergency ambulance direction arrow",
                        {x + 0.9f, z - 0.42f}, 0.215f, 1.2f, 0.025f, 0.18f,
                        StartFinish::White, false};
        upper.yaw_deg = 42.0f;
        out.push_back(upper);
        StartPart lower{"hospital emergency ambulance direction arrow",
                        {x + 0.9f, z + 0.42f}, 0.215f, 1.2f, 0.025f, 0.18f,
                        StartFinish::White, false};
        lower.yaw_deg = -42.0f;
        out.push_back(lower);
    };
    add_east_arrow(55.0f, -28.0f);
    add_east_arrow(127.0f, -28.0f);

    // Wall-mounted equipment stays behind the gurney line. Bay 2 owns the
    // sole 2:3 fitted image receiver; the other three faces stay material-only.
    for (float x : {80.0f, 84.5f, 99.5f, 104.0f}) {
        add("hospital emergency shore power cabinet body", x, -18.18f,
            0.42f, 0.74f, 1.20f, 0.34f, StartFinish::TealDoor, true);
    }
    add("hospital arrival emergency shore power cabinet fitted face", 84.5f,
        -18.37f, 0.57f, 0.60f, 0.90f, 0.03f, StartFinish::White);
    for (float x : {80.0f, 99.5f, 104.0f}) {
        add("hospital emergency shore power cabinet blank face", x, -18.37f,
            0.57f, 0.60f, 0.90f, 0.03f, StartFinish::Steel);
    }

    for (float x : {76.0f, 108.0f}) {
        add("hospital emergency stretcher cabinet body", x, -18.22f, 0.22f,
            1.20f, 1.52f, 0.46f, StartFinish::Steel, true);
        add("hospital emergency stretcher cabinet door", x, -18.48f, 0.32f,
            1.02f, 1.28f, 0.04f, StartFinish::WarmWall);
        add("hospital emergency stretcher cabinet handle", x + 0.35f,
            -18.52f, 0.82f, 0.07f, 0.38f, 0.04f,
            StartFinish::DarkRoof);
    }
    for (float x : {76.0f, 78.0f, 106.0f, 108.0f}) {
        add_bollard("hospital emergency equipment crash bollard", x, -19.15f);
    }
    for (float x : {80.0f, 88.0f, 96.0f, 104.0f}) {
        add("hospital emergency arrival trench drain", x, -26.62f, 0.205f,
            5.0f, 0.025f, 0.32f, StartFinish::DarkRoof);
    }

    // Wall-pack bodies are fixtures; only their separate lenses receive the
    // exact light-routing name requested by the parent.
    for (float x : {79.0f, 85.0f, 99.0f, 105.0f}) {
        add("hospital emergency arrival wall light body", x, -18.14f, 3.62f,
            0.48f, 0.36f, 0.20f, StartFinish::Steel);
        add("hospital arrival canopy light lens", x, -18.27f, 3.70f, 0.32f,
            0.17f, 0.05f, StartFinish::White);
    }

    return out;
}

}  // namespace apricot::city
