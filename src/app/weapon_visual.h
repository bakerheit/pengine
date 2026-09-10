#pragma once

#include <memory>
#include <vector>
#include "app/character_visual.h"
#include "game/weapon.h"
#include "game/blood_particles.h"
#include "scene/scene.h"

namespace apricot {
class Renderer;

// Small original visual prop. The character supplies a sampled rigid palm
// socket; selecting Unarmed or removing that socket hides every piece.
class WeaponVisual {
public:
    bool init(Renderer& renderer, Scene& scene);
    void sync(Scene& scene, WeaponId weapon, const glm::mat4* hand_world);
    void sync(Scene& scene, WeaponId weapon, const glm::mat4* hand_world,
              const WeaponUseState& use);
    void show_impact(glm::vec3 world_position) { impact_position_ = world_position; impact_live_ = true; }
    void clear_impact() { impact_live_ = false; }
    void show_blood(glm::vec3 position, glm::vec3 direction, uint64_t event_id) {
        clear_impact();
        blood_.emit(position, direction, event_id);
    }
    void step_particles(float dt) { blood_.step(dt); }
    std::size_t blood_particle_count() const { return blood_.live_count(); }
    bool muzzle_world(glm::vec3& origin, glm::vec3& direction) const;
    void destroy(Scene& scene);

private:
    enum class Motion { Fixed, Slide, Magazine, Flash };
    struct Part { NodeId node = kInvalidId; Transform local; Motion motion = Motion::Fixed; };
    std::vector<Part> parts_;
    std::vector<NodeId> impact_nodes_;
    std::vector<NodeId> blood_nodes_;
    BloodParticles blood_;
    Transform hand_;
    glm::vec3 impact_position_{0.f};
    bool visible_ = false;
    bool impact_live_ = false;
    MeshId mesh_ = kInvalidId;
    Renderer* renderer_ = nullptr;
};

// One attached copy of the existing pistol prop per armed officer.
class PoliceWeaponVisual {
public:
    void init(Renderer& renderer) { renderer_ = &renderer; }
    void sync(Scene& scene,
              const std::vector<CharacterVisual::PoliceWeaponSocket>& sockets);
    void destroy(Scene& scene);

private:
    struct Entry {
        uint64_t lane_key = 0;
        uint32_t slot = 0;
        WeaponVisual visual;
    };
    Renderer* renderer_ = nullptr;
    std::vector<std::unique_ptr<Entry>> entries_;
};
}  // namespace apricot
