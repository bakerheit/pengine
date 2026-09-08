#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>

#include "core/aabb.h"
#include "traffic/crowd.h"

namespace apricot {

// The bounds and matrix must describe the same frame: native fitted mesh
// bounds + body-to-world, or already-placed chassis bounds + chassis-to-world.
// This is an opaque body proxy; it deliberately does not trace window glass.
struct PoliceVisibilityBody {
    VisiblePoliceIdentity identity{};
    AABB local_bounds;
    glm::mat4 world_from_local{1.0f};
};

// Active traffic is not part of TerrainCollider's static/kinematic prop list.
// Apply this second occlusion check after that collider's world ray succeeds.
// The witness's cruiser is excluded because a seated officer's eye lies in it.
// A surface at the target endpoint is the thing being seen, not an obstruction.
inline bool police_traffic_blocks_view(
    glm::vec3 eye, glm::vec3 target,
    const std::vector<PoliceVisibilityBody>& bodies,
    VisiblePoliceIdentity witness, float endpoint_margin_m = 0.08f) {
    const glm::vec3 delta = target - eye;
    const float distance = glm::length(delta);
    if (!std::isfinite(distance)) return true;
    const float ray_length = distance - std::max(0.0f, endpoint_margin_m);
    if (!(ray_length > 0.0f)) return false;
    const glm::vec3 direction = delta / distance;
    for (const auto& body : bodies) {
        if (body.identity == witness || !body.local_bounds.valid()) continue;
        // A cheap world-space broad phase avoids matrix inversion for the
        // many traffic bodies that are nowhere near this sight segment.
        const auto broad = body.local_bounds.transformed(body.world_from_local)
            .intersect_ray(eye, direction, 0.0f, ray_length);
        if (!broad || broad.t_near >= ray_length) continue;
        const float determinant = glm::determinant(body.world_from_local);
        if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-12f)
            continue;
        const glm::mat4 local_from_world = glm::inverse(body.world_from_local);
        const glm::vec3 local_eye{local_from_world * glm::vec4{eye, 1.0f}};
        const glm::vec3 local_direction{local_from_world * glm::vec4{direction, 0.0f}};
        // Do not normalize after inverse scale: the ray parameter must stay
        // in WORLD metres so a scaled mesh cannot block beyond the endpoint.
        const auto hit = body.local_bounds.intersect_ray(
            local_eye, local_direction, 0.0f, ray_length);
        if (hit && hit.t_near < ray_length) return true;
    }
    return false;
}

}  // namespace apricot
