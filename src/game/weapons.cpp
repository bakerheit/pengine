#include "game/weapons.h"

#include <algorithm>
#include <limits>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/log.h"
#include "game/pedestrian.h"
#include "game/traffic.h"
#include "game/vehicle.h"
#include "physics/world_collision.h"
#include "render/shader.h"

namespace pengine {

namespace {

constexpr float MAX_RANGE = 200.f;

// Ray vs axis-aligned box (slab test). Returns true if the ray hits the box
// at a positive t, with `out_t` set to the entry distance (or 0 if origin
// is inside). dir need not be normalised.
bool ray_vs_aabb(const glm::vec3& origin, const glm::vec3& dir,
                 const glm::vec3& mn, const glm::vec3& mx,
                 float& out_t) {
    constexpr float INF = std::numeric_limits<float>::infinity();
    const glm::vec3 inv{
        dir.x != 0.f ? 1.f / dir.x : INF,
        dir.y != 0.f ? 1.f / dir.y : INF,
        dir.z != 0.f ? 1.f / dir.z : INF,
    };
    const glm::vec3 t1 = (mn - origin) * inv;
    const glm::vec3 t2 = (mx - origin) * inv;
    const glm::vec3 tmin3 = glm::min(t1, t2);
    const glm::vec3 tmax3 = glm::max(t1, t2);
    const float tmin = std::max({tmin3.x, tmin3.y, tmin3.z});
    const float tmax = std::min({tmax3.x, tmax3.y, tmax3.z});
    if (tmax < 0.f || tmin > tmax) return false;
    out_t = tmin >= 0.f ? tmin : tmax;
    return out_t > 0.f;
}

}  // namespace

bool Weapons::init(const std::string& assets_root) {
    if (!load_static_emesh(assets_root + "/models/weapons/glock17.emesh",
                            gun_mesh_)) {
        PE_WARN("Glock 17 mesh failed to load; equip will be invisible");
    } else {
        glm::vec3 mn = gun_mesh_.bounds_min();
        glm::vec3 mx = gun_mesh_.bounds_max();
        PE_INFO("Glock bounds min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) "
                "size=(%.3f,%.3f,%.3f)",
                mn.x, mn.y, mn.z, mx.x, mx.y, mx.z,
                mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
    }

    // Baked grip offset from live-tuning (rotation hotkeys [/]/;/' /,/.
    // then translation hotkeys with the same keys). Composition
    // (innermost-first on gun-local vertices):
    //   T(0, 5, -85) cm — slide along gun's local Z so grip lands in palm.
    //   R_z(-90)            — final roll to seat the grip.
    //   R_y(-180)           — face barrel forward.
    //   R_x(-90)            — stand the gun upright (grip down).
    gun_grip_offset_ = glm::rotate(glm::mat4{1.f}, glm::radians(-90.f),
                                    glm::vec3{1.f, 0.f, 0.f})
                     * glm::rotate(glm::mat4{1.f}, glm::radians(-180.f),
                                    glm::vec3{0.f, 1.f, 0.f})
                     * glm::rotate(glm::mat4{1.f}, glm::radians(-90.f),
                                    glm::vec3{0.f, 0.f, 1.f})
                     * glm::translate(glm::mat4{1.f},
                                       glm::vec3{0.f, 5.f, -85.f});
    return true;
}

void Weapons::shutdown() {
    gun_mesh_.destroy();
}

Weapons::FireResult Weapons::fire(const glm::vec3& origin, const glm::vec3& dir,
                                  const WorldCollision& world_col,
                                  const TrafficSystem& traffic,
                                  PedestrianSystem& pedestrians) const {
    FireResult r;
    r.origin = origin;

    float     closest_t = MAX_RANGE;
    glm::vec3 hit_point = origin + dir * MAX_RANGE;

    // Static world (terrain + buildings) — bullets don't pass through walls.
    RayHit world_hit = world_col.raycast(origin, dir, closest_t);
    if (world_hit.hit && world_hit.t < closest_t) {
        closest_t = world_hit.t;
        hit_point = world_hit.position;
    }

    // AI cars — occlude only, no damage in phase 1. The chassis box is in
    // local space; we use position ± half-extents as a generous AABB. A few
    // metres of imprecision at car-yaw extremes is fine for occlusion.
    for (const auto& car_ptr : traffic.cars()) {
        const Vehicle& v = car_ptr->vehicle;
        const glm::vec3 half = v.chassis_full_extents * 0.5f;
        // Inflate xz by sqrt(2) so a car turned 45° still occludes its
        // full silhouette.
        constexpr float YAW_INFLATE = 1.4143f;
        const glm::vec3 box_min = v.position()
            - glm::vec3{half.x * YAW_INFLATE, half.y, half.z * YAW_INFLATE};
        const glm::vec3 box_max = v.position()
            + glm::vec3{half.x * YAW_INFLATE, half.y, half.z * YAW_INFLATE};
        float t;
        if (ray_vs_aabb(origin, dir, box_min, box_max, t) && t < closest_t) {
            closest_t = t;
            hit_point = origin + dir * t;
        }
    }

    // Pedestrians — distance-falloff damage. ~3 shots at typical range.
    auto ped_hit = pedestrians.raycast(origin, dir, closest_t);
    if (ped_hit.hit) {
        hit_point = ped_hit.position;
        // Linear falloff: 60 dmg at point-blank → 25 dmg at >=30 m.
        // 100 HP with this curve drops in ~2 shots up close, ~3 shots
        // at urban range (~12 m), ~4 shots at the falloff floor.
        constexpr float DMG_NEAR     = 60.f;
        constexpr float DMG_FAR      = 25.f;
        constexpr float DMG_FALLOFF_M = 30.f;
        float t_far = glm::clamp(ped_hit.t / DMG_FALLOFF_M, 0.f, 1.f);
        float damage = DMG_NEAR * (1.f - t_far) + DMG_FAR * t_far;
        pedestrians.apply_damage(ped_hit.ped_idx, damage, origin);
        r.hit_ped     = true;
        r.heat_delta += 3.0f;
    }

    r.heat_delta += 0.6f;
    r.hit_point   = hit_point;
    return r;
}

void Weapons::render(Shader& lit, const glm::mat4& view_proj,
                     const glm::vec3& cam_pos,
                     const glm::mat4& hand_world_xform) const {
    if (!ready()) return;

    // GUN_SCALE: with PreTransformVertices on, the FBX root node's
    // unit-scale (often 100 for Blender's cm→m export) gets baked
    // into vertex positions, making the gun much bigger than its
    // pre-transform size. Drop the scale until it sits right.
    constexpr float GUN_SCALE = 0.3f;
    glm::mat4 scale_mat = glm::scale(glm::mat4{1.f}, glm::vec3{GUN_SCALE});
    glm::mat4 gun_world = hand_world_xform * scale_mat * gun_grip_offset_;

    lit.use();
    lit.set("u_view_proj",   view_proj);
    lit.set("u_cam_pos",     cam_pos);
    lit.set("u_light_dir",   glm::normalize(glm::vec3{0.6f, 1.f, 0.4f}));
    lit.set("u_light_color", glm::vec3{1.f, 0.95f, 0.85f});
    lit.set("u_ambient",     glm::vec3{0.18f, 0.22f, 0.28f});
    lit.set("u_diffuse",     0);
    lit.set("u_model",       gun_world);
    lit.set("u_normal_mat",
             glm::mat3(glm::inverseTranspose(gun_world)));
    gun_mesh_.draw();
}

} // namespace pengine
