#include "city/objective_runtime.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace apricot {

// ---- Trigger volumes --------------------------------------------------------

ObjectiveRuntime::TriggerId ObjectiveRuntime::add_trigger(
        const glm::vec3& center, float radius_m,
        std::function<void()> on_enter, std::function<void()> on_exit) {
    Trigger t;
    t.id       = next_trigger_id_++;
    t.center   = center;
    t.radius_m = radius_m;
    t.on_enter = std::move(on_enter);
    t.on_exit  = std::move(on_exit);
    triggers_.push_back(std::move(t));
    return triggers_.back().id;
}

void ObjectiveRuntime::remove_trigger(TriggerId id) {
    // Deferred reap: update() iterates triggers_ by index while firing
    // callbacks, and a callback is allowed to call remove_trigger (on itself
    // or a sibling). Marking dead here and compacting at the end of update()
    // keeps that reentrancy safe; a dead trigger fires nothing more.
    for (auto& t : triggers_)
        if (t.id == id) { t.alive = false; return; }
}

const ObjectiveRuntime::Trigger* ObjectiveRuntime::find_trigger(
        TriggerId id) const {
    for (const auto& t : triggers_)
        if (t.id == id && t.alive) return &t;
    return nullptr;
}

bool ObjectiveRuntime::trigger_inside(TriggerId id) const {
    const Trigger* t = find_trigger(id);
    return t != nullptr && t->inside;
}

int ObjectiveRuntime::trigger_count() const {
    int n = 0;
    for (const auto& t : triggers_)
        if (t.alive) ++n;
    return n;
}

// ---- The tracked objective ---------------------------------------------------

void ObjectiveRuntime::start_objective(const Desc& d) {
    desc_          = d;
    state_         = State::Active;
    time_left_s_   = d.time_limit_s > 0.f ? d.time_limit_s : 0.f;
    result_hold_s_ = 0.f;
}

void ObjectiveRuntime::pass_objective() {
    if (state_ != State::Active) return;
    state_         = State::Passed;
    result_hold_s_ = RESULT_HOLD_S;
}

void ObjectiveRuntime::fail_objective() {
    if (state_ != State::Active) return;
    state_         = State::Failed;
    result_hold_s_ = RESULT_HOLD_S;
}

void ObjectiveRuntime::clear_objective() {
    state_         = State::Inactive;
    desc_          = Desc{};
    time_left_s_   = 0.f;
    result_hold_s_ = 0.f;
}

// ---- Update -------------------------------------------------------------------

void ObjectiveRuntime::update(float dt, const glm::vec3& tracked_pos) {
    // 1) Trigger edges. Snapshot the count so a trigger added from inside a
    //    callback is first evaluated NEXT update (its `inside` state hasn't
    //    seen a position yet); index loop because push_back may reallocate.
    const std::size_t n = triggers_.size();
    for (std::size_t i = 0; i < n; ++i) {
        // Re-check alive per step: an earlier callback this pass may have
        // removed this trigger.
        if (!triggers_[i].alive) continue;
        const glm::vec3 d = tracked_pos - triggers_[i].center;
        const bool inside_now =
            glm::dot(d, d) <= triggers_[i].radius_m * triggers_[i].radius_m;
        if (inside_now == triggers_[i].inside) continue;
        triggers_[i].inside = inside_now;
        // Copy the callback before invoking: the callback may add triggers
        // (reallocating triggers_) or remove this one — the copy stays valid.
        const std::function<void()> cb =
            inside_now ? triggers_[i].on_enter : triggers_[i].on_exit;
        if (cb) cb();
    }
    // Reap dead triggers now that the pass is over.
    triggers_.erase(
        std::remove_if(triggers_.begin(), triggers_.end(),
                       [](const Trigger& t) { return !t.alive; }),
        triggers_.end());

    // 2) The objective machine.
    switch (state_) {
    case State::Active: {
        // Reach check before the deadline check: a step that crosses both
        // the marker and the time limit passes.
        if (desc_.reach_radius_m > 0.f && desc_.has_waypoint) {
            const glm::vec3 d = tracked_pos - desc_.waypoint;
            if (glm::dot(d, d) <= desc_.reach_radius_m * desc_.reach_radius_m) {
                pass_objective();
                break;
            }
        }
        if (desc_.time_limit_s > 0.f) {
            time_left_s_ -= dt;
            if (time_left_s_ <= 0.f) {
                time_left_s_ = 0.f;
                fail_objective();
            }
        }
        break;
    }
    case State::Passed:
    case State::Failed:
        // Hold the verdict banner, then auto-clear so the HUD goes quiet.
        result_hold_s_ -= dt;
        if (result_hold_s_ <= 0.f) clear_objective();
        break;
    case State::Inactive:
        break;
    }
}

// ---- the smoke-test objective ----------------------------------------------

void start_smoke_test_objective(ObjectiveRuntime& rt,
                                const glm::vec3& player_pos,
                                const glm::vec3& facing_dir,
                                float (*ground_y)(float wx, float wz)) {
    constexpr float kDistM      = 40.f;
    constexpr float kReachM     = 2.5f;
    constexpr float kTimeLimitS = 30.f;

    glm::vec3 dir{facing_dir.x, 0.f, facing_dir.z};
    const float len2 = glm::dot(dir, dir);
    dir = len2 > 1e-6f ? dir / std::sqrt(len2) : glm::vec3{0.f, 0.f, -1.f};

    ObjectiveRuntime::Desc d;
    d.text           = "REACH THE MARKER";
    d.waypoint       = player_pos + dir * kDistM;
    if (ground_y) d.waypoint.y = ground_y(d.waypoint.x, d.waypoint.z);
    d.has_waypoint   = true;
    d.reach_radius_m = kReachM;
    d.time_limit_s   = kTimeLimitS;
    rt.start_objective(d);
}

} // namespace apricot
