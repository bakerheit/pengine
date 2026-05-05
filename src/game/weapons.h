#pragma once

#include <string>

#include <glm/glm.hpp>

#include "render/mesh.h"

namespace pengine {

class PedestrianSystem;
class Shader;
class TrafficSystem;
class WorldCollision;

// Player pistol: static Glock 17 mesh + grip offset + hitscan firing.
// Decoupled from Player via the right-hand bone matrix the caller passes in
// (so PedestrianSystem can also reuse the mesh + grip for police peds).
//
// fire() doesn't itself emit audio, particles, or wanted heat — it returns
// a FireResult the caller drains, matching the event-vector pattern used by
// PedestrianSystem.
class Weapons {
public:
    Weapons() = default;
    Weapons(const Weapons&)            = delete;
    Weapons& operator=(const Weapons&) = delete;

    bool init(const std::string& assets_root);
    void shutdown();

    struct FireResult {
        glm::vec3 origin    {0.f};
        glm::vec3 hit_point {0.f};
        bool      hit_ped    = false;
        float     heat_delta = 0.f;
    };

    // Hitscan against the static world, AI-car AABBs (occlusion only) and
    // pedestrians (damage with distance falloff). Heat-delta covers both
    // the fire-shot base contribution and the bonus for hitting a ped.
    FireResult fire(const glm::vec3& origin, const glm::vec3& dir,
                    const WorldCollision& world_col,
                    const TrafficSystem& traffic,
                    PedestrianSystem& pedestrians) const;

    // Render the gun on the supplied right-hand world transform (already
    // includes the player's visual-node matrix, the bone's world matrix,
    // and any model-scale baked in by Player). We multiply on the local
    // gun scale + grip offset. Caller binds the diffuse texture.
    void render(Shader& lit, const glm::mat4& view_proj,
                const glm::vec3& cam_pos,
                const glm::mat4& hand_world_xform) const;

    // Borrowed by PedestrianSystem::render so police peds use the same
    // mesh + tuned grip offset as the player.
    const Mesh&      mesh()        const { return gun_mesh_; }
    const glm::mat4& grip_offset() const { return gun_grip_offset_; }
    bool             ready()       const { return gun_mesh_.index_count() > 0; }

private:
    Mesh      gun_mesh_;
    glm::mat4 gun_grip_offset_ {1.f};
};

} // namespace pengine
