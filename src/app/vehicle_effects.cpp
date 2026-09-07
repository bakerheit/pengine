#include "app/vehicle_effects.h"
#include "app/vehicle_effects_layout.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "core/asset_root.h"
#include "core/rng.h"
#include "gfx/primitives.h"
#include "gfx/renderer.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "traffic/ambient.h"
#include "traffic/crowd.h"

namespace apricot {
namespace {

constexpr uint64_t kPlayerOwner = 0x504C415945525F31ull;
constexpr uint64_t kMarkLifetimeSteps = 120u * 14u;
constexpr std::size_t kMaxMarks = 384u;
glm::vec4 fluid_color(VehicleFluidKind kind) {
    switch (kind) {
        case VehicleFluidKind::Coolant:
            return {0.10f, 0.60f, 0.22f, 0.72f};
        case VehicleFluidKind::Oil:
            return {0.055f, 0.045f, 0.035f, 0.84f};
        case VehicleFluidKind::Fuel:
            return {0.55f, 0.38f, 0.10f, 0.46f};
    }
    return {0.1f, 0.1f, 0.1f, 1.0f};
}

glm::vec2 fluid_spread(VehicleFluidKind kind) {
    switch (kind) {
        case VehicleFluidKind::Coolant: return {1.22f, 0.88f};
        case VehicleFluidKind::Oil: return {1.04f, 0.78f};
        case VehicleFluidKind::Fuel: return {1.52f, 1.04f};
    }
    return {1.0f, 1.0f};
}

glm::vec2 normal_or(glm::vec2 value, glm::vec2 fallback) {
    const float length = glm::length(value);
    return length > 1e-5f ? value / length : fallback;
}

}  // namespace

bool VehicleEffects::init(Renderer& renderer) {
    const MeshData decal = make_plane(1.0f, 1);
    mesh_ = renderer.add_mesh(decal);
    Texture spill_texture;
    if (!spill_texture.load_file(
            asset_path("textures/effects/fluid-spill-mask.png"))) {
        return false;
    }
    // A fresh leak stains the top of a snowpack. Letting the generic upward-
    // surface snow shader recolour this material makes every puddle white and
    // reads exactly like the fluid rendered underneath the snow.
    material_ = renderer.add_material(
        std::move(spill_texture), true, 1.0f, Renderer::DepthBias{}, false);
    bounds_ = decal.bounds;
    marks_.reserve(kMaxMarks);
    return mesh_ != kInvalidId && material_ != kInvalidId;
}

void VehicleEffects::step(Scene& scene, const TerrainCollider& collider,
                          uint64_t step, const VehicleState& player,
                          const VehicleTuning& player_tuning,
                          const Crowd& traffic) {
    const auto expired = std::remove_if(
        marks_.begin(), marks_.end(), [&](const Mark& mark) {
            if (step < mark.expires_step) return false;
            scene.remove(mark.node);
            return true;
        });
    marks_.erase(expired, marks_.end());

    const glm::vec3 player_right3 =
        player.orientation * glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 player_forward3 =
        player.orientation * glm::vec3{0.0f, 0.0f, -1.0f};
    emit_vehicle(scene, collider, step, kPlayerOwner, player.body_damage, player.mechanical,
                 player.position, {player_right3.x, player_right3.z},
                 {player_forward3.x, player_forward3.z},
                 player_tuning.car_collision_half_width,
                 player_tuning.car_collision_half_length);

    for (const VehicleAgent& agent : traffic.vehicles()) {
        if (!(vehicle_damage_total(agent.body_damage) > 0.0f)) continue;
        const TrafficVehicleFootprint footprint = traffic_vehicle_footprint(
            traffic_vehicle_kind(agent));
        const glm::vec2 forward = normal_or({agent.fwd.x, agent.fwd.z},
                                            {0.0f, -1.0f});
        const glm::vec2 right{-forward.y, forward.x};
        emit_vehicle(scene, collider, step,
                     traffic_vehicle_identity_hash(agent.lane_key, agent.slot),
                     agent.body_damage, agent.mechanical, agent.pos, right, forward,
                     footprint.half_width_m, footprint.half_length_m);
    }
}

void VehicleEffects::emit_vehicle(
    Scene& scene, const TerrainCollider& collider, uint64_t step,
    uint64_t owner, const VehicleDamageState& damage, const VehicleMechanicalState& mechanical, glm::vec3 centre,
    glm::vec2 right_xz, glm::vec2 forward_xz, float half_width,
    float half_length) {
    right_xz = normal_or(right_xz, {1.0f, 0.0f});
    forward_xz = normal_or(forward_xz, {0.0f, -1.0f});
    for (const VehicleFluidLeak& leak : vehicle_fluid_leaks(damage)) {
        if (!(leak.severity > 0.0f)) continue;
        if ((leak.kind==VehicleFluidKind::Oil && mechanical.oil_remaining<=0.f) ||
            (leak.kind==VehicleFluidKind::Fuel && mechanical.fuel_remaining<=0.f)) continue;
        const int period = std::clamp(static_cast<int>(std::lround(
                                          glm::mix(105.0f, 20.0f,
                                                   leak.severity))),
                                      20, 105);
        const uint64_t channel = static_cast<uint64_t>(leak.kind) + 1u;
        const uint64_t identity = splitmix64_mix(
            owner ^ (channel * 0x9E3779B97F4A7C15ull));
        if ((step + identity % static_cast<uint64_t>(period)) %
                static_cast<uint64_t>(period) !=
            0u) {
            continue;
        }

        const float local_x = leak.local_xz.x * half_width;
        const float local_z = leak.local_xz.y * half_length;
        const glm::vec2 position = {centre.x, centre.z};
        // Physics local -Z is forward, hence the minus on local_z.
        const glm::vec2 source = position + right_xz * local_x -
                                 forward_xz * local_z;
        emit_mark(scene, collider, step, owner, leak, source, centre.y);
    }
}

void VehicleEffects::emit_mark(Scene& scene, const TerrainCollider& collider,
                               uint64_t step, uint64_t owner,
                               const VehicleFluidLeak& leak,
                               glm::vec2 position_xz, float source_y) {
    const uint64_t h = splitmix64_mix(
        owner ^ (step * 0xD6E8FEB86659FD93ull) ^
        static_cast<uint64_t>(leak.kind));
    const float variation = 0.84f +
        static_cast<float>((h >> 24u) & 0xFFFFu) / 65535.0f * 0.32f;
    const float new_radius = (0.12f + leak.severity * 0.25f) * variation;

    // A stopped wreck grows one puddle instead of stacking coplanar discs.
    for (auto it = marks_.rbegin(); it != marks_.rend(); ++it) {
        Mark& mark = *it;
        if (mark.owner != owner || mark.kind != leak.kind) continue;
        if (glm::distance(mark.position_xz, position_xz) >
            std::max(0.16f, mark.radius * 0.72f)) {
            continue;
        }
        mark.radius = std::min(0.72f,
                               std::sqrt(mark.radius * mark.radius +
                                         new_radius * new_radius * 0.32f));
        mark.expires_step = step + kMarkLifetimeSteps;
        // A continuously leaking wreck may have made this puddle before snow
        // accumulated or before a developer depth change. Keep the same XZ
        // and rotation, but move the active stain onto the current physical
        // snow surface when it grows.
        const VehicleFluidMarkPlacement placement =
            vehicle_fluid_mark_placement(
                collider, mark.position_xz, source_y);
        mark.surface_position = placement.position;
        mark.surface_normal = placement.normal;
        Transform transform;
        transform.position = mark.surface_position;
        const glm::vec2 spread = fluid_spread(mark.kind);
        transform.scale = {mark.radius * spread.x, 1.0f,
                           mark.radius * spread.y};
        transform.rotation = vehicle_fluid_mark_rotation(
            mark.surface_normal, mark.spin_radians);
        scene.set_transform(mark.node, transform);
        return;
    }

    if (marks_.size() >= kMaxMarks) {
        scene.remove(marks_.front().node);
        marks_.erase(marks_.begin());
    }

    Renderable renderable;
    renderable.mesh = mesh_;
    renderable.material = material_;
    renderable.tint = fluid_color(leak.kind);
    const VehicleFluidMarkPlacement placement = vehicle_fluid_mark_placement(
        collider, position_xz, source_y);
    const float spin_radians =
        static_cast<float>(h & 0xFFFFu) / 65535.0f * 3.14159265f;
    const glm::vec2 spread = fluid_spread(leak.kind);
    Transform transform;
    transform.position = placement.position;
    transform.scale = {new_radius * spread.x, 1.0f,
                       new_radius * spread.y};
    transform.rotation = vehicle_fluid_mark_rotation(
        placement.normal, spin_radians);
    const NodeId node = scene.create(renderable, transform, bounds_);
    if (SceneNode* scene_node = scene.get(node))
        scene_node->max_draw_distance = 140.0f;
    marks_.push_back(
        {node, owner, leak.kind, position_xz, placement.position,
         placement.normal, spin_radians, new_radius,
         step + kMarkLifetimeSteps});
}

void VehicleEffects::destroy(Scene& scene) {
    for (const Mark& mark : marks_) scene.remove(mark.node);
    marks_.clear();
    mesh_ = kInvalidId;
    material_ = kInvalidId;
}

}  // namespace apricot
