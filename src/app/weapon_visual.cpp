#include "app/weapon_visual.h"

#include "gfx/primitives.h"
#include "gfx/renderer.h"

namespace apricot {

bool WeaponVisual::init(Renderer& renderer, Scene& scene) {
    destroy(scene);
    renderer_ = &renderer;
    const auto cube = make_box(glm::vec3{.5f});
    mesh_ = renderer.add_mesh(cube);
    if (mesh_ == kInvalidId) return false;
    const glm::vec4 steel{.23f, .25f, .27f, 1};
    const glm::vec4 edge{.36f, .38f, .39f, 1};
    const glm::vec4 grip{.075f, .068f, .058f, 1};
    const glm::vec4 dark{.025f, .027f, .029f, 1};
    const auto add = [&](glm::vec3 center, glm::vec3 size, glm::vec4 tint,
                         float pitch = 0.f) {
        Transform local;
        local.position = center;
        local.scale = size;
        local.set_euler_deg(pitch, 0, 0);
        Renderable renderable;
        renderable.mesh = mesh_;
        renderable.material = renderer.white_material();
        renderable.tint = tint;
        const NodeId node = scene.create(renderable, local, cube.bounds);
        if (auto* value = scene.get(node)) value->visible = false;
        parts_.push_back({node, local});
    };
    // Grip centre is the attachment origin. Native forward is -Z. Dimensions
    // are metres: compact ~21 cm profile with a real open trigger-guard gap.
    add({0, .003f, .006f}, {.029f, .095f, .037f}, grip, -12);
    add({0, -.045f, .016f}, {.033f, .009f, .042f}, dark, -12);
    add({0, .049f, -.047f}, {.033f, .026f, .177f}, steel);
    add({0, .071f, -.044f}, {.034f, .026f, .190f}, steel);
    add({0, .085f, -.044f}, {.026f, .004f, .181f}, edge);
    add({0, .064f, -.141f}, {.022f, .018f, .008f}, dark);
    add({0, .064f, -.146f}, {.010f, .010f, .001f}, grip);
    add({0, .021f, -.061f}, {.009f, .028f, .008f}, steel, 12);
    add({0, .006f, -.043f}, {.010f, .007f, .043f}, steel);
    add({0, .026f, -.032f}, {.005f, .018f, .007f}, dark, -20);
    add({0, .091f, -.117f}, {.005f, .008f, .009f}, dark);
    add({-.010f, .091f, .032f}, {.006f, .008f, .013f}, dark);
    add({.010f, .091f, .032f}, {.006f, .008f, .013f}, dark);
    add({.0175f, .075f, -.026f}, {.001f, .009f, .027f}, dark);
    for (int i = 0; i < 5; ++i) {
        const float z = .009f + static_cast<float>(i) * .006f;
        add({-.0175f, .071f, z}, {.001f, .017f, .002f}, dark);
        add({.0175f, .071f, z}, {.001f, .017f, .002f}, dark);
    }
    return true;
}

void WeaponVisual::sync(Scene& scene, WeaponId weapon, const glm::mat4* hand_world) {
    const bool visible = weapon == WeaponId::Pistol && hand_world;
    Transform hand;
    if (visible) {
        hand.position = glm::vec3{(*hand_world)[3]};
        hand.rotation = glm::normalize(glm::quat_cast(glm::mat3{*hand_world}));
    }
    for (const auto& part : parts_) {
        if (auto* node = scene.get(part.node)) node->visible = visible;
        if (visible) scene.set_transform(part.node, hand * part.local);
    }
}

void WeaponVisual::destroy(Scene& scene) {
    for (const auto& part : parts_) scene.remove(part.node);
    parts_.clear();
    if (renderer_ && mesh_ != kInvalidId) renderer_->remove_mesh(mesh_);
    mesh_ = kInvalidId;
    renderer_ = nullptr;
}

}  // namespace apricot
