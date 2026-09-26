#pragma once

#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Logistics geometry is authored entirely in kHospitalSite local space.
// These envelopes are public so integration tests can pin the deliberately
// separate emergency and service routes without duplicating magic numbers.
inline constexpr float kHospitalAmbulanceLoopMinX = 204.0f;
inline constexpr float kHospitalAmbulanceLoopMaxX = 228.0f;
inline constexpr float kHospitalAmbulanceLoopMinZ = 38.0f;
inline constexpr float kHospitalAmbulanceLoopMaxZ = 128.0f;
inline constexpr float kHospitalAmbulanceBypassMinX = 220.5f;
inline constexpr float kHospitalAmbulanceBypassMaxX = 227.5f;
inline constexpr float kHospitalAmbulanceBypassMinZ = 44.0f;
inline constexpr float kHospitalAmbulanceBypassMaxZ = 122.0f;
inline constexpr float kHospitalServiceYardMinX = 126.0f;
inline constexpr float kHospitalServiceYardMaxX = 220.5f;
inline constexpr float kHospitalServiceYardMinZ = 144.0f;
inline constexpr float kHospitalServiceYardMaxZ = 206.0f;
inline constexpr float kHospitalServiceTruckSweepMinX = 148.0f;
inline constexpr float kHospitalServiceTruckSweepMaxX = 199.0f;
inline constexpr float kHospitalServiceTruckSweepMinZ = 148.0f;
inline constexpr float kHospitalServiceTruckSweepMaxZ = 203.0f;
inline constexpr float kHospitalEmergencyServiceClearGapM =
    kHospitalServiceYardMinZ - kHospitalAmbulanceLoopMaxZ;

