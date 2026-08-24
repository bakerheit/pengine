// Headless tests for the bare-fist melee punch phase clock.
//
// character_punch.h is the pure maths behind the player's fist. The
// load-bearing decisions are:
//   - the recovery-tail trim (the jab clip winds the arm back to guard),
//   - the playback rate (>1 snaps the jab),
//   - the clamp that keeps the animation sampler from wrapping past the end,
//   - the finished flag that hands the pose back to locomotion,
//   - the mid-swing CONTACT WINDOW that lands exactly one hit per swing,
//   - the right/left ALTERNATION between swings.
// None of that touches the host layer or an asset, so it runs headless. The
// full punch -> hit -> ped-stagger data path is a feel check and stays one.

#include <cmath>
#include <cstdio>

#include "city/character_punch.h"
#include "test_assert.h"

namespace cp = apricot::character_punch;

namespace {

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// A representative jab: ~2.0 s clip, recovery tail trimmed 0.35 s, played 1.35x.
constexpr float DUR  = 2.0f;
constexpr float TAIL = 0.35f;
constexpr float RATE = 1.35f;
// Mid-swing contact window: the fist is extended between 30% and 55% of the
// (trimmed) clip.
constexpr float LO_FRAC = 0.30f;
constexpr float HI_FRAC = 0.55f;

// (1) The recovery tail is trimmed off the true end, so the swing finishes
// before the wind-down (a snappier jab).
void test_effective_end_trims_tail() {
    REQUIRE(approx(cp::effective_end(DUR, TAIL), DUR - TAIL));
    REQUIRE(cp::effective_end(DUR, TAIL) > 0.f);
}

// (2) Degrades gracefully when the trim would zero/invert the clip: keep the
// whole clip; a zero-length clip yields 0.
void test_effective_end_degrades() {
    REQUIRE(approx(cp::effective_end(0.2f, 0.35f), 0.2f));  // tail > clip → whole clip
    REQUIRE(approx(cp::effective_end(0.f, 0.35f), 0.f));    // no clip → 0
}

// (3) Sample time scales by the playback rate.
void test_sample_time_scales_by_rate() {
    REQUIRE(approx(cp::sample_time(1.0f, RATE), 1.35f));
    REQUIRE(approx(cp::sample_time(0.f, RATE), 0.f));
    REQUIRE(approx(cp::sample_time(1.0f, 1.0f), 1.0f));
}

// (4) Mid-swing: not finished, and the sample time is elapsed*rate.
void test_advance_midclip_not_finished() {
    cp::Progress p = cp::advance(0.5f, RATE, DUR, TAIL);
    REQUIRE(!p.finished);
    REQUIRE(approx(p.sample_t, 0.675f));
}

// (5) Past the (trimmed) end: finished, and the sample time is CLAMPED just shy
// of effective_end so it never fmod-wraps back to the wind-up frame.
void test_advance_past_end_finishes_and_clamps() {
    const float end = cp::effective_end(DUR, TAIL);
    cp::Progress p = cp::advance(100.f, RATE, DUR, TAIL);
    REQUIRE(p.finished);
    REQUIRE(p.sample_t < end);
    REQUIRE(p.sample_t <= end - cp::SAMPLE_EPS + 1e-5f);
    REQUIRE(approx(p.sample_t, end - cp::SAMPLE_EPS));
}

// (6) A missing / zero-length clip finishes immediately with a safe sample time
// (graceful degrade — the caller falls straight through to "swing over").
void test_advance_missing_clip_finishes_now() {
    cp::Progress p = cp::advance(0.f, RATE, 0.f, TAIL);
    REQUIRE(p.finished);
    REQUIRE(approx(p.sample_t, 0.f));
}

// (7) The contact window maps mid-swing fractions to absolute sample-time
// bounds, and is_contact() is a half-open [lo, hi) test: inside the window is a
// hit, the wind-up (before lo) and recovery (at/after hi) are not.
void test_contact_window_and_is_contact() {
    const float end = cp::effective_end(DUR, TAIL);        // 1.65
    cp::ContactWindow w = cp::contact_window(end, LO_FRAC, HI_FRAC);
    REQUIRE(approx(w.lo, end * LO_FRAC));
    REQUIRE(approx(w.hi, end * HI_FRAC));
    // Wind-up: before the window → no hit.
    REQUIRE(!cp::is_contact(w.lo - 0.01f, w.lo, w.hi));
    // Extended: inside → hit.
    REQUIRE(cp::is_contact((w.lo + w.hi) * 0.5f, w.lo, w.hi));
    REQUIRE(cp::is_contact(w.lo, w.lo, w.hi));             // lo is inclusive
    // Recovery: at/after hi → no hit (half-open).
    REQUIRE(!cp::is_contact(w.hi, w.lo, w.hi));
    REQUIRE(!cp::is_contact(w.hi + 0.01f, w.lo, w.hi));
}

// (8) contact_window can't invert: a degenerate lo > hi collapses to an empty
// (zero-width) window rather than a backwards one that would never/always hit.
void test_contact_window_never_inverts() {
    cp::ContactWindow w = cp::contact_window(1.65f, 0.6f, 0.2f);  // lo_frac > hi_frac
    REQUIRE(w.hi >= w.lo);
    REQUIRE(!cp::is_contact(w.lo, w.lo, w.hi));  // empty window → no frame hits
}

// (9) Consecutive swings alternate: take() returns THIS jab's side and advances
// to the other, so a right jab is followed by a left, then a right again.
void test_alternating_side_flips() {
    cp::AlternatingSide side;
    REQUIRE(side.peek());          // first jab is a right
    REQUIRE(side.take());          // …and take() reports right
    REQUIRE(!side.peek());         // next pending side is now left
    REQUIRE(!side.take());         // second jab is a left
    REQUIRE(side.take());          // third is a right again
    REQUIRE(!side.take());         // fourth a left — strict alternation
}

}  // namespace

int main() {
    test_effective_end_trims_tail();
    test_effective_end_degrades();
    test_sample_time_scales_by_rate();
    test_advance_midclip_not_finished();
    test_advance_past_end_finishes_and_clamps();
    test_advance_missing_clip_finishes_now();
    test_contact_window_and_is_contact();
    test_contact_window_never_inverts();
    test_alternating_side_flips();
    return apricot_test::done("character_punch_tests");
}
