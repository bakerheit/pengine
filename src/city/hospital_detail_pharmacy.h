#pragma once

#include <initializer_list>
#include <vector>

#include "city/hospital_campus.h"

namespace apricot::city {

// Dispensary fixtures turn together to face the south service window. The
// shelving, preparation bench and pickup desk leave a clear staff aisle.
inline std::vector<StartPart> bake_hospital_detail_pharmacy() {
    std::vector<StartPart> out;
    out.reserve(80);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw_deg = 0.0f) {
        StartPart part{name, {x, z}, bottom, width, height, depth, finish,
                       solid};
        part.yaw_deg = yaw_deg;
        out.push_back(part);
    };

    // Three shelf bays share one floor-supported base and a continuous
    // laminate deck. The backing and four uprights carry the open shelves.
    add("hospital detail pharmacy lower cabinet tex laminate", 34.4f,
        10.42f, 0.315f, 11.8f, 0.69f, 0.64f, StartFinish::White, true);
    add("hospital detail pharmacy cabinet top tex laminate", 34.4f, 10.42f,
        1.005f, 11.9f, 0.07f, 0.70f, StartFinish::White, true);
    for (float x : {30.45f, 34.4f, 38.35f}) {
        add("hospital detail pharmacy cabinet door tex laminate", x, 10.085f,
            0.40f, 3.68f, 0.52f, 0.04f, StartFinish::White);
    }
    add("hospital detail pharmacy shelving back panel tex wallpaint", 34.4f,
        10.72f, 0.315f, 11.8f, 2.0f, 0.12f, StartFinish::White, true);

    constexpr float bay_centres[] = {30.45f, 34.4f, 38.35f};
    constexpr float upright_x[] = {28.5f, 32.4f, 36.4f, 40.3f};
    for (float x : upright_x) {
        add("hospital detail pharmacy shelf upright tex laminate", x, 10.42f,
            0.315f, 0.10f, 2.10f, 0.56f, StartFinish::White, true);
    }
    for (float x : bay_centres) {
        for (float bottom : {1.39f, 1.88f}) {
            add("hospital detail pharmacy shelf board tex laminate", x,
                10.42f, bottom, 3.90f, 0.07f, 0.56f, StartFinish::White,
                true);
        }
    }

    // Keep cartons in tidy, colour-coded groups by bay. Each sits directly on
    // the deck or a shelf board; package pieces are visual props, not blockers.
    constexpr const char* carton_names[] = {
        "hospital detail pharmacy pain relief carton",
        "hospital detail pharmacy cold care carton",
        "hospital detail pharmacy prescription carton",
    };
    constexpr StartFinish carton_finishes[] = {
        StartFinish::WarmWall, StartFinish::Steel, StartFinish::TealDoor,
        StartFinish::Yellow};
    constexpr float shelf_tops[] = {1.075f, 1.46f, 1.95f};
    for (int row = 0; row < 3; ++row) {
        for (int bay = 0; bay < 3; ++bay) {
            const bool full_group = bay == row;
            const int package_count = full_group ? 4 : 3;
            for (int package = 0; package < package_count; ++package) {
                const float offset = full_group
                                         ? -1.35f + 0.90f * static_cast<float>(package)
                                         : -0.95f + 0.95f * static_cast<float>(package);
                const float height = row == 0
                                         ? 0.24f + 0.02f * static_cast<float>(package % 2)
                                         : row == 1
                                               ? 0.27f +
                                                     0.02f * static_cast<float>(package % 2)
                                               : 0.29f +
                                                     0.02f * static_cast<float>(package % 2);
                add(carton_names[bay], bay_centres[bay] + offset, 10.36f,
                    shelf_tops[row], 0.46f, height, 0.28f,
                    carton_finishes[(package + row) % 4]);
            }
        }
    }

    // A single fitted, north-facing sign sits above the shelf wall. Its two
    // uprights stand on the top shelf and meet the panel's rear face.
    add("hospital detail pharmacy sign tex pharmacy-sign", 34.4f, 10.0f,
        2.55f, 2.80f, 0.70f, 0.06f, StartFinish::White, false, 0.0f);
    for (float x : {33.3f, 35.5f}) {
        add("hospital detail pharmacy sign mounting post", x, 10.09f, 1.95f,
            0.10f, 1.30f, 0.12f, StartFinish::Steel);
    }

