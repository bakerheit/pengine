#pragma once

#include <array>
#include <vector>

#include "physics/terrain_collider.h"

namespace apricot {

// Vertical rain shadows from the same solid geometry used by the world.
// Rebuilt at draw time so streaming, teleports and moving doors cannot leave
// stale cover behind. Only nearby boxes reach the per-particle narrow phase.
class PrecipitationShelter {
public:
    void rebuild(const std::vector<StaticBox>& boxes, const AABB& field,
                 const std::vector<StaticBox>& roofs = {}) {
        boxes_.clear();
        append_nearby(boxes, field);
        append_nearby(roofs, field);
    }

private:
    void append_nearby(const std::vector<StaticBox>& boxes, const AABB& field) {
        for (const StaticBox& box : boxes) {
            if (!box.enabled || box.is_vehicle ||
                box.bounds.max.y < field.min.y ||
                box.bounds.max.x < field.min.x || box.bounds.min.x > field.max.x ||
                box.bounds.max.z < field.min.z || box.bounds.min.z > field.max.z)
                continue;
            // Do not cap roof height at the field's top: a tall roof still
            // shelters particles seeded inside the room underneath it.
            boxes_.push_back(&box);
        }
    }

public:
    bool sheltered(const std::array<glm::vec3, 4>& quad) const {
        AABB bounds;
        for (const glm::vec3 p : quad) bounds.expand(p);
        for (const StaticBox* box : boxes_) {
            if (bounds.min.y > box->bounds.max.y ||
                bounds.max.x < box->bounds.min.x || bounds.min.x > box->bounds.max.x ||
                bounds.max.z < box->bounds.min.z || bounds.min.z > box->bounds.max.z)
                continue;
            if (!box->oriented) return true;

            AABB local;
            for (const glm::vec3 p : quad) local.expand(box->local_point(p));
            const AABB& roof = box->local_bounds;
            if (local.max.x >= roof.min.x && local.min.x <= roof.max.x &&
                local.max.z >= roof.min.z && local.min.z <= roof.max.z)
                return true;
        }
        return false;
    }

private:
    // Valid only until the collider's box storage changes; render owns the
    // rebuild/query interval and performs no world mutations during it.
    std::vector<const StaticBox*> boxes_;
};

}  // namespace apricot
