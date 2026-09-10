#include "app/app.h"
#include "core/log.h"
#include "city/police_officer.h"

namespace apricot {

// A (lane_key, slot) pair is a recurring schedule slot. One pedestrian can
// retire and the next lap's pedestrian appear at the SAME pair with no absent
// presentation update in between — 161 times in 270 s on the authored city.
// CharacterVisual keeps a live ragdoll, a get-up crossfade and an animator on
// each rig and reconciles by identity, so a pair-only match hands a brand-new
// person the previous one's sprawl.
//
// The live crowd cannot be asked to stage that on demand: nothing knocks a
// chosen walker down and then re-departs their slot to order. So this drives
// the REAL reconciliation — CharacterVisual::sync_ambient, the same function
// sync() calls every frame — from an explicit agent list, against the real
// loaded models and the real terrain collider.
void App::character_identity_check() {
    if (character_identity_check_done_) return;
    // One frame of models and terrain first; a rig needs a loaded model to
    // pick from and the ragdoll needs ground to land on.
    if (frames_rendered_ < 2) return;
    character_identity_check_done_ = true;

    auto fail = [&](const char* why) {
        AP_ERROR("character identity check: %s", why);
        character_identity_check_failed_ = true;
    };

    const glm::vec3 where{start_position_.x, collider_.height(start_position_.x,
                                                              start_position_.y),
                          start_position_.y};
    const uint64_t lane_key = 0x51D3F00Dull;
    const uint32_t slot = 11u;

    auto walker = [&](int64_t generation) {
        PedAgent p;
        p.lane_key = lane_key;
        p.slot = slot;
        p.generation = generation;
        p.pos = where;
        p.fwd = glm::vec3{0.0f, 0.0f, -1.0f};
        p.speed_mps = 1.35f;
        p.activity = PedActivity::Walking;
        return p;
    };

    // --- A is knocked down and left to settle into a ragdoll ---------------
    std::vector<PedAgent> agents{walker(0)};
    agents[0].activity = PedActivity::Downed;
    agents[0].impact_dir_xz = glm::vec2{1.0f, 0.0f};
    agents[0].impact_from_bullet = false;
    double seconds = 0.0;
    CharacterVisual::AmbientRigProbe probe;
    for (int i = 0; i < 90; ++i) {
        seconds += kSimDt;
        sync_ambient_for_check(agents, seconds);
    }
    if (!character_visual_.ambient_rig_probe(lane_key, slot, probe)) {
        fail("A never got a rig");
        return;
    }
    if (!probe.ragdolling) {
        fail("A never entered a ragdoll; the check cannot prove anything");
        return;
    }
    if (probe.generation != 0) { fail("A's rig has the wrong generation"); return; }
    AP_INFO("character identity check: A down, ragdolling=%d generation=%lld",
            probe.ragdolling ? 1 : 0, (long long)probe.generation);

    // --- B replaces A at the SAME pair, on the very next update ------------
    // The pair is present in `agents` before and after, so the reconciliation
    // never observes it absent — exactly the case a pair-only match misses.
    agents[0] = walker(1);
    seconds += kSimDt;
    sync_ambient_for_check(agents, seconds);

    if (!character_visual_.ambient_rig_probe(lane_key, slot, probe)) {
        fail("B never got a rig");
        return;
    }
    if (probe.generation != 1) {
        fail("B's rig kept A's generation: the reconciliation reused it");
        return;
    }
    if (probe.ragdolling) {
        fail("B inherited A's ragdoll");
        return;
    }
    if (probe.getup_running) {
        fail("B is standing up out of A's landed pose");
        return;
    }
    // --- the police officer rig is a distinct reconciliation --------------
    // Same pair, same seamless replacement, different loop and struct. The
    // officer rig is deliberately stable across every occupancy phase of ONE
    // cruiser, so this proves the reset triggers on a different cruiser only.
    const uint64_t cruiser_key = 0x0C0FFEE0ull;
    const uint32_t cruiser_slot = 3u;
    auto cruiser = [&](int64_t generation) {
        VehicleAgent v;
        v.lane_key = cruiser_key;
        v.slot = cruiser_slot;
        v.generation = generation;
        v.police_unit = true;
        v.pos = where;
        v.fwd = glm::vec3{0.0f, 0.0f, -1.0f};
        // ON FOOT on purpose. A seated officer is posed by sample_clip and
        // accumulates nothing, so reuse and rebuild would be indistinguishable
        // and this leg would prove nothing. Pursuing runs advance_rig, which is
        // what carries an animator across frames.
        v.officer.phase = PoliceOfficerPhase::Pursuing;
        v.officer.pos = where;
        v.officer.previous_pos = where;
        return v;
    };
    std::vector<VehicleAgent> units{cruiser(0)};
    character_visual_.sync_police_units(units, traffic_visual_, 0.0f, 0, seconds,
                                        camera_.position, 0.0f);
    CharacterVisual::AmbientRigProbe police;
    if (!character_visual_.police_rig_probe(cruiser_key, cruiser_slot, police)) {
        fail("the police cruiser never got an officer rig");
        return;
    }
    const std::size_t officer_model = police.model;
    // Same cruiser for a while: the officer rig must be KEPT and its animator
    // must accumulate. Without this the check would pass for the wrong reason —
    // a rig that reset every frame also "resets" on a generation change.
    double officer_seconds = seconds;
    for (int i = 1; i <= 120; ++i) {
        officer_seconds += kSimDt;
        character_visual_.sync_police_units(units, traffic_visual_, 0.0f, i,
                                            officer_seconds, camera_.position,
                                            0.0f);
    }
    if (!character_visual_.police_rig_probe(cruiser_key, cruiser_slot, police) ||
        police.generation != 0 || !police.started) {
        fail("the officer rig did not survive its own cruiser's next frames");
        return;
    }
    // NOTE, and it bounds what this leg proves: with the officer pursuing on
    // foot and stationary, the animator reports 0 accumulated clip time, so
    // there is no observable carried state to catch leaking. What this leg
    // therefore proves is the RESET MECHANISM — that the police reconciliation
    // reuses on the same departure and rebuilds on a different one — not a
    // visible artefact. The visible artefact is proven on the pedestrian leg
    // above, which shares the rig type and the predicate.
    // A different departure at the same pair is a different cruiser.
    units[0] = cruiser(1);
    officer_seconds += kSimDt;
    character_visual_.sync_police_units(units, traffic_visual_, 0.0f, 121,
                                        officer_seconds, camera_.position,
                                        0.0f);
    if (!character_visual_.police_rig_probe(cruiser_key, cruiser_slot, police)) {
        fail("the replacement cruiser never got an officer rig");
        return;
    }
    if (police.generation != 1) {
        fail("the officer rig kept the previous cruiser's generation");
        return;
    }
    AP_INFO("character identity check: police officer rig held generation 0 "
            "across 120 frames of one cruiser, then reset to %lld on a new "
            "departure at the same (lane,slot) (model %zu)",
            (long long)police.generation, officer_model);

    AP_INFO("character identity check: PASS; a fresh departure at the same "
            "(lane,slot) begins upright with no ragdoll and no get-up fade "
            "(generation %lld, ragdolling=%d, getup=%d)",
            (long long)probe.generation, probe.ragdolling ? 1 : 0,
            probe.getup_running ? 1 : 0);
}

void App::sync_ambient_for_check(const std::vector<PedAgent>& agents,
                                 double seconds) {
    character_visual_.sync_ambient(agents, seconds, camera_.position, 0.0f,
                                   &collider_);
}

}  // namespace apricot
