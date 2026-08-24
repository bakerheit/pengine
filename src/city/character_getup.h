#pragma once

// PCG-144 — the pure phase-clock math for the "stand up after a survived
// knockdown" get-up, shared by the player (Player::advance_knockdown) and the
// pedestrian crowd (PedestrianSystem::advance_downed).
//
// When a car knocks a character down but they SURVIVE, they play the Mixamo
// "Stand Up" clip once and then resume control / flee. Two things about that
// clip drive this header:
//
//   1. Snappiness. The clip is ~5.4 s and ends with ~1 s of settled
//      standing-still dead-time. We play it faster than 1x (rate) AND trim that
//      settle tail, so recovery feels reactive instead of a long lie-in.
//
//   2. Root anchoring reference. The fall clips (Hit By Car / Dying) are
//      anchored at bind translation + (anim_t - t0), where t0 is sampled at the
//      clip's STANDING frame — for a fall that's its (trimmed) START. A get-up
//      is the reverse: its standing frame is its END. So the standing anchor
//      reference is effective_end(), NOT t=0 — sampling t0 at the start would
//      leave the root at bind (standing height) while the pose is still lying,
//      floating the body a full body-length up. effective_end() gives both the
//      anchor frame and the finish frame so they agree.
//
// Header-only, no host-layer types and no glm, so it runs in the headless
// gate (sibling to character_punch.h). The character system owns the state
// transitions and the per-frame sampling; this only decides the clock.

namespace apricot::character_getup {

// Keep a hair short of the true end so Animation::sample never fmod-wraps back
// to the start of the clip (same guard the corpse-hold uses).
inline constexpr float SAMPLE_EPS = 0.01f;

// The get-up clip's last useful frame: its true duration minus the settle-tail
// trim (the clip finishes with ~1 s of standing dead-time). Both the standing
// root anchor (sampled here at load) and the finish test below key off this, so
// the body lands exactly at bind when recovery completes. Degrades gracefully if
// the trim would zero/invert the clip.
inline float effective_end(float clip_dur, float tail_trim_s) {
    float e = clip_dur - tail_trim_s;
    if (e > 0.f)         return e;
    if (clip_dur > 0.f)  return clip_dur;
    return 0.f;
}

// Time to sample the get-up clip at, given the seconds elapsed in the get-up
// sub-phase and a playback rate (>1 speeds recovery up).
inline float sample_time(float getup_elapsed_s, float rate) {
    return getup_elapsed_s * rate;
}

struct Progress {
    float sample_t = 0.f;   // clamped time to sample the get-up clip at
    bool  finished = false; // recovery complete → hand back control / flee
};

// Advance the get-up phase: map elapsed get-up seconds (at `rate`) to a clamped
// sample time on [0, effective_end] and report whether we've reached the end.
// A missing/zero-length clip reports finished immediately (graceful degrade —
// the caller falls straight through to "recovered").
inline Progress advance(float getup_elapsed_s, float rate,
                        float clip_dur, float tail_trim_s) {
    Progress p;
    if (clip_dur <= 0.f) { p.finished = true; return p; }
    const float end = effective_end(clip_dur, tail_trim_s);
    float t = sample_time(getup_elapsed_s, rate);
    if (t >= end - SAMPLE_EPS) {
        p.finished = true;
        t = end > SAMPLE_EPS ? end - SAMPLE_EPS : 0.f;
    }
    p.sample_t = t;
    return p;
}

} // namespace apricot::character_getup
