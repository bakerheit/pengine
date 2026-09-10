#include "app/weapon_visual.h"
#include "app/weapon_visual_pose.h"

#include "gfx/primitives.h"
#include "gfx/renderer.h"

#include <algorithm>

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
                         float pitch = 0.f, Motion motion = Motion::Fixed) {
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
        parts_.push_back({node, local, motion});
    };
    // Grip centre is the attachment origin. Native forward is -Z. Dimensions
    // are metres: compact ~21 cm profile with a real open trigger-guard gap.
    add({0, .003f, .006f}, {.029f, .095f, .037f}, grip, -12);
    add({0, -.045f, .016f}, {.033f, .009f, .042f}, dark, -12, Motion::Magazine);
    add({0, -.011f, .009f}, {.022f, .064f, .027f}, steel, -12, Motion::Magazine);
    add({0, .049f, -.047f}, {.033f, .026f, .177f}, steel);
    add({0, .071f, -.044f}, {.034f, .026f, .190f}, steel, 0.f, Motion::Slide);
    add({0, .085f, -.044f}, {.026f, .004f, .181f}, edge, 0.f, Motion::Slide);
    add({0, .064f, -.141f}, {.022f, .018f, .008f}, dark);
    add({0, .064f, -.146f}, {.010f, .010f, .001f}, grip);
    add({0, .021f, -.061f}, {.009f, .028f, .008f}, steel, 12);
    add({0, .006f, -.043f}, {.010f, .007f, .043f}, steel);
    add({0, .026f, -.032f}, {.005f, .018f, .007f}, dark, -20);
    add({0, .091f, -.117f}, {.005f, .008f, .009f}, dark, 0.f, Motion::Slide);
    add({-.010f, .091f, .032f}, {.006f, .008f, .013f}, dark, 0.f, Motion::Slide);
    add({.010f, .091f, .032f}, {.006f, .008f, .013f}, dark, 0.f, Motion::Slide);
    add({.0175f, .075f, -.026f}, {.001f, .009f, .027f}, dark, 0.f, Motion::Slide);
    for (int i = 0; i < 5; ++i) {
        const float z = .009f + static_cast<float>(i) * .006f;
        add({-.0175f, .071f, z}, {.001f, .017f, .002f}, dark, 0.f, Motion::Slide);
        add({.0175f, .071f, z}, {.001f, .017f, .002f}, dark, 0.f, Motion::Slide);
    }
    // A short, narrow flash extends beyond the actual barrel mouth.
    add({0, .064f, -.175f}, {.018f, .018f, .055f}, {1.f, .77f, .24f, 2.5f}, 0.f, Motion::Flash);
    add({0, .064f, -.163f}, {.035f, .008f, .024f}, {1.f, .44f, .08f, 2.f}, 0.f, Motion::Flash);
    add({0, .064f, -.163f}, {.008f, .035f, .024f}, {1.f, .44f, .08f, 2.f}, 0.f, Motion::Flash);
    for (int i = 0; i < 3; ++i) {
        Renderable renderable;
        renderable.mesh = mesh_;
        renderable.material = renderer.white_material();
        renderable.tint = {.8f, .65f, .40f, 1.6f};
        const NodeId node = scene.create(renderable, Transform{}, cube.bounds);
        if (auto* value = scene.get(node)) value->visible = false;
        impact_nodes_.push_back(node);
    }
    return true;
}

void WeaponVisual::sync(Scene& scene, WeaponId weapon, const glm::mat4* hand_world) {
    WeaponUseState idle;
    idle.equipped = weapon;
    sync(scene, weapon, hand_world, idle);
}

