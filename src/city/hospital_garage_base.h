#pragma once

#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// The clinical shell was replaced by the professional ring-and-spine plan,
// but the approved three-level garage remains a separate reusable structure.
inline std::vector<StartPart> bake_hospital_garage_base() {
    std::vector<StartPart> out;
    out.reserve(180);
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    const float garage_x = kHospitalGarageCentre.x;
    const float garage_z = kHospitalGarageCentre.z;
    add("hospital garage lot", garage_x, garage_z, 0.0f,
        kHospitalGarageWidthM, 0.10f, kHospitalBlockDepthM,
        StartFinish::Asphalt);
    add("hospital garage ground deck", garage_x, garage_z, 0.10f,
        kHospitalGarageWidthM, 0.20f, 37.0f, StartFinish::Concrete, true);
    for (int level = 1; level <= 2; ++level) {
        const float deck = 0.10f + level * 3.7f;
        add("hospital garage upper deck", -22.9f, garage_z, deck,
            8.2f, 0.28f, 37.0f, StartFinish::Concrete, true);
        add("hospital garage upper deck", 54.9f, garage_z, deck,
            128.2f, 0.28f, 37.0f, StartFinish::Concrete, true);
        add("hospital garage upper deck", -14.0f, 192.25f, deck,
            9.6f, 0.28f, 24.5f, StartFinish::Concrete, true);
        add("hospital garage front fascia", garage_x, garage_z - 18.35f,
            deck + 0.28f, kHospitalGarageWidthM, 0.65f, 0.35f,
            StartFinish::Concrete, true);
        add("hospital garage rear fascia", garage_x, garage_z + 18.35f,
            deck + 0.28f, kHospitalGarageWidthM, 0.65f, 0.35f,
            StartFinish::Concrete, true);
    }
    for (int level = 0; level < 3; ++level) {
        const float bottom = 0.22f + level * 3.7f;
        for (float x : {-68.0f, -34.0f, 0.0f, 34.0f, 68.0f}) {
            if (x != 0.0f) {
                add("hospital garage column", garage_x + x,
                    garage_z - 15.0f, bottom, 0.60f, 3.35f, 0.60f,
                    StartFinish::Steel, true);
            }
            if (x != 34.0f) {
                add("hospital garage column", garage_x + x,
                    garage_z + 15.0f, bottom, 0.60f, 3.35f, 0.60f,
                    StartFinish::Steel, true);
            }
        }
        for (float x = -64.0f; x <= 64.0f; x += 8.0f) {
            const float stripe_x = garage_x + x;
            if ((stripe_x >= -19.0f && stripe_x <= -9.0f) ||
                (stripe_x >= 40.0f && stripe_x <= 52.0f))
                continue;
            add("hospital garage parking stripe", stripe_x,
                garage_z - 7.0f, bottom + 3.40f, 0.10f, 0.018f, 5.0f,
                StartFinish::White);
        }
    }
    add("hospital garage parapet front", garage_x, garage_z - 18.5f,
        7.95f, kHospitalGarageWidthM, 1.05f, 0.35f,
        StartFinish::TealDoor, true);
    add("hospital garage parapet rear", garage_x, garage_z + 18.5f,
        7.95f, kHospitalGarageWidthM, 1.05f, 0.35f,
        StartFinish::TealDoor, true);
    add("hospital garage entry walk", garage_x, garage_z - 22.0f, 0.10f,
        12.0f, 0.10f, 6.0f, StartFinish::Concrete);
    add("hospital garage sign", garage_x, garage_z - 18.72f, 5.3f,
        16.0f, 1.6f, 0.08f, StartFinish::RedTrim);
    add("hospital garage entry pay station body", 0.0f, 195.0f, 0.20f,
        0.74f, 1.25f, 0.68f, StartFinish::TealDoor, true);
    add("hospital garage entry pay station control face", -0.39f, 195.0f,
        0.47f, 0.04f, 0.90f, 0.60f, StartFinish::TealDoor);

    add("hospital garage skybridge floor", garage_x, garage_z - 31.0f,
        3.93f, 11.0f, 0.18f, 24.0f, StartFinish::Concrete, true);
    for (float x : {garage_x - 5.35f, garage_x + 5.35f}) {
        add("hospital garage skybridge lower rail", x, garage_z - 31.0f,
            4.11f, 0.22f, 0.72f, 24.0f, StartFinish::TealDoor, true);
        add("hospital garage skybridge glazing", x, garage_z - 31.0f,
            4.83f, 0.08f, 1.78f, 24.0f, StartFinish::Glass);
        add("hospital garage skybridge upper rail", x, garage_z - 31.0f,
            6.61f, 0.22f, 0.59f, 24.0f, StartFinish::TealDoor, true);
    }
    add("hospital garage skybridge roof", garage_x, garage_z - 31.0f, 7.2f,
        11.5f, 0.25f, 24.5f, StartFinish::TealDoor, true);
    add("hospital garage skybridge hospital landing", garage_x, 136.5f,
        3.93f, 11.0f, 0.18f, 13.0f, StartFinish::Concrete, true);
    for (float x : {garage_x - 5.35f, garage_x + 5.35f}) {
        add("hospital garage skybridge hospital landing wall", x, 136.5f,
            4.11f, 0.22f, 3.09f, 13.0f, StartFinish::WarmWall, true);
    }
    add("hospital garage skybridge hospital landing roof", garage_x,
        136.5f, 7.2f, 11.5f, 0.25f, 13.5f,
        StartFinish::TealDoor, true);
    return out;
}

}  // namespace apricot::city
