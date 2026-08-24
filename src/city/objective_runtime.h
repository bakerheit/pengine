#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace apricot {

// The objective/trigger runtime substrate. Two small, engine-free machines:
//
//   1. Sphere TRIGGER VOLUMES with enter/exit callbacks, tested every update
//      against one tracked world point (the player, v1).
//   2. A single TRACKED OBJECTIVE state machine (exactly one active at a
//      time): Inactive -> Active -> Passed/Failed, with HUD text, an optional
//      world waypoint for something else to draw, an optional auto-pass reach
//      sphere, and an optional time limit whose expiry fails the objective.
//
// DELIBERATELY SELF-CONTAINED, and it is worth saying why rather than treating
// it as tidiness. It does not include mission_def.h and it depends on nothing
// but glm. That is what let the authored data model, the actor layer and this
// runtime be built at the same time by different people and meet at the end.
// Binding a MissionDef to this is a separate job with a separate signature.
//
// Headless-testable, and the tests drive the PRODUCER: they step update() with
// a moving point rather than hand-setting states, so a broken transition
// actually fails instead of being bypassed.
class ObjectiveRuntime {
public:
    // ---- Sphere trigger volumes --------------------------------------------
    //
    // Containment is evaluated in update(); callbacks fire on the EDGE
    // (outside -> inside fires on_enter, inside -> outside fires on_exit), at
    // most once per update. A trigger added while the tracked point is
    // already inside it fires on_enter on the next update (initial state is
    // "outside"). Callbacks may safely add/remove triggers or start/pass/fail
    // the objective from inside the callback (removal is deferred to the end
    // of the update pass; triggers added mid-update are first evaluated on
    // the NEXT update).
    using TriggerId = int;
    static constexpr TriggerId kInvalidTrigger = -1;

    TriggerId add_trigger(const glm::vec3& center, float radius_m,
                          std::function<void()> on_enter,
                          std::function<void()> on_exit = {});
    void remove_trigger(TriggerId id);              // unknown id: no-op
    // Containment as of the LAST update (false for a just-added trigger).
    bool trigger_inside(TriggerId id) const;
    int  trigger_count() const;

    // ---- The tracked objective ---------------------------------------------
    enum class State { Inactive, Active, Passed, Failed };

    struct Desc {
        std::string text;                 // HUD line while active
        glm::vec3   waypoint {0.f};       // world point for the HUD marker
        bool        has_waypoint  = false;
        // > 0: auto-pass when the tracked point enters the sphere at
        // `waypoint` (the v1 `reach` objective). 0: verdicts come only from
        // pass_objective()/fail_objective() (kill/approach etc., PCG-206).
        float       reach_radius_m = 0.f;
        // > 0: seconds allowed; expiry => Failed. <= 0: untimed.
        float       time_limit_s   = 0.f;
    };

    // Starts (or restarts) the tracked objective: state -> Active, the time
    // limit rearmed from the desc. Replaces any previous objective, including
    // a still-showing pass/fail banner.
    void start_objective(const Desc& d);
    // External verdicts. No-ops unless Active. Both hold the result on screen
    // for RESULT_HOLD_S (see result_hold_remaining_s), then auto-clear.
    void pass_objective();
    void fail_objective();
    // Back to Inactive immediately, no verdict, banner cleared.
    void clear_objective();

    // Advance one fixed step against the tracked point (the player). Fires
    // trigger enter/exit edges, then drives the objective machine: reach
    // check first (a step that crosses both the marker and the deadline
    // passes — player-friendly tie), then the time limit.
    void update(float dt, const glm::vec3& tracked_pos);

    // ---- HUD-facing reads (consumed by objective_hud::render) ---------------
    State              state() const         { return state_; }
    const std::string& text() const          { return desc_.text; }
    // True only while Active — the marker disappears with the verdict.
    bool      has_waypoint() const {
        return state_ == State::Active && desc_.has_waypoint;
    }
    glm::vec3 waypoint() const               { return desc_.waypoint; }
    bool      timed() const {
        return state_ == State::Active && desc_.time_limit_s > 0.f;
    }
    float     time_remaining_s() const       { return time_left_s_; }
    // Seconds the Passed/Failed banner has left (0 when not showing).
    float     result_hold_remaining_s() const { return result_hold_s_; }

    static constexpr float RESULT_HOLD_S = 4.f;   // banner hold after verdict

private:
    struct Trigger {
        TriggerId             id     = kInvalidTrigger;
        glm::vec3             center {0.f};
        float                 radius_m = 0.f;
        bool                  inside = false;   // as of the last update
        bool                  alive  = true;    // false: reap after the pass
        std::function<void()> on_enter;
        std::function<void()> on_exit;
    };

    const Trigger* find_trigger(TriggerId id) const;

    std::vector<Trigger> triggers_;
    TriggerId            next_trigger_id_ = 0;

    State state_         = State::Inactive;
    Desc  desc_;
    float time_left_s_   = 0.f;
    float result_hold_s_ = 0.f;
};

// The smoke-test objective — the probe a dev panel fires to check the whole
// path end to end. Starts a "REACH THE MARKER" objective 40 m from
// `player_pos` along `facing_dir` (XZ only; degenerate directions fall back to
// -Z), with a 2.5 m reach sphere and a 30 s time limit.
//
// `ground_y(wx, wz)` snaps the marker to the terrain — a function POINTER, not
// a terrain reference, which is the whole reason this file owes the terrain
// module nothing. Pass null and the marker keeps player_pos.y, which is what
// the headless tests do.
void start_smoke_test_objective(ObjectiveRuntime& rt,
                                const glm::vec3& player_pos,
                                const glm::vec3& facing_dir,
                                float (*ground_y)(float wx, float wz));

} // namespace apricot
