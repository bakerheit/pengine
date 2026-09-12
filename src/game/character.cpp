#include "game/character.h"

#include <algorithm>
#include <cmath>

namespace apricot {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kPitchLimit = 1.20f;

float wrap_angle(float angle) {
    while (angle > kPi) angle -= kTwoPi;
    while (angle < -kPi) angle += kTwoPi;
    return angle;
}

float approach_angle(float from, float to, float max_delta) {
    const float delta = wrap_angle(to - from);
    return wrap_angle(from + std::clamp(delta, -max_delta, max_delta));
}

bool circle_overlaps_box(glm::vec2 centre, float radius, const AABB& box) {
    const glm::vec2 nearest{
        std::clamp(centre.x, box.min.x, box.max.x),
        std::clamp(centre.y, box.min.z, box.max.z),
    };
    const glm::vec2 delta = centre - nearest;
    return glm::dot(delta, delta) < radius * radius;
}

float support_height(const TerrainCollider& collider, glm::vec3 from,
                     glm::vec2 xz, const CharacterTuning& tuning) {
    const float lift = tuning.max_step_m + 0.08f;
    const TerrainCollider::GroundHit hit = collider.probe_down(
        {xz.x, from.y + lift, xz.y}, tuning.max_drop_m + lift);
    return hit.hit ? hit.point.y : collider.height(xz.x, xz.y);
}

}  // namespace

glm::vec3 character_forward(float yaw) {
    return {std::sin(yaw), 0.0f, -std::cos(yaw)};
}

glm::quat character_root_rotation(float yaw) {
    return glm::angleAxis(-yaw, glm::vec3{0.0f, 1.0f, 0.0f});
}

bool character_position_clear(const TerrainCollider& collider,
                              glm::vec3 feet,
                              const CharacterTuning& tuning) {
    for (const StaticBox& solid : collider.static_boxes()) {
        if (!solid.enabled) continue;
        const AABB& box = solid.bounds;
        const bool through_body = box.min.y < feet.y + tuning.height_m &&
                                  box.max.y > feet.y + tuning.max_step_m;
        if (!through_body) continue;
        const glm::vec3 local_feet = solid.local_point(feet);
        if (circle_overlaps_box(
                                {local_feet.x, local_feet.z}, tuning.radius_m,
                                solid.collision_bounds())) {
            return false;
        }
    }
    return true;
}

PlayerCharacterState spawn_character(const TerrainCollider& collider,
                                     float x, float z, float facing_yaw) {
    PlayerCharacterState state;
    state.position = {x, collider.height(x, z), z};
    const TerrainCollider::GroundHit support = collider.probe_down(
        {x, state.position.y + 4.0f, z}, 8.0f);
    if (support.hit) state.position.y = support.point.y;
    state.facing_yaw = wrap_angle(facing_yaw);
    state.view_yaw = state.facing_yaw;
    return state;
}

