// Wall and fence climbing, driven through the REAL collider.
//
// Every case here builds an actual TerrainCollider with actual static boxes and
// asks the real plan_climb(); nothing hand-builds a ClimbPlan. A consumer test
// fed a hand-made plan passes happily while the planner in front of it decides
// a two-metre fence is waist high.
//
// Everything stands on a flat plinth rather than on raw terrain, so a height is
// a number the test chose and not whatever the height field happened to do at
// that coordinate. A case that fails here fails because of the rule under test.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "city/building_access.h"
#include "city/residential_neighborhood.h"
#include "game/character.h"
#include "game/climb.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kPlinthTop = 60.0f;
constexpr float kPi = 3.14159265358979323846f;

struct Yard {
    TerrainCollider collider{7u};

    Yard() {
        // 40 m of dead-flat standing room, thick enough that probe_down() finds
        // its top rather than falling through to the terrain underneath.
        collider.add_static_box({{-20.0f, kPlinthTop - 8.0f, -20.0f},
                                 {20.0f, kPlinthTop, 20.0f}});
    }

    // A wall `height` tall whose near face is at `z`, running across X.
    void wall(float z, float height, float thickness = 0.25f) {
        collider.add_static_box({{-8.0f, kPlinthTop, z},
                                 {8.0f, kPlinthTop + height, z + thickness}});
    }

    // A solid sitting on top of something, for the "has no top" rule.
    void cap(float z, float base, float height, float thickness = 0.25f) {
        collider.add_static_box({{-8.0f, kPlinthTop + base, z},
                                 {8.0f, kPlinthTop + base + height, z + thickness}});
    }
};

// Facing +Z. character_forward(yaw) is {sin yaw, 0, -cos yaw}, so +Z is yaw pi.
PlayerCharacterState walker_at(float z) {
    PlayerCharacterState s;
    s.position = {0.0f, kPlinthTop, z};
    s.facing_yaw = kPi;
    // Movement follows the VIEW, not the body: spawn_character() seeds the two
    // together and a fixture that leaves view_yaw at zero walks the character
    // backwards out of its own test.
    s.view_yaw = s.facing_yaw;
    s.grounded = true;
    return s;
}

const CharacterTuning kCharacter;
const ClimbTuning kClimb;

void climbs_a_low_wall() {
    Yard yard;
    yard.wall(0.0f, 1.0f);
    const ClimbPlan plan =
        plan_climb(yard.collider, walker_at(-0.45f), kCharacter, kClimb);
    REQUIRE(plan.possible);
    REQUIRE_NEAR(plan.height_m, 1.0f, 0.02f);
    REQUIRE_NEAR(plan.ledge.y, kPlinthTop + 1.0f, 0.02f);
    // It must finish on the FAR side of the wall, not on top of it.
    REQUIRE_MSG(plan.finish.z > 0.25f, "climb must end past the wall", "low wall");
    REQUIRE_NEAR(plan.finish.y, kPlinthTop, 0.02f);
    REQUIRE(plan.duration_s > 0.0f);
}

void rejects_a_tall_fence() {
    // The gate the whole feature hangs on: a 2 m chain-link fence is a barrier.
    for (float height : {1.45f, 1.8f, 2.0f, 2.4f}) {
        Yard yard;
        yard.wall(0.0f, height);
        const ClimbPlan plan =
            plan_climb(yard.collider, walker_at(-0.45f), kCharacter, kClimb);
        REQUIRE_MSG(!plan.possible, "fence above reach must not be climbable",
                    "tall fence");
    }
}

void rejects_a_kerb() {
    // Below the gate you already walk up it; a climb here reads as a stumble.
    Yard yard;
    yard.wall(0.0f, 0.22f);
    const ClimbPlan plan =
        plan_climb(yard.collider, walker_at(-0.45f), kCharacter, kClimb);
    REQUIRE_MSG(!plan.possible, "a kerb is walked, not climbed", "kerb");
}

