#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include <glm/gtc/quaternion.hpp>

#include "core/aabb.h"
#include "core/transform.h"
#include "physics/terrain_collider.h"

namespace apricot {

inline constexpr std::size_t kMaxRoadsideDebrisPieces = 7;
inline constexpr float kRoadsideDebrisLifetimeSeconds = 12.0f;

struct RoadsideDebrisPiece {
    Transform pose{};
    AABB local_bounds{};
    glm::vec3 linear_velocity{0.0f};
    glm::vec3 angular_velocity{0.0f};
    Transform welded_local{};
    std::size_t welded_to = kMaxRoadsideDebrisPieces;
    float still_seconds = 0.0f;
    bool touched_ground = false;
    bool sleeping = false;
};

struct RoadsideDebrisState {
    std::array<RoadsideDebrisPiece, kMaxRoadsideDebrisPieces> pieces{};
    std::size_t piece_count = 0;
    float age_seconds = 0.0f;
    bool active = false;
    bool expired = false;
};

inline RoadsideDebrisPiece make_roadside_debris_piece(
    const Transform& standing, const AABB& local_bounds,
    glm::vec3 impact_velocity, std::size_t piece_index) {
    RoadsideDebrisPiece out;
    out.pose = standing;
    out.local_bounds = local_bounds;

    impact_velocity.y = 0.0f;
    const float speed = glm::length(impact_velocity);
    const glm::vec3 direction = speed > 1e-5f
        ? impact_velocity / speed : glm::vec3{0.0f, 0.0f, 1.0f};
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const glm::vec3 lateral = glm::normalize(glm::cross(up, direction));

    if (piece_index == 0u) {
        // The long pole starts rotating from the car strike instead of being
        // teleported through a canned ninety-degree fall.
        out.linear_velocity = impact_velocity * 0.16f + up * 0.18f;
        out.angular_velocity = glm::cross(up, direction) *
                               std::clamp(speed * 0.22f, 1.4f, 3.2f);
        return out;
    }

    const float side = (piece_index & 1u) == 0u ? 1.0f : -1.0f;
    const float index = static_cast<float>(piece_index);
    out.linear_velocity = impact_velocity * (0.12f + index * 0.012f) +
        direction * (0.55f + index * 0.10f) +
        lateral * side * (0.70f + index * 0.12f) +
        up * (0.65f + index * 0.12f);
    out.angular_velocity =
        (lateral * side + direction * 0.35f + up * (0.30f * side)) *
        (1.8f + index * 0.42f);
    return out;
}

template <std::size_t N>
inline void start_roadside_debris(
    RoadsideDebrisState& state,
    const std::array<Transform, N>& standing,
    const std::array<AABB, N>& local_bounds,
    glm::vec3 impact_velocity) {
    static_assert(N <= kMaxRoadsideDebrisPieces);
    state = {};
    state.active = true;
    state.piece_count = N;
    for (std::size_t i = 0; i < N; ++i) {
        state.pieces[i] = make_roadside_debris_piece(
            standing[i], local_bounds[i], impact_velocity, i);
    }
}

inline bool weld_roadside_debris_piece(RoadsideDebrisState& state,
                                       std::size_t child,
                                       std::size_t parent) {
    if (child >= state.piece_count || parent >= state.piece_count ||
        child == parent) return false;
    auto& piece = state.pieces[child];
    piece.welded_to = parent;
    piece.welded_local = combine(state.pieces[parent].pose.inverse(),
                                 piece.pose);
    return true;
}

inline void integrate_roadside_debris_piece(RoadsideDebrisPiece& piece,
                                             float dt) {
    if (piece.sleeping || !(dt > 0.0f)) return;
    // Mesh origins sit at useful authoring points (the foot of a pole, for
    // example), not necessarily at the loose piece's centre of mass. Preserve
    // the bounds centre while rotating so a detached sign face does not orbit
    // its former mounting point and appear to hang in the air.
    const glm::vec3 centre_before = piece.pose.transform_point(
        piece.local_bounds.center());
    piece.linear_velocity.y -= 9.81f * dt;
    piece.linear_velocity *= std::exp(-0.08f * dt);
    piece.angular_velocity *= std::exp(-0.30f * dt);
    const glm::vec3 centre_after_translation = centre_before +
        piece.linear_velocity * dt;

    const glm::quat spin{0.0f, piece.angular_velocity.x,
                         piece.angular_velocity.y,
                         piece.angular_velocity.z};
    const glm::quat rotated = piece.pose.rotation +
                              (spin * piece.pose.rotation) * (0.5f * dt);
    const float length2 = glm::dot(rotated, rotated);
    if (length2 > 1e-8f) piece.pose.rotation = rotated / std::sqrt(length2);
    piece.pose.position += centre_after_translation -
        piece.pose.transform_point(piece.local_bounds.center());
}

inline bool roadside_debris_resting_orientation(
    const RoadsideDebrisPiece& piece, glm::vec3 support_normal) {
    const float normal_length2 = glm::dot(support_normal, support_normal);
    if (!piece.local_bounds.valid() || normal_length2 < 1e-8f) return false;
    support_normal /= std::sqrt(normal_length2);

    const glm::vec3 dimensions = piece.local_bounds.size() *
                                 glm::abs(piece.pose.scale);
    const std::array<float, 3> dimension{
        dimensions.x, dimensions.y, dimensions.z};
    const std::array<glm::vec3, 3> axes{
        piece.pose.rotate({1.0f, 0.0f, 0.0f}),
        piece.pose.rotate({0.0f, 1.0f, 0.0f}),
        piece.pose.rotate({0.0f, 0.0f, 1.0f})};
    std::array<std::size_t, 3> order{0u, 1u, 2u};
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return dimension[a] < dimension[b];
    });
    const float shortest = dimension[order[0]];
    const float middle = dimension[order[1]];
    const float longest = dimension[order[2]];

    // Rods rest with their long axis in the support plane. Thin sign faces
    // rest broad-side-down. These tight tolerances prevent a six-metre pole
    // from being declared asleep with its far end visibly off the pavement.
    if (longest > middle * 2.5f)
        return std::fabs(glm::dot(axes[order[2]], support_normal)) < 0.035f;
    if (middle > std::max(shortest, 0.001f) * 2.5f)
        return std::fabs(glm::dot(axes[order[0]], support_normal)) > 0.992f;
    return true;
}