PlayerCharacterState step_character(const PlayerCharacterState& current,
                                    const CharacterTuning& tuning,
                                    const InputFrame& input,
                                    const TerrainCollider& collider,
                                    float dt,
                                    const ClimbTuning& climb) {
    PlayerCharacterState next = current;
    const float safe_dt = std::clamp(dt, 0.0f, 0.1f);
    next.view_yaw = wrap_angle(current.view_yaw + input.look_dx);
    next.view_pitch = std::clamp(current.view_pitch - input.look_dy,
                                 -kPitchLimit, kPitchLimit);

    if (!(safe_dt > 0.0f)) return next;

    // A CLIMB OWNS THE BODY, AND IT IS HANDLED BEFORE INTENT IS EVEN READ.
    // Locomotion, gravity and the jump are all suspended for its duration and
    // the feet follow the plan that was committed to at the start. Letting the
    // stick keep steering mid-traverse is how a player walks sideways out of a
    // vault and ends up standing inside the wall they were crossing: the plan
    // proved the LANDING was clear, and it cannot vouch for anywhere else.
    if (current.climb.running()) {
        next.climb.elapsed_s = current.climb.elapsed_s + safe_dt;
        const float progress = next.climb.duration_s > 0.0f
            ? next.climb.elapsed_s / next.climb.duration_s
            : 1.0f;
        next.position = climb_position(next.climb, progress);
        next.velocity = glm::vec3{0.0f};
        next.grounded = false;
        if (!next.climb.running()) {
            // Land exactly where plan_climb() proved the character fits, not
            // wherever the last partial step happened to put them.
            next.position = next.climb.finish;
            next.climb = ClimbPlan{};
            next.grounded = true;
        }
        return next;
    }

    const bool jump_pressed = current.grounded && was_pressed(input, kBtnJump);
    if (jump_pressed) {
        const ClimbPlan plan = plan_climb(collider, current, tuning, climb);
        if (plan.possible) {
            next.climb = plan;
            next.velocity = glm::vec3{0.0f};
            next.grounded = false;
            return next;
        }
    }

    glm::vec2 intent{input.steer, input.throttle - input.brake};
    const float intent_length = glm::length(intent);
    if (intent_length > 1.0f) intent /= intent_length;

    const glm::vec3 view_forward = character_forward(next.view_yaw);
    const glm::vec3 view_right{std::cos(next.view_yaw), 0.0f,
                               std::sin(next.view_yaw)};
    glm::vec3 direction = view_right * intent.x + view_forward * intent.y;
    const float direction_length = glm::length(direction);
    if (direction_length > 1e-5f) direction /= direction_length;
    else direction = glm::vec3{0.0f};

    next.sprinting = is_held(input, kBtnShiftUp) && intent_length > 0.25f;
    const float top_speed = next.sprinting ? tuning.sprint_speed_mps
                                           : tuning.walk_speed_mps;
    next.velocity = direction * top_speed * std::min(intent_length, 1.0f);

    // Same press the climb was offered first refusal on: nothing here is
    // climbable, so it is an ordinary jump.
    const bool taking_off = jump_pressed;
    next.grounded = current.grounded && !taking_off;
    next.velocity.y = taking_off ? tuning.jump_speed_mps
        : (current.grounded ? 0.0f : current.velocity.y);

    // In the air even a kerb is a wall until the feet clear its top.
    CharacterTuning body_tuning = tuning;
    if (!next.grounded) body_tuning.max_step_m = 0.0f;
    glm::vec3 moved = current.position;
    glm::vec3 candidate = moved;
    candidate.x += next.velocity.x * safe_dt;
    if (character_position_clear(collider, candidate, body_tuning)) moved.x = candidate.x;

    candidate = moved;
    candidate.z += next.velocity.z * safe_dt;
    if (character_position_clear(collider, candidate, body_tuning)) moved.z = candidate.z;

    if (next.grounded) {
        const float support = support_height(
            collider, current.position, {moved.x, moved.z}, tuning);
        if (std::abs(support - moved.y) <= tuning.max_step_m + 0.001f) {
            glm::vec3 planted{moved.x, support, moved.z};
            if (character_position_clear(collider, planted, tuning)) moved = planted;
            else moved = current.position;
        } else {
            // Walk off a ledge with gravity instead of snapping to the floor.
            next.grounded = false;
        }
    }
    if (!next.grounded) {
        const float old_y = moved.y;
        float target_y = old_y + next.velocity.y * safe_dt
            - 0.5f * tuning.gravity_mps2 * safe_dt * safe_dt;
        next.velocity.y -= tuning.gravity_mps2 * safe_dt;
        if (target_y > old_y) {
            // Sweep the head against ceilings, including rotated solid boxes.
            for (const StaticBox& solid : collider.static_boxes()) {
                if (!solid.enabled) continue;
                const glm::vec3 local = solid.local_point(moved);
                if (!circle_overlaps_box({local.x, local.z}, tuning.radius_m,
                                         solid.collision_bounds())) continue;
                const float ceiling = solid.bounds.min.y - tuning.height_m;
                if (ceiling >= old_y - 0.001f && ceiling < target_y) {
                    target_y = std::max(old_y, ceiling);
                    next.velocity.y = 0.0f;
                }
            }
        } else {
            // No step-height lift here: that would catch roofs ABOVE the feet.
            const auto hit = collider.probe_down(
                {moved.x, old_y + 0.001f, moved.z},
                old_y - target_y + 0.002f);
            float floor = hit.hit ? hit.point.y : collider.height(moved.x, moved.z);
            for (const StaticBox& solid : collider.static_boxes()) {
                if (!solid.enabled || solid.bounds.max.y > old_y + 0.001f) continue;
                const glm::vec3 local = solid.local_point(moved);
                if (circle_overlaps_box({local.x, local.z}, tuning.radius_m,
                                        solid.collision_bounds()))
                    floor = std::max(floor, solid.bounds.max.y);
            }
            if (target_y <= floor && floor <= old_y + 0.001f) {
                target_y = floor;
                next.velocity.y = 0.0f;
                next.grounded = true;
            }
        }
        moved.y = target_y;
    }
    const glm::vec2 travelled{moved.x - current.position.x,
                              moved.z - current.position.z};
    if (next.grounded) next.distance_walked_m += glm::length(travelled);
    next.position = moved;
    if (safe_dt > 0.0f) {
        next.velocity.x = travelled.x / safe_dt;
        next.velocity.z = travelled.y / safe_dt;
    }

    if (glm::dot(travelled, travelled) > 1e-8f) {
        const float target_yaw = std::atan2(travelled.x, -travelled.y);
        next.facing_yaw = approach_angle(
            current.facing_yaw, target_yaw, tuning.turn_speed_rad_s * safe_dt);
    }
    return next;
}

}  // namespace apricot
