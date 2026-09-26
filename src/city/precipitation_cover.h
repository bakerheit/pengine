#pragma once

#include <cstring>

#include "city/start_area.h"
#include "core/transform.h"
#include "physics/terrain_collider.h"

namespace apricot::city {

// A roof with no walls under it lets wind carry snow in from its edges. The
// list is deliberately short and names structures, not materials: anything it
// does not name is treated as a building, and a building's floor never drifts.
// Getting this wrong in the open direction puts snow inside a shop, so a new
// open structure has to be added here on purpose.
inline bool precipitation_cover_open_sided(const char* name) {
    if (!name) return false;
    for (const char* open : {"canopy", "carport", "shelter", "awning", "gazebo",
                             "portico", "pergola"})
        if (std::strstr(name, open)) return true;
    return false;
}

// Roof skins and indoor ceilings are often deliberately non-solid. Give
// weather its own cover geometry without adding invisible player obstacles.
inline void append_precipitation_cover(const StartPart& part,
                                       const Transform& transform,
                                       std::vector<StaticBox>& out) {
    if (!part.name || (!std::strstr(part.name, "roof") &&
                       !std::strstr(part.name, "ceiling"))) return;
    StaticBox box;
    box.open_sided = precipitation_cover_open_sided(part.name);
    const AABB unit{{-.5f, -.5f, -.5f}, {.5f, .5f, .5f}};
    box.bounds = unit.transformed(transform.matrix());
    if (part.pitch_deg == 0 && part.roll_deg == 0) {
        box.oriented = true;
        box.centre = transform.position;
        box.local_bounds = {-transform.scale * .5f, transform.scale * .5f};
        const glm::vec3 x = transform.rotation * glm::vec3{1, 0, 0};
        const glm::vec3 z = transform.rotation * glm::vec3{0, 0, 1};
        box.axis_x = {x.x, x.z};
        box.axis_z = {z.x, z.z};
    }
    // Pitched pieces use conservative bounds, as world collision does.
    out.push_back(box);
}

}  // namespace apricot::city