void rejects_a_capped_wall() {
    // "Has no top": a climbable wall with razor wire / an overhang above it.
    Yard yard;
    yard.wall(0.0f, 1.0f);
    yard.cap(0.0f, 1.30f, 0.9f);
    const ClimbPlan plan =
        plan_climb(yard.collider, walker_at(-0.45f), kCharacter, kClimb);
    REQUIRE_MSG(!plan.possible, "no standing room on top", "capped wall");
}

void rejects_an_obstruction_behind() {
    // "No obstacles directly behind it": a second wall right up against it.
    Yard yard;
    yard.wall(0.0f, 1.0f);
    yard.wall(0.45f, 2.2f);
    const ClimbPlan plan =
        plan_climb(yard.collider, walker_at(-0.45f), kCharacter, kClimb);
    REQUIRE_MSG(!plan.possible, "nowhere to land beyond the wall",
                "blocked landing");
}

void needs_to_be_facing_it() {
    Yard yard;
    yard.wall(0.0f, 1.0f);
    PlayerCharacterState away = walker_at(-0.45f);
    away.facing_yaw = 0.0f;  // facing -Z, away from the wall
    REQUIRE(!plan_climb(yard.collider, away, kCharacter, kClimb).possible);
}

void needs_to_be_grounded() {
    Yard yard;
    yard.wall(0.0f, 1.0f);
    PlayerCharacterState airborne = walker_at(-0.45f);
    airborne.grounded = false;
    REQUIRE(!plan_climb(yard.collider, airborne, kCharacter, kClimb).possible);
}

void out_of_reach_is_not_climbable() {
    Yard yard;
    yard.wall(0.0f, 1.0f);
    // Standing well back, the wall is not yet grabbable.
    REQUIRE(!plan_climb(yard.collider, walker_at(-2.4f), kCharacter, kClimb)
                 .possible);
}

void lands_on_a_wide_ledge() {
    // A wall thick enough to stand on keeps you on top rather than dropping
    // you over the far side.
    Yard yard;
    yard.wall(0.0f, 1.0f, 3.0f);
    const ClimbPlan plan =
        plan_climb(yard.collider, walker_at(-0.45f), kCharacter, kClimb);
    REQUIRE(plan.possible);
    REQUIRE_NEAR(plan.finish.y, kPlinthTop + 1.0f, 0.02f);
}

void traverse_clears_the_wall() {
    // The arc itself, not just its endpoints: sample the whole climb and check
    // the body never ends up inside the solid it is climbing.
    Yard yard;
    yard.wall(0.0f, 1.0f);
    const ClimbPlan plan =
        plan_climb(yard.collider, walker_at(-0.45f), kCharacter, kClimb);
    REQUIRE(plan.possible);
    glm::vec3 previous = climb_position(plan, 0.0f);
    REQUIRE_NEAR(previous.y, plan.start.y, 1e-4f);
    for (int i = 1; i <= 60; ++i) {
        const float t = static_cast<float>(i) / 60.0f;
        const glm::vec3 at = climb_position(plan, t);
        // Never below BOTH ends: a climb may legitimately finish lower than it
        // started when the drop beyond the wall is deeper than the wall is tall.
        const float lowest = std::min(plan.start.y, plan.finish.y);
        REQUIRE_MSG(at.y >= lowest - 1e-3f, "a climb never dips below its ends",
                    "traverse");
        REQUIRE_MSG(at.z >= previous.z - 1e-3f, "a climb never travels backwards",
                    "traverse");
        // The body is never inside anything, asked with the SAME predicate the
        // walk uses. A bespoke geometric check here would be a second opinion
        // about what "inside a wall" means, and the two would drift.
        REQUIRE_MSG(character_position_clear(yard.collider, at, kCharacter),
                    "body must clear the wall it crosses", "traverse");
        previous = at;
    }
    const glm::vec3 end = climb_position(plan, 1.0f);
    REQUIRE_NEAR(end.x, plan.finish.x, 1e-3f);
    REQUIRE_NEAR(end.y, plan.finish.y, 1e-3f);
    REQUIRE_NEAR(end.z, plan.finish.z, 1e-3f);
    // Standing where the climb leaves you must be legal for the walk that
    // follows it, or the player lands inside geometry having passed every check.
    REQUIRE(character_position_clear(yard.collider, end, kCharacter));
}

