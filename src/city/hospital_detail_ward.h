#pragma once

#include <initializer_list>
#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Four compact inpatient bays fit inside the west clinical wing. Each bed has
// its own headwall, bedside storage, visitor chair, IV stand and partial
// privacy curtain; the central floor stays open for staff circulation.
inline std::vector<StartPart> bake_hospital_detail_ward() {
    std::vector<StartPart> out;
    out.reserve(110);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };

    // A compact entry pier carries a legible fitted sign without closing
    // either approach. The shared surfaces module supplies the ward floor.
    add("hospital detail ward entry sign pier tex wallpaint", -7.2f, 96.1f,
        0.315f, 4.4f, 2.65f, 0.22f, StartFinish::White, true);
    add("hospital detail ward sign tex ward-sign", -7.2f, 96.0f, 1.92f,
        3.60f, 0.90f, 0.035f, StartFinish::White);

    constexpr float bed_z = 108.60f;
    constexpr float headwall_z = 106.65f;
    constexpr float bay_centres[] = {-6.25f, 3.25f, 12.75f, 22.25f};

    for (const float cx : bay_centres) {
        // A real 1.0 x 2.2 m mattress sits on a low steel frame. The rails
        // stay below shoulder height and the pillow rests at the head end.
        add("hospital detail ward bed frame tex steel", cx, bed_z, 0.40f,
            1.12f, 0.18f, 2.34f, StartFinish::White, true);
        for (const float dz : {-0.72f, 0.72f}) {
            add("hospital detail ward bed frame floor support tex steel", cx,
                bed_z + dz, 0.315f, 1.04f, 0.085f, 0.20f,
                StartFinish::White, true);
        }
        add("hospital detail ward mattress tex upholstery", cx, bed_z,
            0.58f, 1.0f, 0.20f, 2.20f, StartFinish::White);
        add("hospital detail ward pillow tex upholstery", cx, bed_z - 0.78f,
            0.78f, 0.66f, 0.11f, 0.38f, StartFinish::White);
        add("hospital detail ward bed side rail tex steel", cx - 0.59f,
            bed_z, 0.66f, 0.045f, 0.20f, 1.68f, StartFinish::White);

        // The freestanding headwall is part of each bay, so its medical gear
        // does not rely on shell walls at a guessed location.
        add("hospital detail ward headwall panel tex wallpaint", cx,
            headwall_z, 0.315f, 4.30f, 2.45f, 0.18f,
            StartFinish::White, true);
        add("hospital detail ward medical gas outlet", cx - 1.15f,
            headwall_z + 0.105f, 1.23f, 0.16f, 0.12f, 0.035f,
            StartFinish::TealDoor);
        add("hospital detail ward nurse call button", cx + 1.40f,
            headwall_z + 0.105f, 1.03f, 0.10f, 0.16f, 0.035f,
            StartFinish::Yellow);
        add("hospital detail ward bedside monitor casing tex steel",
            cx + 1.15f, headwall_z + 0.12f, 1.53f, 0.58f, 0.47f, 0.08f,
            StartFinish::White);
        add("hospital detail ward bedside monitor screen tex vitals-screen",
            cx + 1.15f, headwall_z + 0.168f, 1.58f, 0.48f, 0.36f, 0.025f,
            StartFinish::White, false, 180.0f);

        // The IV stand rests beside the bed. The hanging bag is kept distinct
        // from its steel pole so it reads clearly at normal play distance.
        add("hospital detail ward IV stand foot tex steel", cx + 0.92f,
            bed_z - 0.43f, 0.315f, 0.34f, 0.045f, 0.34f,
            StartFinish::White);
        add("hospital detail ward IV stand pole tex steel", cx + 0.92f,
            bed_z - 0.43f, 0.36f, 0.035f, 1.87f, 0.035f,
            StartFinish::White);
        add("hospital detail ward IV fluid bag", cx + 0.92f,
            bed_z - 0.43f, 1.99f, 0.19f, 0.29f, 0.09f,
            StartFinish::Glass);
        add("hospital detail ward IV hanger", cx + 0.92f,
            bed_z - 0.43f, 2.25f, 0.28f, 0.035f, 0.035f,
            StartFinish::Steel);

        // A low bedside cabinet, with one visible drawer and pull, sits to the
        // left of the mattress. Its body provides the collision base.
        const float cabinet_x = cx - 1.55f;
        const float cabinet_z = bed_z - 0.55f;
        add("hospital detail ward bedside cabinet tex laminate", cabinet_x,
            cabinet_z, 0.315f, 0.60f, 0.64f, 0.55f,
            StartFinish::White, true);
        add("hospital detail ward bedside cabinet drawer", cabinet_x + 0.3175f,
            cabinet_z, 0.70f, 0.035f, 0.18f, 0.48f,
            StartFinish::WarmWall);
        add("hospital detail ward bedside cabinet pull tex steel",
            cabinet_x + 0.32f, cabinet_z, 0.77f, 0.025f, 0.035f, 0.16f,
            StartFinish::White);

        // Each guest chair has a floor-supported pedestal, padded seat and a
        // short back. The 0.48 m seat datum is consistent across the hospital.
        const float chair_x = cx + 1.62f;
        add("hospital detail ward visitor chair pedestal tex steel", chair_x,
            bed_z, 0.315f, 0.13f, 0.34f, 0.13f,
            StartFinish::White, true);
        add("hospital detail ward visitor chair seat tex upholstery",
            chair_x, bed_z, 0.655f, 0.56f, 0.14f, 0.56f,
            StartFinish::White, true);
        add("hospital detail ward visitor chair back tex upholstery",
            chair_x + 0.24f, bed_z, 0.795f, 0.08f, 0.53f, 0.52f,
            StartFinish::White, true);

        // Two sides of fabric give privacy while leaving the head and visitor
        // side open. The overhead tracks align with the fabric panels.
        const float curtain_x = cx - 1.95f;
        const float curtain_foot_z = bed_z + 1.27f;
        add("hospital detail ward privacy curtain side track tex steel",
            curtain_x, bed_z, 2.46f, 0.045f, 0.04f, 2.42f,
            StartFinish::White);
        add("hospital detail ward privacy curtain foot track tex steel", cx - 1.275f,
            curtain_foot_z, 2.46f, 1.35f, 0.04f, 0.045f,
            StartFinish::White);
        add("hospital detail ward privacy curtain ceiling hanger",
            curtain_x, bed_z, 2.50f, 0.035f, 0.93f, 0.035f,
            StartFinish::Steel);
        add("hospital detail ward privacy curtain ceiling hanger",
            cx - 1.275f, curtain_foot_z, 2.50f, 0.035f, 0.93f, 0.035f,
            StartFinish::Steel);
        add("hospital detail ward privacy curtain panel tex curtain",
            curtain_x, bed_z, 0.45f, 0.035f, 1.99f, 2.38f,
            StartFinish::White);
        add("hospital detail ward privacy curtain panel tex curtain",
            cx - 1.275f, curtain_foot_z, 0.45f, 1.31f, 1.99f, 0.035f,
            StartFinish::White);
    }

    return out;
}

}  // namespace apricot::city
