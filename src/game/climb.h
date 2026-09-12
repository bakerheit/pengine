#pragma once

#include <glm/glm.hpp>

namespace apricot {

// Forward declared rather than included: PlayerCharacterState CARRIES a
// ClimbPlan, so character.h includes this header and this header must not
// include it back.
struct PlayerCharacterState;
struct CharacterTuning;
class TerrainCollider;

// Climbing a low wall or a fence, GTA-style: walk at it, press jump, and if the
// thing in front of you is short enough to reach, has standing room on top and
// somewhere to land beyond it, you go over instead of bouncing off it.
//
// THIS IS A PLAN, NOT A MOVE. plan_climb() only answers "can this be climbed,
// and where would it put me"; it never touches the character. The traverse is
// then a deterministic function of one scalar, climb_position(), so a climb
// replays from the plan alone and a headless test can assert the whole arc
// without stepping a frame.
//
// WHY IT PROBES RATHER THAN READS BOXES. An earlier sketch walked
// collider.static_boxes() looking for a wall. That sees authored prop boxes and
// nothing else: it is blind to terrain, to authored ground slabs and to the
// baked road surface, so a kerb or a raised planter was unclimbable while a
// crate beside it worked. probe_down() is the same query the character's own
// support test uses, so anything the player can stand on is something the
// player can climb, by construction.
//
// THE CLEARANCE TESTS REUSE character_position_clear() ON PURPOSE. "Has no top"
// and "nothing directly behind it" are both really one question -- could the
// character stand here -- and that question already has exactly one answer in
// this engine. Asking it a second way is how the climb starts disagreeing with
// the walk that follows it, and the symptom is a character who lands inside a
// wall having passed every check.

struct ClimbTuning {
    // Below this you already walk up it: CharacterTuning::max_step_m is 0.34,
    // and a climb that fires on a kerb reads as a stumble.
    float min_height_m = 0.40f;
    // The reach of the animation, not a guess. The cooked climb clip lifts the
    // hips 1.19 m; asking it to cover more leaves the hands in mid-air.
    float max_height_m = 1.30f;
    // How far ahead of the body the obstacle may be and still be grabbed.
    float reach_m = 0.80f;
    // How far past the face to look for footing, and how far to step out.
    float ledge_depth_m = 0.55f;
    // A drop the far side may fall away by and still count as a landing.
    float max_drop_m = 2.20f;
    // Traverse speed. Duration comes from the distance actually covered, so a
    // waist-high rail is quicker than a chest-high wall without a second number.
    float speed_mps = 2.05f;
    float min_duration_s = 0.42f;
};

struct ClimbPlan {
    bool possible = false;
    glm::vec3 start{0.0f};    // feet where the climb was requested
    glm::vec3 ledge{0.0f};    // feet standing on top of the obstacle
    glm::vec3 finish{0.0f};   // feet where the climb hands back control
    float height_m = 0.0f;    // ledge.y - start.y
    float duration_s = 0.0f;
    // How far through the traverse, in seconds. Lives here rather than beside
    // it so the whole climb is ONE value on PlayerCharacterState: a climb that
    // is half state and half plan is a climb that can be restored half-done.
    float elapsed_s = 0.0f;

    bool running() const { return possible && elapsed_s < duration_s; }
};

// Pure. Never mutates the collider and never reads a clock.
ClimbPlan plan_climb(const TerrainCollider& collider,
                     const PlayerCharacterState& state,
                     const CharacterTuning& character,
                     const ClimbTuning& tuning = {});

// Where the feet are `progress` of the way through the plan, progress in [0,1].
// Vertical first, then across: a body that translates straight at the ledge
// clips its own shins through the wall it is climbing.
glm::vec3 climb_position(const ClimbPlan& plan, float progress);

}  // namespace apricot