void is_deterministic() {
    Yard yard;
    yard.wall(0.0f, 1.0f);
    const PlayerCharacterState at = walker_at(-0.45f);
    const ClimbPlan a = plan_climb(yard.collider, at, kCharacter, kClimb);
    const ClimbPlan b = plan_climb(yard.collider, at, kCharacter, kClimb);
    REQUIRE(a.possible && b.possible);
    REQUIRE(a.ledge == b.ledge);
    REQUIRE(a.finish == b.finish);
    REQUIRE(a.duration_s == b.duration_s);
}

void every_reachable_height_is_climbable() {
    // Sweep the whole band rather than trusting two samples at the edges.
    for (int i = 0; i <= 18; ++i) {
        const float height = 0.45f + static_cast<float>(i) * 0.045f;
        Yard yard;
        yard.wall(0.0f, height);
        const ClimbPlan plan =
            plan_climb(yard.collider, walker_at(-0.45f), kCharacter, kClimb);
        REQUIRE_MSG(plan.possible, "height inside the band must climb", "sweep");
        REQUIRE_NEAR(plan.height_m, height, 0.03f);
    }
}

// --- the real controller, not just the planner -------------------------------

void step_character_climbs_a_wall() {
    // THE PRODUCER, END TO END. Everything above asks plan_climb() directly;
    // this drives the actual step_character() the game runs, with a real
    // InputFrame, and watches the body all the way over. A planner that is
    // right while the controller never calls it is the failure this catches.
    Yard yard;
    yard.wall(0.0f, 1.0f);
    constexpr float dt = 1.0f / 120.0f;
    PlayerCharacterState actor = walker_at(-0.45f);

    InputFrame jump;
    jump.pressed = kBtnJump;
    actor = step_character(actor, kCharacter, jump, yard.collider, dt);
    REQUIRE_MSG(actor.climb.possible, "jumping at a low wall starts a climb",
                "controller");

    const InputFrame idle;
    int steps = 0;
    while (actor.climb.running() && steps < 600) {
        actor = step_character(actor, kCharacter, idle, yard.collider, dt);
        REQUIRE_MSG(character_position_clear(yard.collider, actor.position, kCharacter),
                    "the controller never puts the body inside the wall",
                    "controller");
        // VELOCITY STAYS ZERO, AND THAT IS LOAD BEARING OUTSIDE THIS FILE.
        // App::check_player_fall_damage() reads -velocity.y on the frame the
        // character becomes grounded again. A climb goes airborne and lands, so
        // the moment climb_position() starts reporting a real fall speed,
        // every vault over a garden fence also hurts the player.
        REQUIRE_MSG(actor.velocity.y == 0.0f, "a climb reports no fall speed",
                    "controller");
        ++steps;
    }
    REQUIRE_MSG(steps < 600, "the climb terminates", "controller");
    REQUIRE_MSG(actor.position.z > 0.25f, "ends beyond the wall", "controller");
    REQUIRE_NEAR(actor.position.y, kPlinthTop, 0.02f);
    REQUIRE_MSG(actor.grounded, "hands back a grounded character", "controller");

    // And the walk that follows it works: no residual climb state, no drift.
    InputFrame forward;
    forward.throttle = 1.0f;
    const glm::vec3 before = actor.position;
    for (int i = 0; i < 30; ++i)
        actor = step_character(actor, kCharacter, forward, yard.collider, dt);
    REQUIRE(!actor.climb.possible);
    REQUIRE_MSG(actor.position.z > before.z, "walks on after landing", "controller");
}

