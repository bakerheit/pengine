#include "physics/terrain_collider.h"

#include "terrain/heightmap.h"

namespace apricot {

float TerrainCollider::height(float x, float z) const {
    return height_at(seed_, x, z);
}

glm::vec3 TerrainCollider::normal(float x, float z) const {
    return normal_at(seed_, x, z);
}

TerrainCollider::GroundHit TerrainCollider::probe_down(
    glm::vec3 origin, float max_distance) const {
    const float h = height(origin.x, origin.z);
    const float drop = origin.y - h;

    GroundHit out;
    // `drop <= max_distance` rather than `< `, and no lower bound: a negative
    // drop means the origin is already under the surface, which is a hit that
    // callers very much need to hear about.
    out.hit = drop <= max_distance;
    out.distance = drop;
    out.point = glm::vec3{origin.x, h, origin.z};
    out.normal = normal(origin.x, origin.z);
    return out;
}

}  // namespace apricot
