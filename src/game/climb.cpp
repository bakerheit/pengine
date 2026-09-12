#include "game/climb.h"

#include <algorithm>

#include "game/character.h"
#include "physics/terrain_collider.h"

namespace apricot {
namespace {

// Height of whatever the character could stand on at this XZ, searched from
// above so the top face of a wall is found rather than the ground beneath it.
// probe_down() only ever reports a box's TOP face, which is exactly the
// property that makes "what is the ledge here" a single query.
float surface_top(const TerrainCollider& collider, glm::vec2 xz,
                  float from_y, float down_to_y) {
    const float span = std::max(from_y - down_to_y, 0.01f);
    const TerrainCollider::GroundHit hit =
        collider.probe_down({xz.x, from_y, xz.y}, span);
    return hit.hit ? hit.point.y : collider.height(xz.x, xz.y);
}

}  // namespace

ClimbPlan plan_climb(const TerrainCollider& collider,
                     const PlayerCharacterState& state,
                     const CharacterTuning& character,
                     const ClimbTuning& tuning) {
    ClimbPlan plan;
    plan.start = state.position;
    if (!state.grounded) return plan;

    const glm::vec3 forward = character_forward(state.facing_yaw);
    const glm::vec2 heading{forward.x, forward.z};
    const glm::vec2 feet{state.position.x, state.position.z};
    // Search from above the tallest climbable ledge, so a wall taller than the
    // animation can reach is FOUND and then rejected on height. Probing from
    // head height instead would miss it entirely and silently fall through to
    // "no obstacle", which is a different answer with the same shape.
    const float ceiling = state.position.y + tuning.max_height_m + character.height_m;
    const float floor_y = state.position.y - tuning.max_drop_m;

    // Walk outward until the ground rises. The first rise is the face; starting
    // at the body radius keeps the character's own footing out of the search.
    const float step = 0.06f;
    float face_distance = 0.0f;
    float ledge_y = 0.0f;
    for (float d = character.radius_m; d <= tuning.reach_m + 1e-4f; d += step) {
        const glm::vec2 at = feet + heading * d;
        const float top = surface_top(collider, at, ceiling, floor_y);
        const float rise = top - state.position.y;
        if (rise < tuning.min_height_m) continue;
        if (rise > tuning.max_height_m) return plan;  // too tall to reach
        face_distance = d;
        ledge_y = top;
        break;
    }
    if (face_distance <= 0.0f) return plan;

    // Standing room ON the obstacle. character_position_clear() ignores solids
    // whose top is at or below the feet, so the wall being climbed does not
    // count as its own blocker -- only something stacked above it does.
    const glm::vec2 ledge_xz = feet + heading * (face_distance + tuning.ledge_depth_m);
    const glm::vec3 ledge{ledge_xz.x, ledge_y, ledge_xz.y};
    if (!character_position_clear(collider, ledge, character)) return plan;

    // Where the feet actually come to rest. A wide wall keeps you on top; a
    // fence drops you onto the far side. Both are the same query one step on.
    const glm::vec2 finish_xz = feet + heading * (face_distance + tuning.ledge_depth_m * 2.0f);
    const float finish_top = surface_top(
        collider, finish_xz, ledge_y + character.height_m, ledge_y - tuning.max_drop_m);
    if (finish_top > ledge_y + character.max_step_m) return plan;  // it keeps going up
    if (finish_top < ledge_y - tuning.max_drop_m) return plan;     // nothing to land on

    const glm::vec3 finish{finish_xz.x, finish_top, finish_xz.y};
    if (!character_position_clear(collider, finish, character)) return plan;

    plan.ledge = ledge;
    plan.finish = finish;
    plan.height_m = ledge_y - state.position.y;
    const float climbed = plan.height_m +
        glm::length(glm::vec2{finish.x - state.position.x, finish.z - state.position.z});
    plan.duration_s = std::max(tuning.min_duration_s, climbed / tuning.speed_mps);
    plan.possible = true;
    return plan;
}

glm::vec3 climb_position(const ClimbPlan& plan, float progress) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    // THREE SEGMENTS THAT DO NOT OVERLAP: straight up, across at full height,
    // then straight down. The phases are separated on purpose and the seam
    // between any two of them is where this goes wrong.
    //
    // Two earlier versions blended them for smoothness and both dragged the
    // body through the wall. Moving forward while still rising clips the top;
    // and so does starting the descent at the ledge, because the ledge point
    // sits only ledge_depth past the FACE and the body has a radius -- for a
    // thin fence that is fine and for a thick wall the shoulders are still
    // inside it. Descending only once the feet are at plan.finish is the one
    // version that needs no reasoning about thickness, because plan_climb()
    // has already proved the character fits standing there.
    //
    // It is a touch mechanical in isolation; the clip plays over the top of it
    // and carries the read. Pinned by the arc sampling in climb_tests.cpp,
    // which catches this where the endpoints cannot -- both ends are legal in
    // every broken version above.
    const float rise = std::clamp(t / 0.45f, 0.0f, 1.0f);
    const float cross = std::clamp((t - 0.45f) / 0.37f, 0.0f, 1.0f);
    const float drop = std::clamp((t - 0.82f) / 0.18f, 0.0f, 1.0f);
    const float rise_s = rise * rise * (3.0f - 2.0f * rise);
    const float cross_s = cross * cross * (3.0f - 2.0f * cross);
    const float drop_s = drop * drop * (3.0f - 2.0f * drop);

    const glm::vec3 from = plan.start;
    const glm::vec3 to = plan.finish;
    // Clear the higher of the ledge and the landing before travelling at all.
    const float top = std::max(plan.ledge.y, to.y);
    glm::vec3 out;
    out.x = from.x + (to.x - from.x) * cross_s;
    out.z = from.z + (to.z - from.z) * cross_s;
    out.y = from.y + (top - from.y) * rise_s;
    // Settle onto whatever the far side turned out to be -- which may be below
    // where the climb started, when the drop beyond is deeper than the wall is
    // tall -- only after the feet are over the landing.
    out.y = out.y + (to.y - out.y) * drop_s;
    return out;
}

}  // namespace apricot