inline std::vector<StartPart> bake_hospital_overhaul_logistics() {
    std::vector<StartPart> out;
    out.reserve(280);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };

    const auto add_bollard = [&](const char* name, float x, float z,
                                 StartFinish finish = StartFinish::Yellow) {
        add(name, x, z, 0.16f, 0.28f, 1.02f, 0.28f, finish, true);
        add("hospital logistics bollard cap", x, z, 1.18f, 0.34f, 0.10f,
            0.34f, StartFinish::Steel);
    };

    const auto add_south_arrow = [&](const char* name, float x, float z) {
        add(name, x, z, 0.215f, 0.20f, 0.025f, 2.5f,
            StartFinish::White);
        add(name, x - 0.42f, z + 0.88f, 0.216f, 0.18f, 0.025f, 1.25f,
            StartFinish::White, false, -42.0f);
        add(name, x + 0.42f, z + 0.88f, 0.216f, 0.18f, 0.025f, 1.25f,
            StartFinish::White, false, 42.0f);
    };

    const auto add_north_arrow = [&](const char* name, float x, float z) {
        add(name, x, z, 0.205f, 0.20f, 0.025f, 2.5f,
            StartFinish::White);
        add(name, x - 0.42f, z - 0.88f, 0.206f, 0.18f, 0.025f, 1.25f,
            StartFinish::White, false, 42.0f);
        add(name, x + 0.42f, z - 0.88f, 0.206f, 0.18f, 0.025f, 1.25f,
            StartFinish::White, false, -42.0f);
    };

    // ------------------------------------------------------------------
    // Juniper-side emergency receiving
    // ------------------------------------------------------------------

    // The four bays are a hardstand beside the east ED bar. Ambulances enter
    // at the north throat, travel south in the outer bypass, reverse west into
    // a selected bay, then return to the bypass and leave at the south throat.
    // Neither throat continues into the service court.
    add("hospital overhaul ambulance bay hardstand", 212.0f, 79.0f, 0.10f,
        16.0f, 0.10f, 78.0f, StartFinish::Concrete);
    add("hospital overhaul ambulance bypass lane", 224.0f, 83.0f, 0.10f,
        7.0f, 0.10f, 78.0f, StartFinish::Asphalt);
    add("hospital overhaul ambulance north turn apron", 221.0f, 42.0f,
        0.10f, 13.0f, 0.10f, 8.0f, StartFinish::Asphalt);
    add("hospital overhaul ambulance south turn apron", 221.0f, 124.0f,
        0.10f, 13.0f, 0.10f, 8.0f, StartFinish::Asphalt);
    add("hospital overhaul ambulance entry throat", 225.0f, 42.0f, 0.11f,
        10.0f, 0.08f, 7.2f, StartFinish::Concrete);
    add("hospital overhaul ambulance exit throat", 225.0f, 124.0f, 0.11f,
        10.0f, 0.08f, 7.2f, StartFinish::Concrete);

    // A long canopy covers the full 12.5 m vehicle depth plus the protected
    // rear-door apron. Front columns sit on bay divider lines, west of the
    // bypass, so the 7 m moving lane remains completely unobstructed.
    add("hospital overhaul emergency canopy roof", 212.1f, 79.0f, 5.70f,
        16.2f, 0.28f, 74.0f, StartFinish::RedTrim, true);
    add("hospital overhaul emergency canopy east fascia", 220.12f, 79.0f,
        5.24f, 0.24f, 0.46f, 74.0f, StartFinish::RedTrim, true);
    add("hospital overhaul emergency canopy north fascia", 212.1f, 42.12f,
        5.24f, 16.2f, 0.46f, 0.24f, StartFinish::RedTrim, true);
    add("hospital overhaul emergency canopy south fascia", 212.1f, 115.88f,
        5.24f, 16.2f, 0.46f, 0.24f, StartFinish::RedTrim, true);
    for (float z : {42.6f, 61.0f, 79.0f, 97.0f, 115.4f}) {
        add("hospital overhaul emergency canopy column", 219.7f, z, 0.16f,
            0.42f, 5.08f, 0.42f, StartFinish::Steel, true);
        add("hospital overhaul emergency canopy column guard", 219.15f, z,
            0.16f, 0.26f, 0.86f, 0.26f, StartFinish::Yellow, true);
    }
    for (float z : {48.0f, 58.0f, 68.0f, 78.0f, 88.0f, 98.0f, 108.0f}) {
        add("hospital overhaul emergency canopy underside rib", 212.1f, z,
            5.50f, 15.3f, 0.12f, 0.14f, StartFinish::Steel);
        // Keep this exact name. Parent integration already turns every named
        // lens into a real downward light rather than emissive-only geometry.
        add("hospital arrival canopy light lens", 214.0f, z, 5.39f, 0.46f,
            0.05f, 0.46f, StartFinish::White);
        add("hospital arrival canopy light lens", 218.0f, z, 5.39f, 0.46f,
            0.05f, 0.46f, StartFinish::White);
    }

    // Four 4.8 x 12.5 m positions fit the municipal ambulance with door work
    // space. Paint remains flush; low wheel stops are intentionally omitted
    // from the gurney end of each bay.
    constexpr float kBayCentresZ[] = {52.0f, 70.0f, 88.0f, 106.0f};
    for (float z : kBayCentresZ) {
        for (float side : {-2.4f, 2.4f}) {
            add("hospital overhaul ambulance bay boundary", 213.25f,
                z + side, 0.205f, 12.5f, 0.025f, 0.15f,
                StartFinish::White);
        }
        add("hospital overhaul ambulance bay stop bar", 207.08f, z,
            0.205f, 0.18f, 0.025f, 4.5f, StartFinish::Yellow);
        add("hospital overhaul ambulance bay drain", 218.75f, z, 0.205f,
            0.24f, 0.025f, 4.4f, StartFinish::Steel);
    }
    add("hospital overhaul ambulance bypass trench drain", 220.55f, 79.0f,
        0.205f, 0.22f, 0.025f, 72.0f, StartFinish::Steel);
    add_south_arrow("hospital overhaul ambulance direction arrow", 224.0f,
                    58.0f);
    add_south_arrow("hospital overhaul ambulance direction arrow", 224.0f,
                    91.0f);
    add_south_arrow("hospital overhaul ambulance direction arrow", 224.0f,
                    113.0f);
    add("hospital overhaul ambulance exit stop bar", 224.0f, 121.0f,
        0.215f, 6.0f, 0.025f, 0.22f, StartFinish::White);

    // The trauma vestibule is an east-facing, two-leaf opening target. These
    // leaves are visual and non-solid; the massing layer must leave the actual
    // wall opening behind them. A 3.6 m flush apron runs straight to Bays 2/3.
    add("hospital overhaul trauma approach apron", 208.0f, 79.0f, 0.11f,
        8.0f, 0.08f, 3.6f, StartFinish::Concrete);
    for (float z : {77.75f, 80.25f}) {
        add("hospital overhaul trauma sliding door glass", 204.13f, z,
            0.24f, 0.05f, 3.20f, 2.30f, StartFinish::Glass);
        add("hospital overhaul trauma sliding door kick plate", 204.18f, z,
            0.28f, 0.035f, 0.34f, 2.08f, StartFinish::Steel);
    }
    add("hospital overhaul trauma door transom", 204.10f, 79.0f, 3.44f,
        0.16f, 0.18f, 5.0f, StartFinish::Steel);
    for (float z : {73.8f, 84.2f}) {
        add_bollard("hospital overhaul trauma crash bollard", 205.65f, z);
    }

    // One fitted bay marker and one fitted shore-power face reuse the exact
    // existing runtime receiver names. The other bay/equipment faces are
    // ordinary authored materials, avoiding repeated generated art.
    add("hospital emergency bay-two marker backing", 204.04f, 70.0f, 3.72f,
        0.12f, 1.34f, 1.34f, StartFinish::Steel, true);
    add("hospital emergency bay-two marker face", 204.13f, 70.0f, 3.78f,
        0.05f, 1.20f, 1.20f, StartFinish::White);
    for (float z : {56.0f, 70.0f, 92.0f, 106.0f}) {
        add("hospital overhaul emergency shore power cabinet body", 204.45f,
            z, 0.42f, 0.72f, 1.22f, 0.76f, StartFinish::TealDoor, true);
    }
    add("hospital arrival emergency shore power cabinet fitted face",
        204.83f, 70.0f, 0.58f, 0.03f, 0.90f, 0.60f,
        StartFinish::White);
    for (float z : {56.0f, 92.0f, 106.0f}) {
        add("hospital overhaul emergency shore power cabinet blank face",
            204.83f, z, 0.58f, 0.03f, 0.90f, 0.60f,
            StartFinish::Steel);
    }

    // Wall stores and a parked transfer trolley make the receiving area read
    // operationally. They sit beyond the trauma-door maneuver rectangle.
    for (float z : {47.0f, 113.0f}) {
        add("hospital overhaul emergency stretcher cabinet body", 204.48f, z,
            0.22f, 0.78f, 1.58f, 1.35f, StartFinish::Steel, true);
        add("hospital overhaul emergency stretcher cabinet door", 204.90f, z,
            0.32f, 0.04f, 1.30f, 1.16f, StartFinish::WarmWall);
        add_bollard("hospital overhaul emergency equipment crash bollard",
                    205.55f, z - 1.15f);
        add_bollard("hospital overhaul emergency equipment crash bollard",
                    205.55f, z + 1.15f);
    }
    add("hospital overhaul parked stretcher mattress", 208.5f, 112.6f,
        1.05f, 2.20f, 0.18f, 0.72f, StartFinish::WarmWall);
    add("hospital overhaul parked stretcher frame", 208.5f, 112.6f, 0.72f,
        2.05f, 0.12f, 0.62f, StartFinish::Steel);
    for (float x : {207.72f, 209.28f}) {
        for (float z : {112.32f, 112.88f}) {
            add("hospital overhaul parked stretcher caster", x, z, 0.34f,
                0.18f, 0.38f, 0.18f, StartFinish::DarkRoof);
        }
    }

    // A real solid separator closes the tempting gap between the ambulance
    // exit and service yard. It leaves a sixteen-metre open safety buffer in Z
    // and keeps both operations visually distinct from Juniper.
    add("hospital overhaul emergency service separation wall", 214.0f,
        140.8f, 0.16f, 13.0f, 2.25f, 0.36f, StartFinish::WarmWall, true);
    add("hospital overhaul emergency service separation cap", 214.0f,
        140.8f, 2.41f, 13.3f, 0.14f, 0.52f, StartFinish::TealDoor);
    add("hospital overhaul emergency service buffer soil", 214.0f, 135.2f,
        0.11f, 13.0f, 0.08f, 7.6f, StartFinish::DarkRoof);
    for (float x : {210.5f, 214.0f, 217.5f}) {
        add("hospital overhaul emergency service buffer planting", x, 135.2f,
            0.19f, 1.65f, 0.72f, 4.8f, StartFinish::TealDoor);
    }

    // ------------------------------------------------------------------
    // Southeast service and plant yard
    // ------------------------------------------------------------------

    // The yard has one controlled Sixth Street throat. Its centre remains an
    // empty 51 x 55 m truck-sweep box; all solid plant, waste, and utility
    // fixtures hug the perimeter outside that envelope.
    add("hospital overhaul service yard hardstand", 174.0f, 175.0f, 0.10f,
        90.0f, 0.10f, 62.0f, StartFinish::Asphalt);
    add("hospital overhaul service yard Sixth Street throat", 184.0f, 210.0f,
        0.11f, 8.0f, 0.08f, 10.0f, StartFinish::Concrete);
    add("hospital overhaul service gate stop bar", 184.0f, 204.4f, 0.215f,
        7.2f, 0.025f, 0.24f, StartFinish::White);
    add_south_arrow("hospital overhaul service outbound arrow", 188.0f,
                    200.0f);
    add("hospital overhaul service inbound arrow shaft", 180.0f, 200.0f,
        0.215f, 0.20f, 0.025f, 2.5f, StartFinish::White);
    add("hospital overhaul service inbound arrow head", 179.58f, 199.12f,
        0.216f, 0.18f, 0.025f, 1.25f, StartFinish::White, false, 42.0f);
    add("hospital overhaul service inbound arrow head", 180.42f, 199.12f,
        0.216f, 0.18f, 0.025f, 1.25f, StartFinish::White, false, -42.0f);

    // Painted corner ticks communicate the box-truck sweep without putting a
    // curb, pallet, post, or invisible collider inside it.
    for (float x : {151.0f, 161.0f, 171.0f, 181.0f, 191.0f, 196.0f}) {
        add("hospital overhaul box truck sweep marking", x, 202.35f, 0.215f,
            4.0f, 0.025f, 0.16f, StartFinish::Yellow);
    }
    for (float z : {153.0f, 163.0f, 173.0f, 183.0f, 193.0f}) {
        add("hospital overhaul box truck sweep marking", 148.35f, z, 0.215f,
            0.16f, 0.025f, 4.0f, StartFinish::Yellow);
    }

    // Flush wheel guides show the three northbound reverse approaches to the
    // docks. They sit inside the truck-sweep box as paint only, leaving the
    // full maneuver envelope free of posts, curbs, and collision geometry.
    for (float x : {158.0f, 174.0f, 190.0f}) {
        for (float side : {-1.05f, 1.05f}) {
            add("hospital overhaul loading dock reverse guide", x + side,
                153.0f, 0.215f, 0.10f, 0.025f, 10.0f,
                StartFinish::Yellow);
        }
    }

    // Three raised docks meet the south face of the surgery/support bar.
    // Dock shelters are wall-supported, so no canopy column lands in a truck
    // reverse envelope. Doors are visual opening targets for the massing pass.
    for (float x : {158.0f, 174.0f, 190.0f}) {
        add("hospital overhaul service loading dock platform", x, 140.1f,
            0.16f, 7.0f, 1.10f, 3.8f, StartFinish::Concrete, true);
        add("hospital overhaul service loading dock edge stripe", x, 142.02f,
            1.26f, 6.7f, 0.14f, 0.16f, StartFinish::Yellow);
        add("hospital overhaul service loading door", x, 138.18f, 1.26f,
            5.2f, 3.7f, 0.06f, StartFinish::TealDoor);
        add("hospital overhaul service dock shelter head", x, 138.40f,
            4.65f, 5.6f, 0.44f, 0.52f, StartFinish::DarkRoof, true);
        for (float side : {-2.85f, 2.85f}) {
            add("hospital overhaul service dock shelter side", x + side,
                139.65f, 1.26f, 0.34f, 3.55f, 2.95f,
                StartFinish::DarkRoof, true);
            add("hospital overhaul service dock bumper", x + side * 0.62f,
                142.10f, 0.48f, 0.72f, 0.62f, 0.34f,
                StartFinish::DarkRoof, true);
        }
        add("hospital overhaul service dock trench drain", x, 143.05f,
            0.205f, 7.0f, 0.025f, 0.24f, StartFinish::Steel);
        add("hospital arrival canopy light lens", x, 140.20f, 4.72f,
            0.70f, 0.05f, 0.34f, StartFinish::White);
    }

    // Staff receiving is west of the loading docks, with its own protected
    // walk and door. It never opens into a reversing bay or waste compound.
    add("hospital overhaul staff receiving walk", 135.0f, 149.0f, 0.11f,
        3.0f, 0.08f, 18.0f, StartFinish::Concrete);
    add("hospital overhaul staff receiving door", 135.0f, 138.18f, 0.24f,
        3.2f, 3.15f, 0.06f, StartFinish::TealDoor);
    add("hospital overhaul staff receiving canopy", 135.0f, 141.0f, 4.05f,
        8.0f, 0.24f, 5.8f, StartFinish::WarmWall, true);
    add("hospital arrival canopy light lens", 135.0f, 141.6f, 3.92f,
        0.58f, 0.05f, 0.40f, StartFinish::White);
    // Edge lines and northbound arrows keep the separate staff route legible
    // through the yard without introducing any raised pedestrian barriers.
    for (float x : {133.65f, 136.35f}) {
        add("hospital overhaul staff receiving walk edge line", x, 149.0f,
            0.205f, 0.08f, 0.025f, 17.4f, StartFinish::Yellow);
    }
    add_north_arrow("hospital overhaul staff receiving walk direction arrow",
                    135.0f, 146.5f);
    add_north_arrow("hospital overhaul staff receiving walk direction arrow",
                    135.0f, 154.0f);
    for (float x : {131.8f, 138.2f}) {
        add_bollard("hospital overhaul staff receiving crash bollard", x,
                    143.7f, StartFinish::Steel);
    }

    // Medical oxygen gets an open, independently locked cage on the east
    // edge. It is more than five metres from loading doors and far from waste,
    // generator fuel, and the ambulance route.
    add("hospital overhaul oxygen compound slab", 211.5f, 163.0f, 0.11f,
        13.0f, 0.10f, 15.0f, StartFinish::Concrete);
    for (float x : {205.1f, 217.9f}) {
        add("hospital overhaul oxygen cage fence", x, 163.0f, 0.21f,
            0.22f, 2.35f, 15.0f, StartFinish::Steel, true);
    }
    add("hospital overhaul oxygen cage north fence", 211.5f, 155.6f, 0.21f,
        13.0f, 2.35f, 0.22f, StartFinish::Steel, true);
    add("hospital overhaul oxygen cage south fence west", 207.2f, 170.4f,
        0.21f, 4.4f, 2.35f, 0.22f, StartFinish::Steel, true);
    add("hospital overhaul oxygen cage south fence east", 216.0f, 170.4f,
        0.21f, 3.8f, 2.35f, 0.22f, StartFinish::Steel, true);
    add("hospital overhaul oxygen cage open gate leaf", 210.15f, 171.55f,
        0.21f, 0.18f, 2.30f, 3.4f, StartFinish::Steel, false, 48.0f);
    add("hospital overhaul oxygen cage open gate leaf", 213.1f, 171.55f,
        0.21f, 0.18f, 2.30f, 3.4f, StartFinish::Steel, false, -48.0f);
    add("hospital overhaul oxygen bulk vessel", 209.0f, 162.3f, 0.21f,
        2.2f, 4.8f, 2.2f, StartFinish::White, true);
    add("hospital overhaul oxygen vessel cap", 209.0f, 162.3f, 5.01f,
        1.45f, 0.35f, 1.45f, StartFinish::White);
    add("hospital overhaul oxygen manifold cabinet", 214.5f, 160.8f, 0.21f,
        1.5f, 2.0f, 0.72f, StartFinish::TealDoor, true);
    for (float z : {164.5f, 166.5f}) {
        for (float x : {213.2f, 215.0f}) {
            add("hospital overhaul oxygen reserve cylinder", x, z, 0.21f,
                0.52f, 1.85f, 0.52f, StartFinish::White, true);
        }
    }
    add("hospital overhaul oxygen warning placard", 211.5f, 170.28f, 1.05f,
        2.4f, 1.2f, 0.04f, StartFinish::Yellow);
    for (float x : {204.0f, 219.0f}) {
        for (float z : {155.0f, 171.0f}) {
            add_bollard("hospital overhaul oxygen crash bollard", x, z);
        }
    }

    // Generator and plant gear stay against the screened west edge, outside
    // the marked truck sweep and remote from the oxygen compound.
    add("hospital overhaul generator housekeeping slab", 135.0f, 180.0f,
        0.11f, 10.0f, 0.16f, 13.0f, StartFinish::Concrete, true);
    add("hospital overhaul standby generator body", 135.0f, 179.0f, 0.27f,
        3.4f, 2.55f, 6.8f, StartFinish::TealDoor, true);
    add("hospital overhaul standby generator top", 135.0f, 179.0f, 2.82f,
        3.6f, 0.22f, 7.0f, StartFinish::DarkRoof, true);
    for (float z : {177.2f, 178.4f, 179.6f, 180.8f}) {
        add("hospital overhaul standby generator louver", 136.76f, z, 1.0f,
            0.04f, 0.18f, 0.84f, StartFinish::Steel);
    }
    add("hospital overhaul generator exhaust stack", 134.1f, 176.2f, 3.0f,
        0.34f, 2.4f, 0.34f, StartFinish::Steel, true);
    add("hospital overhaul plant switchgear", 135.0f, 185.8f, 0.27f,
        4.8f, 2.2f, 0.82f, StartFinish::Steel, true);
    for (float x : {130.2f, 139.8f}) {
        add_bollard("hospital overhaul generator crash bollard", x, 181.0f);
    }

    // Waste occupies the far southeast corner. Three opaque walls shield it
    // from Juniper and Sixth; the open west gate faces the service court and
    // stays clear of the staff receiving route.
    add("hospital overhaul waste enclosure washable slab", 210.0f, 194.0f,
        0.11f, 15.0f, 0.12f, 18.0f, StartFinish::Concrete);
    add("hospital overhaul waste enclosure east wall", 217.35f, 194.0f,
        0.23f, 0.30f, 2.45f, 18.0f, StartFinish::WarmWall, true);
    add("hospital overhaul waste enclosure north wall", 210.0f, 185.15f,
        0.23f, 15.0f, 2.45f, 0.30f, StartFinish::WarmWall, true);
    add("hospital overhaul waste enclosure south wall", 210.0f, 202.85f,
        0.23f, 15.0f, 2.45f, 0.30f, StartFinish::WarmWall, true);
    add("hospital overhaul waste enclosure open gate leaf", 202.9f, 190.5f,
        0.23f, 0.26f, 2.35f, 6.0f, StartFinish::Steel, false, -58.0f);
    add("hospital overhaul waste enclosure open gate leaf", 202.9f, 197.5f,
        0.23f, 0.26f, 2.35f, 6.0f, StartFinish::Steel, false, 58.0f);
    add("hospital overhaul clinical waste container", 212.8f, 189.0f,
        0.35f, 3.2f, 1.65f, 2.1f, StartFinish::Yellow, true);
    add("hospital overhaul general waste container", 212.8f, 194.0f,
        0.35f, 3.2f, 1.65f, 2.1f, StartFinish::DarkRoof, true);
    add("hospital overhaul cardboard container", 212.8f, 199.0f, 0.35f,
        3.2f, 1.65f, 2.1f, StartFinish::TealDoor, true);
    // Colored flush pads reserve a clear west-side stance for servicing each
    // bin from the open gate, without putting carts or people in a vehicle
    // route. Each pad stops at the container face and stays inside the walls.
    add("hospital overhaul clinical waste service stance", 210.45f, 189.0f,
        0.235f, 1.5f, 0.025f, 2.6f, StartFinish::Yellow);
    add("hospital overhaul general waste service stance", 210.45f, 194.0f,
        0.235f, 1.5f, 0.025f, 2.6f, StartFinish::DarkRoof);
    add("hospital overhaul cardboard service stance", 210.45f, 199.0f,
        0.235f, 1.5f, 0.025f, 2.6f, StartFinish::TealDoor);
    add("hospital overhaul waste hose cabinet", 216.95f, 199.8f, 0.55f,
        0.42f, 1.1f, 0.72f, StartFinish::Steel, true);
    add("hospital overhaul waste enclosure drain", 208.0f, 200.8f, 0.245f,
        4.0f, 0.025f, 0.22f, StartFinish::Steel);
    for (float z : {187.0f, 201.0f}) {
        add_bollard("hospital overhaul waste enclosure crash bollard", 201.5f,
                    z);
    }

    // Masonry and open steel screening hide operations from the garage and
    // perimeter streets without walling off the only emergency exits.
    add("hospital overhaul service west screen wall", 126.2f, 177.0f, 0.16f,
        0.38f, 2.45f, 58.0f, StartFinish::WarmWall, true);
    add("hospital overhaul service east screen wall north", 220.2f, 181.0f,
        0.16f, 0.38f, 2.45f, 18.0f, StartFinish::WarmWall, true);
    add("hospital overhaul service east screen wall south", 220.2f, 201.0f,
        0.16f, 0.38f, 2.45f, 14.0f, StartFinish::WarmWall, true);
    for (float z : {151.0f, 177.0f, 203.0f}) {
        add("hospital overhaul service screen pier", 126.2f, z, 0.16f,
            0.62f, 2.85f, 0.62f, StartFinish::Concrete, true);
    }
    for (float x : {135.0f, 158.0f, 181.0f, 204.0f}) {
        add("hospital overhaul service yard wall light body", x, 144.05f,
            4.2f, 0.65f, 0.42f, 0.24f, StartFinish::Steel);
        add("hospital arrival canopy light lens", x, 144.18f, 4.28f,
            0.48f, 0.18f, 0.05f, StartFinish::White);
    }

    return out;
}

}  // namespace apricot::city
