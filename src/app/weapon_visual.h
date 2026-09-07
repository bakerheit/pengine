#pragma once

#include <vector>
#include "game/weapon.h"
#include "scene/scene.h"

namespace apricot {
class Renderer;

// Small original visual prop. The character supplies a sampled rigid palm
// socket; selecting Unarmed or removing that socket hides every piece.
class WeaponVisual {
public:
    bool init(Renderer& renderer, Scene& scene);
    void sync(Scene& scene, WeaponId weapon, const glm::mat4* hand_world);
    void destroy(Scene& scene);

private:
    struct Part { NodeId node = kInvalidId; Transform local; };
    std::vector<Part> parts_;
    MeshId mesh_ = kInvalidId;
    Renderer* renderer_ = nullptr;
};
}  // namespace apricot