    // Locked steel storage is tucked at the east end of the pharmacy, outside
    // both the worktop footprint and the approach aisle.
    add("hospital detail pharmacy controlled storage cabinet tex steel",
        42.15f, 9.40f, 0.315f, 1.25f, 2.05f, 0.70f, StartFinish::White,
        true);
    add("hospital detail pharmacy controlled storage door tex steel", 42.15f,
        9.0325f, 0.42f, 1.10f, 1.84f, 0.035f, StartFinish::White);
    add("hospital detail pharmacy controlled storage lock plate", 42.50f,
        8.9975f, 1.58f, 0.13f, 0.16f, 0.035f, StartFinish::Steel);
    add("hospital detail pharmacy controlled storage pull", 42.51f, 8.995f,
        1.22f, 0.04f, 0.22f, 0.04f, StartFinish::TealDoor);

    // A compact preparation bench leaves a broad staff aisle to the pickup
    // counter. Drawers and pulls attach to the north face; all tools rest on
    // the worktop.
    add("hospital detail pharmacy preparation bench tex laminate", 35.5f,
        8.52f, 0.315f, 5.8f, 0.82f, 0.68f, StartFinish::White, true);
    add("hospital detail pharmacy preparation worktop tex laminate", 35.5f,
        8.52f, 1.135f, 6.0f, 0.06f, 0.76f, StartFinish::White, true);
    add("hospital detail pharmacy bench toe rail", 35.5f, 8.16f, 0.315f,
        5.5f, 0.14f, 0.04f, StartFinish::TealDoor);
    for (float x : {34.1f, 36.9f}) {
        add("hospital detail pharmacy bench drawer face", x, 8.16f, 0.54f,
            2.50f, 0.40f, 0.04f, StartFinish::WarmWall);
        add("hospital detail pharmacy bench drawer pull", x, 8.125f, 0.72f,
            0.42f, 0.035f, 0.03f, StartFinish::Steel);
    }

    // Prescription scale: platter, upright and a fitted 4:3 display face.
    add("hospital detail pharmacy prescription scale platter", 33.35f,
        8.43f, 1.195f, 0.50f, 0.04f, 0.38f, StartFinish::Steel);
    add("hospital detail pharmacy prescription scale display support", 33.35f,
        8.43f, 1.235f, 0.09f, 0.19f, 0.08f, StartFinish::Steel);
    add("hospital detail pharmacy prescription scale screen tex scale-screen",
        33.35f, 8.37f, 1.425f, 0.20f, 0.15f, 0.04f,
        StartFinish::White);

    // Label printer and a low-sided checking tray sit within easy reach.
    add("hospital detail pharmacy label printer body", 35.15f, 8.45f,
        1.195f, 0.58f, 0.21f, 0.36f, StartFinish::Steel);
    add("hospital detail pharmacy label printer feed lid", 35.15f, 8.45f,
        1.405f, 0.40f, 0.035f, 0.22f, StartFinish::WarmWall);
    add("hospital detail pharmacy label printer output slip", 35.15f, 8.24f,
        1.30f, 0.22f, 0.07f, 0.08f, StartFinish::White);
    add("hospital detail pharmacy prescription checking tray", 36.45f,
        8.45f, 1.195f, 0.82f, 0.04f, 0.42f, StartFinish::WarmWall);
    for (float x : {36.08f, 36.82f}) {
        add("hospital detail pharmacy checking tray rail", x, 8.45f, 1.235f,
            0.04f, 0.08f, 0.42f, StartFinish::Steel);
    }

    // Three capped medicine bottles stand in a shallow organizer tray.
    add("hospital detail pharmacy bottle organizer tray", 37.55f, 8.48f,
        1.195f, 0.92f, 0.035f, 0.42f, StartFinish::Steel);
    for (float x : {37.25f, 37.55f, 37.85f}) {
        add("hospital detail pharmacy work bottle", x, 8.48f, 1.23f, 0.14f,
            0.17f, 0.14f, StartFinish::White);
        add("hospital detail pharmacy bottle cap", x, 8.48f, 1.40f, 0.15f,
            0.04f, 0.15f, StartFinish::TealDoor);
    }

    for (auto& part : out) {
        part.centre.x = 71.0f - part.centre.x;
        part.centre.z = 16.0f - part.centre.z;
        part.yaw_deg += 180.0f;
    }
    return out;
}

}  // namespace apricot::city
