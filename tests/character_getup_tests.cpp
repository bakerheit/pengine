// Headless tests for the survived-knockdown get-up phase clock.
//
// character_getup.h is the pure maths shared by the player and the pedestrian
// crowd when a car knocks a character down but they SURVIVE and play the stand
// up clip. The load-bearing decisions are:
//   - the settle-tail trim (the clip ends with ~1 s of standing dead-time),
//   - the playback rate (>1 speeds recovery up),
//   - the clamp that keeps the animation sampler from wrapping past the end,
//   - the finish flag that hands control back / triggers the flee.
// None of that touches the host layer or an asset, so it runs headless. The
// full fall -> get-up state flow is a feel check and stays one.

#include <cmath>
#include <cstdio>

#include "city/character_getup.h"
#include "test_assert.h"

namespace cg = apricot::character_getup;

namespace {

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// The real stand_up clip: 5.43 s, tail-trimmed by 1.0 s, played at 1.5x.
constexpr float DUR  = 5.43f;
constexpr float TAIL = 1.0f;
constexpr float RATE = 1.5f;

// (1) The settle tail is trimmed off the true end, so the standing anchor +
// finish frame sit before the dead-time — NOT at t=0 (which would float the
// body a body-length up, see character_getup.h).
void test_effective_end_trims_tail() {
    REQUIRE(approx(cg::effective_end(DUR, TAIL), DUR - TAIL));
    REQUIRE(cg::effective_end(DUR, TAIL) > 0.f);
}

// (2) Degrades gracefully when the trim would zero/invert the clip: keep the
// whole clip; a zero-length clip yields 0.
void test_effective_end_degrades() {
    REQUIRE(approx(cg::effective_end(0.5f, 1.0f), 0.5f));   // tail > clip → whole clip
    REQUIRE(approx(cg::effective_end(0.f, 1.0f), 0.f));     // no clip → 0
}

// (3) Sample time scales by the playback rate.
void test_sample_time_scales_by_rate() {
    REQUIRE(approx(cg::sample_time(2.0f, RATE), 3.0f));
    REQUIRE(approx(cg::sample_time(0.f, RATE), 0.f));
    REQUIRE(approx(cg::sample_time(1.0f, 1.0f), 1.0f));
}

// (4) Mid-clip: not finished, and the sample time is elapsed*rate.
void test_advance_midclip_not_finished() {
    cg::Progress p = cg::advance(1.0f, RATE, DUR, TAIL);
    REQUIRE(!p.finished);
    REQUIRE(approx(p.sample_t, 1.5f));
}

// (5) Past the (trimmed) end: finished, and the sample time is CLAMPED just shy
// of effective_end so it never fmod-wraps back to the lying start frame.
void test_advance_past_end_finishes_and_clamps() {
    const float end = cg::effective_end(DUR, TAIL);
    // elapsed so large that elapsed*rate is well beyond the end.
    cg::Progress p = cg::advance(100.f, RATE, DUR, TAIL);
    REQUIRE(p.finished);
    REQUIRE(p.sample_t < end);              // never at/over the end
    REQUIRE(p.sample_t <= end - cg::SAMPLE_EPS + 1e-5f);
    REQUIRE(approx(p.sample_t, end - cg::SAMPLE_EPS));
}

// (6) At the exact boundary (elapsed*rate == end - eps) it reports finished.
void test_advance_boundary_finishes() {
    const float end     = cg::effective_end(DUR, TAIL);
    const float elapsed = (end - cg::SAMPLE_EPS) / RATE;   // sample lands at the boundary
    cg::Progress p = cg::advance(elapsed, RATE, DUR, TAIL);
    REQUIRE(p.finished);
}

// (7) A missing / zero-length clip finishes immediately with a safe sample time
// (graceful degrade — the caller falls straight through to "recovered").
void test_advance_missing_clip_finishes_now() {
    cg::Progress p = cg::advance(0.f, RATE, 0.f, TAIL);
    REQUIRE(p.finished);
    REQUIRE(approx(p.sample_t, 0.f));
}

// (8) The sample time is monotonic non-decreasing in elapsed and never exceeds
// the clamp — the property that guards against a wrap-around pop mid-get-up.
void test_advance_monotonic_and_bounded() {
    const float end = cg::effective_end(DUR, TAIL);
    float prev = -1.f;
    for (float e = 0.f; e <= 10.f; e += 0.05f) {
        cg::Progress p = cg::advance(e, RATE, DUR, TAIL);
        REQUIRE(p.sample_t >= prev - 1e-5f);   // non-decreasing
        REQUIRE(p.sample_t <= end);            // bounded by the trimmed end
        prev = p.sample_t;
    }
}

}  // namespace

int main() {
    test_effective_end_trims_tail();
    test_effective_end_degrades();
    test_sample_time_scales_by_rate();
    test_advance_midclip_not_finished();
    test_advance_past_end_finishes_and_clamps();
    test_advance_boundary_finishes();
    test_advance_missing_clip_finishes_now();
    test_advance_monotonic_and_bounded();
    return apricot_test::done("character_getup_tests");
}