void WeaponVisual::sync(Scene& scene, WeaponId weapon, const glm::mat4* hand_world,
                        const WeaponUseState& use) {
    visible_ = weapon == WeaponId::Pistol && hand_world;
    const auto pose = weapon_visual_pose(use);
    if (visible_) {
        hand_.position = glm::vec3{(*hand_world)[3]};
        hand_.rotation = glm::normalize(glm::quat_cast(glm::mat3{*hand_world}));
    }
    for (const auto& part : parts_) {
        const bool show = visible_ && (part.motion != Motion::Flash || pose.flash_scale > 0.f);
        if (auto* node = scene.get(part.node)) node->visible = show;
        if (!show) continue;
        Transform local = part.local;
        if (part.motion == Motion::Slide) local.position.z += pose.slide_back;
        if (part.motion == Motion::Magazine) {
            local.position.y -= pose.magazine_drop;
            local.position.z += .2f * pose.magazine_drop;
            local.rotation *= glm::quat(glm::vec3{glm::radians(pose.magazine_pitch), 0.f, 0.f});
        }
        if (part.motion == Motion::Flash) local.scale *= pose.flash_scale;
        scene.set_transform(part.node, hand_ * local);
    }
    if (pose.impact_scale <= 0.f) impact_live_ = false;
    for (std::size_t i = 0; i < impact_nodes_.size(); ++i) {
        const bool show = impact_live_ && pose.impact_scale > 0.f;
        if (auto* node = scene.get(impact_nodes_[i])) node->visible = show;
        if (!show) continue;
        const float age = 1.f - pose.impact_scale;
        const float side = static_cast<float>(i) - 1.f;
        Transform spark;
        spark.position = impact_position_ + glm::vec3{side * .08f * age, .035f + .13f * age, side * .05f * age};
        spark.scale = glm::vec3{.035f, .022f, .035f} * pose.impact_scale;
        scene.set_transform(impact_nodes_[i], spark);
    }
    // Allocate this pool once, only for weapons which actually produce body
    // hits. Ordinary officer weapon copies never need these extra scene nodes.
    if (blood_nodes_.empty() && blood_.live_count() > 0 && renderer_ && mesh_ != kInvalidId) {
        const auto cube = make_box(glm::vec3{.5f});
        blood_nodes_.reserve(BloodParticles::kCapacity);
        for (std::size_t i = 0; i < BloodParticles::kCapacity; ++i) {
            Renderable renderable;
            renderable.mesh = mesh_;
            renderable.material = renderer_->white_material();
            const NodeId node = scene.create(renderable, Transform{}, cube.bounds);
            if (auto* value = scene.get(node)) value->visible = false;
            blood_nodes_.push_back(node);
        }
    }
    for (std::size_t i = 0; i < blood_nodes_.size(); ++i) {
        const auto draw = blood_.draw(i);
        if (auto* node = scene.get(blood_nodes_[i])) {
            node->visible = draw.visible;
            node->renderable.tint = draw.tint;
        }
        if (!draw.visible) continue;
        Transform droplet;
        droplet.position = draw.position;
        droplet.scale = draw.scale;
        scene.set_transform(blood_nodes_[i], droplet);
    }
}

bool WeaponVisual::muzzle_world(glm::vec3& origin, glm::vec3& direction) const {
    if (!visible_) return false;
    origin = hand_.transform_point({0.f, .064f, -.147f});
    direction = hand_.forward();
    return true;
}

void WeaponVisual::destroy(Scene& scene) {
    for (const auto& part : parts_) scene.remove(part.node);
    parts_.clear();
    for (const auto node : impact_nodes_) scene.remove(node);
    impact_nodes_.clear();
    for (const auto node : blood_nodes_) scene.remove(node);
    blood_nodes_.clear();
    blood_.clear();
    visible_ = false;
    impact_live_ = false;
    if (renderer_ && mesh_ != kInvalidId) renderer_->remove_mesh(mesh_);
    mesh_ = kInvalidId;
    renderer_ = nullptr;
}

void PoliceWeaponVisual::sync(
        Scene& scene,
        const std::vector<CharacterVisual::PoliceWeaponSocket>& sockets) {
    std::vector<std::unique_ptr<Entry>> next;
    next.reserve(sockets.size());
    for (const auto& socket : sockets) {
        auto found = std::find_if(entries_.begin(), entries_.end(),
            [&](const auto& entry) {
                return entry && entry->lane_key == socket.lane_key &&
                    entry->slot == socket.slot;
            });
        std::unique_ptr<Entry> entry;
        if (found != entries_.end()) {
            entry = std::move(*found);
            entries_.erase(found);
        } else if (renderer_) {
            entry = std::make_unique<Entry>();
            entry->lane_key = socket.lane_key;
            entry->slot = socket.slot;
            if (!entry->visual.init(*renderer_, scene)) continue;
        }
        if (!entry) continue;
        WeaponUseState use;
        use.equipped = WeaponId::Pistol;
        use.equip_blend = 1.0f;
        use.aim_blend = 1.0f;
        use.recoil = socket.flash_ticks > 0 ? 1.0f : 0.0f;
        use.shot_age = socket.flash_ticks > 0
            ? static_cast<float>(8u - std::min<uint16_t>(8u, socket.flash_ticks)) /
                120.0f
            : 60.0f;
        entry->visual.sync(scene, WeaponId::Pistol, &socket.hand_world, use);
        next.push_back(std::move(entry));
    }
    for (auto& stale : entries_)
        if (stale) stale->visual.destroy(scene);
    entries_ = std::move(next);
}

void PoliceWeaponVisual::destroy(Scene& scene) {
    for (auto& entry : entries_)
        if (entry) entry->visual.destroy(scene);
    entries_.clear();
    renderer_ = nullptr;
}

}  // namespace apricot
