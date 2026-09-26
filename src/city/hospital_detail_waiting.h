#pragma once

#include <initializer_list>
#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Small comfort and housekeeping details fit around the existing waiting
// seats. The wheelchair bays and the open lobby routes stay clear.
inline std::vector<StartPart> bake_hospital_detail_waiting() {
    std::vector<StartPart> out;
    out.reserve(44);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };

    // The slim side frames rise from the floor to the arm pads. The pads sit
    // just above the existing seat tops and stop at the chair-back line.
    for (float x : {11.0f, 14.0f, 17.0f}) {
        for (float z : {13.2f, 20.2f}) {
            for (float side : {-1.0f, 1.0f}) {
                const float arm_x = x + side * 0.405f;
                add("hospital detail waiting chair arm frame tex steel",
                    arm_x, z - 0.03f, 0.315f, 0.09f, 0.465f, 0.48f,
                    StartFinish::White, true);
                add("hospital detail waiting chair arm pad tex upholstery",
                    arm_x, z - 0.03f, 0.78f, 0.16f, 0.16f, 0.48f,
                    StartFinish::White, true);
            }
        }
    }

    // Add simple end arms to the two existing long benches without splitting
    // their seating area or extending into the adjacent route.
    for (float z : {13.2f, 20.2f}) {
        for (float side : {-1.0f, 1.0f}) {
            const float arm_x = 34.0f + side * 3.48f;
            add("hospital detail waiting bench arm frame tex steel", arm_x,
                z - 0.02f, 0.315f, 0.09f, 0.485f, 0.48f,
                StartFinish::White, true);
            add("hospital detail waiting bench arm pad tex upholstery",
                arm_x, z - 0.02f, 0.80f, 0.17f, 0.16f, 0.48f,
                StartFinish::White, true);
        }
    }

    // Two compact side tables sit beside the chair rows, well west of the
    // wheelchair bays. Each has a broad floor foot, a steel stem, and a
    // laminate top at a comfortable reach height.
    for (float z : {13.2f, 20.2f}) {
        add("hospital detail waiting side table foot tex steel", 18.35f, z,
            0.315f, 0.38f, 0.04f, 0.34f, StartFinish::White, true);
        add("hospital detail waiting side table stem tex steel", 18.35f, z,
            0.355f, 0.08f, 0.415f, 0.08f, StartFinish::White, true);
        add("hospital detail waiting side table top tex laminate", 18.35f, z,
            0.77f, 0.58f, 0.07f, 0.48f, StartFinish::White, true);
    }

    // Tissue box and a pair of period-neutral waiting-room magazines rest on
    // the table tops rather than floating above them.
    add("hospital detail waiting tissue box tex laminate", 18.35f, 13.2f,
        0.84f, 0.19f, 0.10f, 0.14f, StartFinish::White);
    add("hospital detail waiting tissue", 18.35f, 13.2f, 0.94f,
        0.06f, 0.05f, 0.045f, StartFinish::White);
    add("hospital detail waiting magazine", 18.28f, 20.15f,
        0.84f, 0.20f, 0.018f, 0.28f, StartFinish::RedTrim, false, -5.0f);
    add("hospital detail waiting magazine", 18.43f, 20.23f,
        0.858f, 0.20f, 0.018f, 0.28f, StartFinish::TealDoor, false, 4.0f);

    // A small open-topped bin is next to the existing water refill station,
    // outside its footprint and clear of the seating and wheelchair bays.
    add("hospital detail waiting waste bin body tex steel", 41.3f, 22.8f,
        0.315f, 0.36f, 0.46f, 0.36f, StartFinish::White, true);
    add("hospital detail waiting waste bin rim tex steel", 41.3f, 22.8f,
        0.775f, 0.41f, 0.035f, 0.41f, StartFinish::White);

    return out;
}

}  // namespace apricot::city
