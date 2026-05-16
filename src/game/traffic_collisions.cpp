// traffic_collisions.cpp — car-vs-car and player/parked physics integration
// for TrafficSystem.
//
// Owns: resolve_vehicle_collisions (XZ-OBB SAT against every car pair with
// impulse exchange) and integrate_player_or_parked (the rigid-body substep
// driver). Extracted from traffic.cpp in PBD-007 commit 4.
//
// The OBB helpers (OBBxz struct, make_obb_xz, obb_xz_intersect) are used only
// by this file and stay in its anon namespace.

#include "game/traffic.h"
#include "game/traffic_internal.h"

#include "game/traffic_ai.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "physics/world_collision.h"

namespace pengine {

namespace {

// =============================================================================
// 2D OBB intersection (XZ plane). Cars rotate primarily around Y, so a 2D OBB
// per car is accurate without paying for full 3D SAT.
// =============================================================================

struct OBBxz {
    glm::vec2 center;
    glm::vec2 half_ext;   // along body's local +X and +Z respectively
    glm::vec2 ax_x;       // unit, body +X projected to XZ
    glm::vec2 ax_z;       // unit, body +Z projected to XZ
};

OBBxz make_obb_xz(const Vehicle& v) {
    OBBxz o;
    glm::vec3 ax = v.right();        // body +X
    glm::vec3 az = -v.forward();     // body +Z (forward() is body -Z)
    // Footprint half-extents from the visual mesh's body-local AABB so
    // car-vs-car contact matches what the player sees, not the chassis box.
    // The AABB is generally NOT centred on the body origin (the rendered
    // mesh sits a bit forward/back of body+0 depending on how the OBJ
    // was authored), so the OBB centre is the world-space transform of
    // the AABB centre — using body.position would shift the rectangle
    // off the visible body and cause a phantom gap.
    glm::vec3 vmin   = v.visual_aabb_min_local();
    glm::vec3 vmax   = v.visual_aabb_max_local();
    glm::vec3 vctr   = (vmin + vmax) * 0.5f;
    glm::vec3 wctr   = v.position() + v.orientation() * vctr;
    o.center     = {wctr.x, wctr.z};
    o.half_ext   = {(vmax.x - vmin.x) * 0.5f,
                    (vmax.z - vmin.z) * 0.5f};
    glm::vec2 ax2{ax.x, ax.z};
    glm::vec2 az2{az.x, az.z};
    float lx = glm::length(ax2);
    float lz = glm::length(az2);
    o.ax_x = lx > 1e-6f ? ax2 / lx : glm::vec2{1.f, 0.f};
    o.ax_z = lz > 1e-6f ? az2 / lz : glm::vec2{0.f, 1.f};
    return o;
}

// SAT against the four candidate axes. Returns true on overlap, with
// `out_normal` pointing from b toward a and `out_depth` = penetration depth
// along that normal.
bool obb_xz_intersect(const OBBxz& a, const OBBxz& b,
                       glm::vec2& out_normal, float& out_depth) {
    const glm::vec2 axes[4] = {a.ax_x, a.ax_z, b.ax_x, b.ax_z};
    glm::vec2 d = a.center - b.center;

    float min_overlap = std::numeric_limits<float>::max();
    glm::vec2 min_axis{1.f, 0.f};

    for (int i = 0; i < 4; ++i) {
        glm::vec2 n = axes[i];
        float a_proj = std::abs(glm::dot(a.ax_x * a.half_ext.x, n))
                     + std::abs(glm::dot(a.ax_z * a.half_ext.y, n));
        float b_proj = std::abs(glm::dot(b.ax_x * b.half_ext.x, n))
                     + std::abs(glm::dot(b.ax_z * b.half_ext.y, n));
        float dist   = std::abs(glm::dot(d, n));
        float overlap = a_proj + b_proj - dist;
        if (overlap <= 0.f) return false;       // separating axis — done
        if (overlap < min_overlap) {
            min_overlap = overlap;
            min_axis    = n;
        }
    }
    if (glm::dot(d, min_axis) < 0.f) min_axis = -min_axis;
    out_normal = min_axis;
    out_depth  = min_overlap;
    return true;
}

} // anonymous namespace

void TrafficSystem::resolve_vehicle_collisions() {
    if (cars_.size() < 2) return;

    constexpr int   MAX_ITER    = 4;
    constexpr float SLOP        = 0.005f;
    constexpr float RESTITUTION = 0.2f;     // mostly inelastic — cars don't bounce off each other much

    for (int iter = 0; iter < MAX_ITER; ++iter) {
        bool any = false;

        for (std::size_t i = 0; i < cars_.size(); ++i) {
            for (std::size_t j = i + 1; j < cars_.size(); ++j) {
                Car& a = *cars_[i];
                Car& b = *cars_[j];

                OBBxz oa = make_obb_xz(a.vehicle);
                OBBxz ob = make_obb_xz(b.vehicle);

                glm::vec2 n2; float depth;
                if (!obb_xz_intersect(oa, ob, n2, depth)) continue;
                any = true;

                // Inflate normal to 3D (Y = 0 — XZ-plane resolution only).
                glm::vec3 n{n2.x, 0.f, n2.y};
                float push = depth + SLOP;

                // If a dynamic car hits an AI, promote that AI to Parked so
                // it picks up real physics for the impact. We flag it for
                // recovery (try_ai_recover) so once the chassis has settled
                // — upright, slowed, still near its assigned lane — it
                // snaps back onto the lane and resumes AI driving. Cars
                // that end up flipped or knocked far off the road simply
                // never satisfy the recovery conditions and stay parked.
                // We deliberately do NOT promote on AI ↔ AI contact — that
                // would turn every traffic jam into a pile of inert wrecks.
                bool a_was_ai = (a.driver == Driver::AI);
                bool b_was_ai = (b.driver == Driver::AI);
                if (a_was_ai && !b_was_ai) {
                    a.driver = Driver::Parked;
                    a.ai_state = TrafficAgentState::PhysicsFallback;
                    a.vehicle.set_inputs(0.f, 0.f, 0.f, false);
                    a.ai_recovery_pending = true;
                    a.ai_recovery_timer   = 0.f;
                }
                if (b_was_ai && !a_was_ai) {
                    b.driver = Driver::Parked;
                    b.ai_state = TrafficAgentState::PhysicsFallback;
                    b.vehicle.set_inputs(0.f, 0.f, 0.f, false);
                    b.ai_recovery_pending = true;
                    b.ai_recovery_timer   = 0.f;
                }

                bool a_dyn = (a.driver != Driver::AI);
                bool b_dyn = (b.driver != Driver::AI);

                if (a_dyn && b_dyn) {
                    // Both dynamic — separate proportional to inverse mass
                    // and exchange momentum along the contact normal.
                    // Position correction: translate apart by inverse-mass weights.
                    float ma = a.vehicle.body().mass;
                    float mb = b.vehicle.body().mass;
                    float mt = ma + mb;
                    a.vehicle.translate( n * (push * (mb / mt)));
                    b.vehicle.translate(-n * (push * (ma / mt)));

                    // Contact point: midpoint between the two visual centres
                    // in XZ. The Y is interpolated between bumper height and
                    // CoM height based on how head-on the hit is. Below-CoM
                    // contacts are load-bearing for SIDE hits — the r×n arm
                    // produces a roll torque so a fast T-bone can flip a car
                    // instead of just shoving it. But the same arm on a
                    // longitudinal hit (rear-end / head-on) becomes pure
                    // PITCH torque, which lifts the rear of the impacting car
                    // off the ground in a way that takes too long to settle.
                    // Solution: scale the bumper drop by min(longitudinality_a,
                    // longitudinality_b), where longitudinality is |n·body_z|.
                    // A pure rear-end (both cars longitudinal) → contact at
                    // CoM-Y, no pitch torque. A T-bone (one car broadside) →
                    // min is low, contact stays at bumper-Y so the side-hit
                    // car still rolls.
                    glm::vec3 ap = a.vehicle.position();
                    glm::vec3 bp = b.vehicle.position();
                    glm::vec3 a_min = a.vehicle.visual_aabb_min_local();
                    glm::vec3 b_min = b.vehicle.visual_aabb_min_local();
                    glm::vec3 n_local_a = glm::inverse(a.vehicle.orientation()) * n;
                    glm::vec3 n_local_b = glm::inverse(b.vehicle.orientation()) * n;
                    float long_a = std::abs(n_local_a.z);
                    float long_b = std::abs(n_local_b.z);
                    float drop_scale = 1.f - std::min(long_a, long_b);
                    glm::vec3 contact{
                        (ap.x + bp.x) * 0.5f,
                        ((ap.y + a_min.y * drop_scale)
                         + (bp.y + b_min.y * drop_scale)) * 0.5f,
                        (ap.z + bp.z) * 0.5f};

                    // Use point velocities (CoM linear + angular×r), not pure
                    // linear, so a spinning car contributes its tangential
                    // velocity at the contact correctly.
                    glm::vec3 v_a_pt = a.vehicle.body().point_velocity(contact);
                    glm::vec3 v_b_pt = b.vehicle.body().point_velocity(contact);
                    glm::vec3 v_rel  = v_a_pt - v_b_pt;
                    float v_n = glm::dot(v_rel, n);
                    if (v_n < 0.f) {
                        // Effective inverse mass at the contact accounts for
                        // both linear and rotational compliance. Without the
                        // (r×n)·I⁻¹(r×n) terms an off-centre impulse over-
                        // bounces because we'd use the linear-only formula
                        // while routing energy into rotation.
                        glm::vec3 ra = contact - ap;
                        glm::vec3 rb = contact - bp;
                        glm::vec3 ra_x_n = glm::cross(ra, n);
                        glm::vec3 rb_x_n = glm::cross(rb, n);
                        glm::mat3 inv_I_a = a.vehicle.body().inv_inertia_world();
                        glm::mat3 inv_I_b = b.vehicle.body().inv_inertia_world();
                        float k =  1.f / ma + 1.f / mb
                                + glm::dot(ra_x_n, inv_I_a * ra_x_n)
                                + glm::dot(rb_x_n, inv_I_b * rb_x_n);
                        float jmag = -(1.f + RESTITUTION) * v_n / k;
                        glm::vec3 imp = n * jmag;
                        a.vehicle.apply_impulse_at( imp, contact);
                        b.vehicle.apply_impulse_at(-imp, contact);
                    }
                } else {
                    // Both AI — split the push 50/50. Both will re-stamp from
                    // their lane scripts next frame; this just avoids a frame
                    // of overlap and prevents wedging.
                    a.vehicle.translate( n * (push * 0.5f));
                    b.vehicle.translate(-n * (push * 0.5f));
                }
            }
        }

        if (!any) break;
    }
}

void TrafficSystem::integrate_player_or_parked(Car& c, float dt,
                                                const WorldCollision& world) {
    // Reset the per-frame VFX scratch (chassis-on-ground contact points).
    // Substep appends to it; the renderer drains it each frame to spawn
    // sparks.
    c.vehicle.clear_scrape_contacts();
    // Two substeps at half-dt to match the player's old cadence.
    c.vehicle.substep(dt * 0.5f, world);
    c.vehicle.substep(dt * 0.5f, world);

    if (assets_->wheel_visible_radius > 1e-4f) {
        float v = glm::dot(c.vehicle.forward(), c.vehicle.body().linear_vel);
        c.wheel_spin_rad += v * dt / assets_->wheel_visible_radius;
    }
}

} // namespace pengine
