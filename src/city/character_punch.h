#pragma once

// PCG-145 — the pure phase-clock math for the player's bare-fist melee punch
// (the "Fist" slot on the weapon wheel), sibling to character_getup.h.
//
// Two Mixamo jab clips (Punching Left / Punching Right) play once per swing;
// consecutive swings alternate sides. The clip drives three things this header
// owns:
//
//   1. Snappiness. The jab clips end with a recovery wind-down back to guard.
//      We play them faster than 1x (rate) AND trim that recovery tail, so a jab
//      reads as a quick pop and consecutive jabs chain instead of dragging.
//
//   2. The contact window. The fist should only register a hit across the
//      frames the arm is EXTENDED (roughly mid-swing) — not on the wind-up or
//      the recovery. is_contact()/contact_window() mark those frames so gameplay
//      lands exactly one hit per swing.
//
//   3. Alternation. AlternatingSide hands out right/left in turn so the two
//      clips trade off between swings.
//
// Header-only, no host-layer types and no glm, so it runs in the headless
// gate (sibling to character_getup.h). The player/character system owns the
// state transitions, the per-frame sampling and the one-shot hit latch; this
// only decides the clock, the window, and the side.

namespace apricot::character_punch {

// Keep a hair short of the true end so Animation::sample never fmod-wraps back
// to the start of the clip (same guard the get-up / corpse-hold use).
inline constexpr float SAMPLE_EPS = 0.01f;

// The jab clip's last useful frame: its true duration minus the recovery-tail
// trim (the clip finishes winding the arm back to guard). Degrades gracefully
// if the trim would zero/invert the clip.
inline float effective_end(float clip_dur, float tail_trim_s) {
    float e = clip_dur - tail_trim_s;
    if (e > 0.f)        return e;
    if (clip_dur > 0.f) return clip_dur;
    return 0.f;
}

// Time to sample the jab clip at, given the seconds elapsed in the swing and a
// playback rate (>1 speeds the jab up).
inline float sample_time(float punch_elapsed_s, float rate) {
    return punch_elapsed_s * rate;
}

struct Progress {
    float sample_t = 0.f;   // clamped time to sample the jab clip at
    bool  finished = false; // swing complete → hand the pose back to locomotion
};

// Advance the swing phase: map elapsed swing seconds (at `rate`) to a clamped
// sample time on [0, effective_end] and report whether we've reached the end.
// A missing/zero-length clip reports finished immediately (graceful degrade —
// the caller falls straight through to "swing over").
inline Progress advance(float punch_elapsed_s, float rate,
                        float clip_dur, float tail_trim_s) {
    Progress p;
    if (clip_dur <= 0.f) { p.finished = true; return p; }
    const float end = effective_end(clip_dur, tail_trim_s);
    float t = sample_time(punch_elapsed_s, rate);
    if (t >= end - SAMPLE_EPS) {
        p.finished = true;
        t = end > SAMPLE_EPS ? end - SAMPLE_EPS : 0.f;
    }
    p.sample_t = t;
    return p;
}

// The fist registers a hit only while extended — the [lo, hi) span of clip
// sample-time (seconds) around mid-swing. Half-open so a single frame can't
// double-count at the boundary. Caller derives lo/hi from contact_window().
inline bool is_contact(float sample_t, float contact_lo, float contact_hi) {
    return sample_t >= contact_lo && sample_t < contact_hi;
}

struct ContactWindow { float lo = 0.f; float hi = 0.f; };

// Map mid-swing fractions of the (trimmed) clip to absolute sample-time bounds.
// Ordered so a degenerate config can't invert the window (hi >= lo always).
inline ContactWindow contact_window(float end, float lo_frac, float hi_frac) {
    ContactWindow w;
    w.lo = end * lo_frac;
    w.hi = end * hi_frac;
    if (w.hi < w.lo) w.hi = w.lo;
    return w;
}

// Which fist throws the NEXT jab. Each swing calls take() to get THIS jab's
// side, which also advances to the other side, so consecutive jabs alternate
// right/left. peek() reads the pending side without advancing (tests / HUD).
struct AlternatingSide {
    bool right = true;    // the first jab is a right jab
    bool take() { bool s = right; right = !right; return s; }
    bool peek() const { return right; }
};

} // namespace apricot::character_punch