inline bool resolve_roadside_debris_ground(
    RoadsideDebrisPiece& piece, const TerrainCollider& collider, float dt) {
    if (piece.sleeping || !piece.local_bounds.valid()) return false;

    float deepest = 0.0f;
    glm::vec3 contact_normal{0.0f, 1.0f, 0.0f};
    glm::vec3 contact_local{0.0f};
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 local{
            (corner & 1) ? piece.local_bounds.max.x : piece.local_bounds.min.x,
            (corner & 2) ? piece.local_bounds.max.y : piece.local_bounds.min.y,
            (corner & 4) ? piece.local_bounds.max.z : piece.local_bounds.min.z};
        const glm::vec3 world = piece.pose.transform_point(local);
        const auto support = collider.probe_down(
            world + glm::vec3{0.0f, 0.30f, 0.0f}, 2.0f);
        if (!support.hit) continue;
        const float penetration = support.point.y - world.y;
        if (penetration > deepest) {
            deepest = penetration;
            contact_normal = support.normal;
            contact_local = local;
        }
    }
    if (!(deepest > 0.0f)) return false;

    // The collider reports a vertical penetration depth. Lift vertically so a
    // long rotated pole cannot retain one end beneath a road or sidewalk.
    piece.pose.position.y += deepest + 0.003f;
    const float normal_speed = glm::dot(piece.linear_velocity, contact_normal);
    if (normal_speed < 0.0f) {
        piece.linear_velocity -= contact_normal * (1.24f * normal_speed);
    }
    const glm::vec3 normal_motion = contact_normal *
                                    glm::dot(piece.linear_velocity,
                                             contact_normal);
    piece.linear_velocity = normal_motion +
                            (piece.linear_velocity - normal_motion) * 0.62f;
    const bool can_settle = roadside_debris_resting_orientation(
        piece, contact_normal);
    if (!can_settle) {
        // A single touching corner is a pivot, not stable support. The upward
        // reaction at that corner supplies the missing torque which finishes
        // tipping poles and knocks sign plates off their edges.
        const glm::vec3 contact = piece.pose.transform_point(contact_local);
        const glm::vec3 centre = piece.pose.transform_point(
            piece.local_bounds.center());
        const glm::vec3 lever = contact - centre;
        const float lever2 = std::max(glm::dot(lever, lever), 0.04f);
        piece.angular_velocity += glm::cross(lever, contact_normal) *
                                  (9.81f * dt / lever2);
    }
    piece.angular_velocity *= can_settle ? 0.58f : 0.995f;
    piece.touched_ground = true;

    if (can_settle && glm::length(piece.linear_velocity) < 0.22f &&
        glm::length(piece.angular_velocity) < 0.32f) {
        piece.still_seconds += dt;
        if (piece.still_seconds >= 0.42f) {
            piece.sleeping = true;
            piece.linear_velocity = glm::vec3{0.0f};
            piece.angular_velocity = glm::vec3{0.0f};
        }
    } else {
        piece.still_seconds = 0.0f;
    }
    return true;
}

inline void step_roadside_debris(RoadsideDebrisState& state,
                                 const TerrainCollider& collider, float dt) {
    if (!state.active || state.expired || !(dt > 0.0f)) return;
    state.age_seconds += dt;
    if (state.age_seconds >= kRoadsideDebrisLifetimeSeconds) {
        state.active = false;
        state.expired = true;
        return;
    }
    for (std::size_t i = 0; i < state.piece_count; ++i) {
        auto& piece = state.pieces[i];
        if (piece.welded_to < state.piece_count) continue;
        integrate_roadside_debris_piece(piece, dt);
        resolve_roadside_debris_ground(piece, collider, dt);
    }
    for (std::size_t i = 0; i < state.piece_count; ++i) {
        auto& piece = state.pieces[i];
        if (piece.welded_to >= state.piece_count) continue;
        const auto& parent = state.pieces[piece.welded_to];
        piece.pose = combine(parent.pose, piece.welded_local);
        piece.linear_velocity = parent.linear_velocity;
        piece.angular_velocity = parent.angular_velocity;
        piece.still_seconds = parent.still_seconds;
        piece.touched_ground = parent.touched_ground;
        piece.sleeping = parent.sleeping;
    }
}

}  // namespace apricot