void step_character_jumps_when_it_cannot_climb() {
    // The same press must still be an ordinary jump against a 2 m fence,
    // rather than being swallowed by a climb that was never possible.
    Yard yard;
    yard.wall(0.0f, 2.0f);
    constexpr float dt = 1.0f / 120.0f;
    PlayerCharacterState actor = walker_at(-0.45f);
    InputFrame jump;
    jump.pressed = kBtnJump;
    actor = step_character(actor, kCharacter, jump, yard.collider, dt);
    REQUIRE(!actor.climb.possible);
    REQUIRE_MSG(!actor.grounded && actor.velocity.y > 0.0f,
                "an unclimbable wall still leaves you a jump", "controller");

    // ...and the fence actually stops them.
    const InputFrame idle;
    for (int i = 0; i < 400; ++i)
        actor = step_character(actor, kCharacter, idle, yard.collider, dt);
    REQUIRE_MSG(actor.position.z < 0.0f, "a 2 m fence is a barrier", "controller");
}

// --- the real world, not a plinth ---------------------------------------------

void the_suburb_has_a_climbable_fence() {
    // THE FEATURE IS ONLY REAL IF REAL GEOMETRY IS IN BAND. Everything above
    // builds its own wall, which proves the rule and proves nothing about
    // Pinatty. This walks up to the actual Sycamore Loop back-garden fence,
    // baked by the actual authoring code, and climbs it.
    //
    // It is also the regression guard for the fence's collision: with the
    // rails non-solid the boundary had 2.9 m gaps between posts, plan_climb()
    // found nothing to grab, and a character simply strolled through the fence.
    for (std::size_t house = 0; house < city::kResidentialHouses.size(); ++house) {
        TerrainCollider collider{city::kMapSeed};
        const GroundSampler ground{
            [](const void* p, float x, float z) {
                return static_cast<const TerrainCollider*>(p)->height(x, z);
            }, &collider};
        const city::StartSite& site = city::kResidentialHouses[house].site;
        for (const city::StartPart& part : city::bake_residential_house(house, ground)) {
            if (!part.solid) continue;
            const glm::vec2 flat = city::residential_world(
                site, {part.centre.x, part.centre.z});
            const glm::vec3 centre{flat.x,
                site.ground_m + part.bottom_m + part.height_m * 0.5f, flat.y};
            const float yaw = std::atan2(site.sin_yaw, site.cos_yaw) +
                              glm::radians(part.yaw_deg);
            collider.add_static_oriented_box(
                centre, {part.width_m * 0.5f, part.height_m * 0.5f,
                         part.depth_m * 0.5f}, yaw);
        }

        // Stand in the garden just short of the boundary, facing it. The fence
        // runs across local x at z = -16.5; local -Z is the way out.
        const glm::vec2 at = city::residential_world(site, {1.5f, -15.75f});
        PlayerCharacterState actor;
        actor.position = {at.x, collider.height(at.x, at.y), at.y};
        actor.facing_yaw = std::atan2(-site.sin_yaw, site.cos_yaw);
        actor.view_yaw = actor.facing_yaw;
        actor.grounded = true;

        const ClimbPlan plan = plan_climb(collider, actor, kCharacter, kClimb);
        REQUIRE_MSG(plan.possible, "the back garden fence must be climbable",
                    site.name);
        REQUIRE_MSG(plan.height_m >= kClimb.min_height_m &&
                        plan.height_m <= kClimb.max_height_m,
                    "and in band", site.name);
        // It must actually put the player OUTSIDE the garden, past the fence.
        const glm::vec2 landed = city::access_local(site, {plan.finish.x, plan.finish.z});
        REQUIRE_MSG(landed.y < -16.5f, "the climb crosses the boundary", site.name);
        std::printf("  %s: fence climb %.2f m, lands at local z %.2f\n",
                    site.name, static_cast<double>(plan.height_m),
                    static_cast<double>(landed.y));
    }
}

}  // namespace

int main() {
    climbs_a_low_wall();
    rejects_a_tall_fence();
    rejects_a_kerb();
    rejects_a_capped_wall();
    rejects_an_obstruction_behind();
    needs_to_be_facing_it();
    needs_to_be_grounded();
    out_of_reach_is_not_climbable();
    lands_on_a_wide_ledge();
    traverse_clears_the_wall();
    is_deterministic();
    every_reachable_height_is_climbable();
    step_character_climbs_a_wall();
    step_character_jumps_when_it_cannot_climb();
    the_suburb_has_a_climbable_fence();
    std::printf("climb tests passed\n");
    return 0;
}
