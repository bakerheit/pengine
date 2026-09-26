#pragma once

#include <initializer_list>
#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Compact outpatient ultrasound and preparation bay. The furnishings stay
// inside the assigned east diagnostics room; all floor and ceiling finishes
// are owned by hospital_detail_surfaces.h.
inline std::vector<StartPart> bake_hospital_detail_diagnostics() {
    std::vector<StartPart> out;
    out.reserve(96);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };

    // A low partition gives the department marker a real, floor-supported
    // backing. Six-metre openings remain clear on both ends.
    add("hospital detail diagnostics sign backing partition tex wallpaint",
        80.0f, 30.55f, 0.315f, 8.0f, 2.65f, 0.16f, StartFinish::White, true);
    add("hospital detail diagnostics department sign tex diagnostics-sign",
        80.0f, 30.46f, 2.08f, 3.20f, 0.80f, 0.025f,
        StartFinish::White);

    // A 2.15 m exam couch faces the ultrasound cart. Its solid frame carries
    // collision while the padding, controls and four casters stay distinct.
    add("hospital detail diagnostics exam couch chassis tex steel", 77.5f,
        25.30f, 0.435f, 1.04f, 0.195f, 2.18f, StartFinish::White, true);
    add("hospital detail diagnostics exam couch lift column tex steel",
        77.5f, 25.30f, 0.35f, 0.42f, 0.22f, 0.78f, StartFinish::White,
        true);
    add("hospital detail diagnostics exam couch mattress tex upholstery",
        77.5f, 25.30f, 0.63f, 0.94f, 0.18f, 2.08f, StartFinish::White);
    add("hospital detail diagnostics exam couch head cushion tex upholstery",
        77.5f, 24.52f, 0.79f, 0.62f, 0.10f, 0.38f, StartFinish::White);
    for (const float side : {-0.55f, 0.55f}) {
        add("hospital detail diagnostics exam couch side rail tex steel",
            77.5f + side, 25.30f, 0.76f, 0.055f, 0.18f, 1.52f,
            StartFinish::White);
    }
    for (const float dx : {-0.40f, 0.40f}) {
        for (const float dz : {-0.82f, 0.82f}) {
            add("hospital detail diagnostics exam couch caster fork", 77.5f + dx,
                25.30f + dz, 0.315f, 0.09f, 0.12f, 0.10f,
                StartFinish::Steel);
            add("hospital detail diagnostics exam couch caster wheel",
                77.5f + dx, 25.30f + dz, 0.315f, 0.12f, 0.09f, 0.08f,
                StartFinish::Steel);
        }
    }
    add("hospital detail diagnostics couch control pendant holder", 78.12f,
        24.68f, 0.87f, 0.12f, 0.10f, 0.18f, StartFinish::Steel);
    add("hospital detail diagnostics couch hand control", 78.12f, 24.63f,
        0.88f, 0.12f, 0.24f, 0.065f, StartFinish::TealDoor);
    for (const float z : {24.57f, 24.65f, 24.73f}) {
        add("hospital detail diagnostics couch control button", 78.12f, z,
            1.02f, 0.035f, 0.025f, 0.035f, StartFinish::White);
    }
    add("hospital detail diagnostics couch pendant lead", 78.02f, 24.82f,
        0.82f, 0.025f, 0.025f, 0.28f, StartFinish::Steel, false, 18.0f);

    // The partial privacy curtain stands on two weighted feet and casters, so
    // the fabric and top rail do not float from an assumed ceiling anchor.
    const float curtain_x = 74.85f;
    for (const float z : {24.0f, 26.65f}) {
        add("hospital detail diagnostics curtain weighted foot tex steel",
            curtain_x, z, 0.315f, 0.30f, 0.08f, 0.38f,
            StartFinish::White, true);
        for (const float dx : {-0.10f, 0.10f}) {
            add("hospital detail diagnostics curtain caster", curtain_x + dx,
                z, 0.315f, 0.08f, 0.07f, 0.09f, StartFinish::Steel);
        }
        add("hospital detail diagnostics curtain upright tex steel", curtain_x,
            z, 0.39f, 0.055f, 2.05f, 0.055f, StartFinish::White);
    }
    add("hospital detail diagnostics curtain top rail tex steel", curtain_x,
        25.325f, 2.40f, 0.06f, 0.05f, 2.65f, StartFinish::White);
    add("hospital detail diagnostics privacy curtain north panel tex curtain",
        curtain_x, 24.77f, 0.43f, 0.035f, 1.95f, 1.48f,
        StartFinish::White);
    add("hospital detail diagnostics privacy curtain south panel tex curtain",
        curtain_x, 25.90f, 0.43f, 0.035f, 1.95f, 1.48f,
        StartFinish::White);
    add("hospital detail diagnostics curtain tie strap", curtain_x + 0.04f,
        25.90f, 1.42f, 0.055f, 0.09f, 0.34f, StartFinish::TealDoor);

    // A compact mobile ultrasound unit has a readable north-facing 4:3 CRT
    // scan, an operator shelf, controls, a docked probe and a short cable.
    const float cart_x = 82.0f;
    const float cart_z = 22.50f;
    for (const float dx : {-0.29f, 0.29f}) {
        for (const float dz : {-0.23f, 0.23f}) {
            add("hospital detail diagnostics ultrasound trolley caster fork",
                cart_x + dx, cart_z + dz, 0.315f, 0.09f, 0.10f, 0.09f,
                StartFinish::Steel);
            add("hospital detail diagnostics ultrasound trolley caster",
                cart_x + dx, cart_z + dz, 0.315f, 0.11f, 0.085f, 0.08f,
                StartFinish::Steel);
        }
    }
    add("hospital detail diagnostics ultrasound trolley chassis tex steel",
        cart_x, cart_z, 0.40f, 0.76f, 0.18f, 0.60f, StartFinish::White, true);
    add("hospital detail diagnostics ultrasound trolley lower shelf tex steel",
        cart_x, cart_z, 0.59f, 0.70f, 0.055f, 0.54f, StartFinish::White);
    add("hospital detail diagnostics ultrasound trolley mast tex steel", cart_x,
        cart_z, 0.62f, 0.12f, 0.86f, 0.12f, StartFinish::White, true);
    add("hospital detail diagnostics ultrasound trolley supply shelf tex steel",
        cart_x, cart_z, 0.96f, 0.74f, 0.055f, 0.55f, StartFinish::White);
    add("hospital detail diagnostics ultrasound monitor support arm tex steel",
        cart_x, cart_z + 0.08f, 1.38f, 0.08f, 0.10f, 0.34f,
        StartFinish::White);
    add("hospital detail diagnostics ultrasound monitor casing tex steel",
        cart_x, cart_z, 1.47f, 0.82f, 0.64f, 0.09f, StartFinish::White);
    add("hospital detail diagnostics ultrasound scan screen tex screen",
        cart_x, cart_z - 0.055f, 1.52f, 0.72f, 0.54f, 0.025f,
        StartFinish::White);
    add("hospital detail diagnostics ultrasound monitor lower trim", cart_x,
        cart_z - 0.06f, 1.43f, 0.82f, 0.035f, 0.035f,
        StartFinish::TealDoor);

    add("hospital detail diagnostics ultrasound operator shelf tex laminate",
        cart_x, cart_z - 0.25f, 1.04f, 0.76f, 0.055f, 0.36f,
        StartFinish::White);
    add("hospital detail diagnostics ultrasound operator control board tex steel",
        cart_x, cart_z - 0.21f, 1.10f, 0.62f, 0.035f, 0.25f,
        StartFinish::White);
    for (const float x : {81.77f, 81.92f, 82.07f, 82.22f}) {
        add("hospital detail diagnostics ultrasound console key", x,
            cart_z - 0.18f, 1.14f, 0.07f, 0.025f, 0.055f,
            StartFinish::White);
    }
    for (const float x : {81.78f, 82.20f}) {
        add("hospital detail diagnostics ultrasound console dial", x,
            cart_z - 0.32f, 1.14f, 0.095f, 0.065f, 0.095f,
            StartFinish::TealDoor);
    }
    add("hospital detail diagnostics ultrasound trackball", 82.0f,
        cart_z - 0.06f, 1.14f, 0.16f, 0.07f, 0.16f, StartFinish::Steel);
    add("hospital detail diagnostics ultrasound probe cradle", 81.57f,
        cart_z - 0.28f, 1.02f, 0.16f, 0.10f, 0.24f,
        StartFinish::Steel);
    add("hospital detail diagnostics ultrasound transducer probe", 81.57f,
        cart_z - 0.28f, 1.12f, 0.085f, 0.25f, 0.10f,
        StartFinish::White);
    add("hospital detail diagnostics ultrasound probe cable upper", 81.64f,
        cart_z - 0.13f, 1.10f, 0.035f, 0.035f, 0.28f,
        StartFinish::Steel, false, 24.0f);
    add("hospital detail diagnostics ultrasound gel bottle", 82.28f,
        cart_z - 0.34f, 1.015f, 0.13f, 0.22f, 0.12f, StartFinish::White);
    add("hospital detail diagnostics ultrasound gel cap", 82.28f,
        cart_z - 0.34f, 1.235f, 0.10f, 0.055f, 0.10f,
        StartFinish::TealDoor);
    add("hospital detail diagnostics ultrasound image film tray", 81.72f,
        cart_z - 0.32f, 0.99f, 0.28f, 0.025f, 0.15f,
        StartFinish::Steel);

    // The scrub sink and storage cabinet back directly onto a real partition.
    // Its north/south ends leave over 2.9 m of open passage.
    const float sink_wall_x = 88.82f;
    add("hospital detail diagnostics handwash backing partition tex wallpaint",
        sink_wall_x, 23.40f, 0.315f, 0.16f, 2.65f, 8.8f,
        StartFinish::White, true);
    add("hospital detail diagnostics handwash cabinet tex laminate", 88.40f,
        23.30f, 0.315f, 1.30f, 0.76f, 0.60f,
        StartFinish::White, true, 90.0f);
    add("hospital detail diagnostics handwash cabinet toe kick", 88.40f,
        23.30f, 0.315f, 1.18f, 0.12f, 0.51f,
        StartFinish::TealDoor, false, 90.0f);
    for (const float z : {22.96f, 23.64f}) {
        add("hospital detail diagnostics handwash cabinet door tex laminate",
            88.06f, z, 0.40f, 0.55f, 0.55f, 0.035f,
            StartFinish::White, false, 90.0f);
        add("hospital detail diagnostics handwash cabinet pull tex steel",
            88.025f, z, 0.66f, 0.20f, 0.035f, 0.035f,
            StartFinish::White, false, 90.0f);
    }
    add("hospital detail diagnostics sink worktop tex laminate", 88.40f,
        23.30f, 1.075f, 1.42f, 0.07f, 0.68f,
        StartFinish::White, false, 90.0f);
    add("hospital detail diagnostics inset scrub basin tex steel", 88.38f,
        23.30f, 1.13f, 0.64f, 0.07f, 0.40f,
        StartFinish::White, false, 90.0f);
    add("hospital detail diagnostics sink drain", 88.35f, 23.30f, 1.18f,
        0.12f, 0.015f, 0.12f, StartFinish::Steel, false, 90.0f);
    add("hospital detail diagnostics faucet base", 88.55f, 23.30f, 1.145f,
        0.11f, 0.06f, 0.11f, StartFinish::Steel);
    add("hospital detail diagnostics faucet riser", 88.55f, 23.30f, 1.205f,
        0.055f, 0.24f, 0.055f, StartFinish::Steel);
    add("hospital detail diagnostics faucet spout", 88.43f, 23.30f, 1.40f,
        0.06f, 0.055f, 0.24f, StartFinish::Steel, false, 90.0f);
    add("hospital detail diagnostics faucet lever", 88.55f, 23.48f, 1.42f,
        0.035f, 0.07f, 0.14f, StartFinish::Steel);

    // Wall-mounted dispensers and the handwashing reminder touch the
    // partition's west face; each has a visible plate and a backing surface.
    add("hospital detail diagnostics soap dispenser plate tex steel", 88.68f,
        24.30f, 1.48f, 0.30f, 0.42f, 0.10f,
        StartFinish::White, false, 90.0f);
    add("hospital detail diagnostics soap dispenser push bar", 88.62f,
        24.30f, 1.53f, 0.12f, 0.055f, 0.06f,
        StartFinish::TealDoor, false, 90.0f);
    add("hospital detail diagnostics towel dispenser plate tex steel",
        88.68f, 22.30f, 1.48f, 0.38f, 0.52f, 0.10f,
        StartFinish::White, false, 90.0f);
    add("hospital detail diagnostics towel dispenser outlet", 88.62f,
        22.30f, 1.32f, 0.20f, 0.045f, 0.07f,
        StartFinish::TealDoor, false, 90.0f);
    add("hospital detail diagnostics handwash instruction plate", 88.725f,
        23.28f, 2.10f, 0.48f, 0.55f, 0.025f,
        StartFinish::White, false, 90.0f);

    // Small prep supplies make the cabinet worktop read as an active station.
    add("hospital detail diagnostics sterile glove box", 88.36f, 22.86f,
        1.145f, 0.26f, 0.12f, 0.19f, StartFinish::WarmWall);
    add("hospital detail diagnostics gauze packet", 88.41f, 23.72f,
        1.145f, 0.22f, 0.045f, 0.15f, StartFinish::White);

    return out;
}

}  // namespace apricot::city
