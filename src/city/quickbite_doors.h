#pragma once

#include <vector>

#include "city/start_area.h"
#include "game/house_door.h"

namespace apricot::city {

struct QuickbiteDoor {
    const char* name = nullptr;
    glm::vec2 local_hinge{};
    float local_yaw = 0.0f;
    HouseDoor physics{};
    const StartSite* site = &kFastFoodSite;
};

inline std::vector<QuickbiteDoor> quickbite_doors(const StartSite& site) {
    std::vector<QuickbiteDoor> out;
    const auto add = [&](const char* name, glm::vec2 hinge, float yaw) {
        const glm::vec2 p{
            site.origin.x + site.cos_yaw * hinge.x + site.sin_yaw * hinge.y,
            site.origin.z - site.sin_yaw * hinge.x + site.cos_yaw * hinge.y};
        HouseDoor door;
        door.hinge = {p.x, site.ground_m + 0.225f, p.y};
        door.closed_yaw = std::atan2(site.sin_yaw, site.cos_yaw) + yaw;
        door.width = 1.18f;
        door.height = 2.55f;
        door.thickness = 0.065f;
        door.pivot_inset = 0.09f;
        out.push_back({name, hinge, yaw, door, &site});
    };
    // Each leaf is narrower than house one's 1.418 m front door. Together
    // they fit the existing 2.5 m restaurant opening with a small 3 cm centre
    // reveal and enough hinge-side sweep clearance for the authored jambs.
    add("Cloggers west glass push door", {-6.104f, -5.0f}, 0.0f);
    add("Cloggers east glass push door", {-3.896f, -5.0f}, 3.14159265359f);
    return out;
}

inline std::vector<QuickbiteDoor> quickbite_doors() {
    return quickbite_doors(kFastFoodSite);
}

inline std::vector<StartPart> quickbite_door_parts(const QuickbiteDoor& door,
                                                   float angle) {
    const StartSite& site = door.site ? *door.site : kFastFoodSite;
    const float yaw = door.local_yaw + angle;
    const glm::vec2 tangent{std::cos(yaw), -std::sin(yaw)};
    const glm::vec2 normal{-tangent.y, tangent.x};
    std::vector<StartPart> out;
    const auto add = [&](const char* name, float along, float across,
                         float bottom, float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        const glm::vec2 p = door.local_hinge +
            tangent * (along - door.physics.pivot_inset) + normal * across;
        out.push_back({name, {p.x, p.y},
            door.physics.hinge.y - site.ground_m + bottom,
            width, height, depth, finish, solid, 0.0f, glm::degrees(yaw)});
    };

    add(door.name, door.physics.width * .5f, 0.0f, 0.0f,
        door.physics.width, door.physics.height, door.physics.thickness,
        StartFinish::Glass, true);
    for (float along : {.055f, door.physics.width - .055f})
        add("Cloggers glass door stile", along, 0.0f, 0.0f,
            .11f, door.physics.height, .085f, StartFinish::RedTrim);
    for (float y : {.055f, 1.05f, door.physics.height - .055f})
        add("Cloggers glass door rail", door.physics.width * .5f, 0.0f,
            y - .055f, door.physics.width - .11f, .11f, .085f,
            StartFinish::RedTrim);
    for (float side : {-1.0f, 1.0f})
        add("Cloggers glass door pull", door.physics.width - .20f,
            side * .072f, .92f, .075f, .42f, .055f, StartFinish::Steel);
    return out;
}

} // namespace apricot::city
