// The camera-locked rain field.
//
// One function does both jobs — recycling a drop that fell out of the bottom,
// and keeping the field locked to a camera that moved — and its postcondition
// is a HALF-OPEN interval. Everything here exists to hammer that postcondition
// with the inputs you cannot produce by looking at the game: a twenty-second
// debugger pause, a camera that teleported across the world, a degenerate box.
//
// A drop that escapes the box does not crash. It streaks off to somewhere odd
// once every few minutes, gets written off as a fluke, and lives forever.

#include <cmath>
#include <cstdio>

#include "gfx/rain_field.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool inside_half_open(float v, float centre, float span) {
    const float lo = centre - span * 0.5f;
    return v >= lo && v < lo + span;
}

void wrapping_always_lands_inside_the_half_open_window() {
    const float centre = 12.5f;
    const float span = 40.0f;

    const float probes[] = {
        12.5f,      // already centred
        -7.5f,      // exactly on the low edge
        32.5f,      // exactly on the high edge (must wrap to the low one)
        32.4999f,
        -1000.0f,   // twenty-five spans below
        1.0e6f,     // twenty-five THOUSAND spans above: a lag spike, or a
        -1.0e6f,    // camera that teleported
        0.0f,
    };

    for (const float p : probes) {
        const float w = wrap_into_span(p, centre, span);
        REQUIRE_MSG(inside_half_open(w, centre, span),
                    "wrap must land in [centre-span/2, centre+span/2)", "probe");
        // And it must differ from the input only by whole spans, or the drop
        // has been teleported rather than recycled.
        const float k = (p - w) / span;
        REQUIRE_MSG(std::fabs(k - std::round(k)) < 1e-2f,
                    "wrap must shift by a whole number of spans", "probe");
    }

    // The high edge specifically wraps to the low one, which is what makes the
    // interval half-open rather than closed-and-hoping.
    REQUIRE(wrap_into_span(32.5f, centre, span) < 32.5f);
    apricot_test::pass("wrap holds its half-open postcondition on every input");
}

void a_degenerate_span_is_returned_untouched() {
    // A zero or negative box has no inside to wrap into. Returning the value
    // beats dividing by zero and filling the field with NaN, which propagates
    // into the vertex buffer and takes the whole pass down with it.
    REQUIRE(wrap_into_span(7.0f, 0.0f, 0.0f) == 7.0f);
    REQUIRE(wrap_into_span(7.0f, 0.0f, -3.0f) == 7.0f);
    apricot_test::pass("a degenerate span cannot produce NaN");
}

void drops_stay_in_the_box_however_long_the_frame_was() {
    RainTuning t;
    const glm::vec3 cam{100.0f, 25.0f, -40.0f};
    const glm::vec3 centre = cam + glm::vec3{0.0f, t.span.y * 0.15f, 0.0f};

    // Every dt from a 240 Hz frame to a debugger held for twenty seconds.
    const float deltas[] = {1.0f / 240.0f, 1.0f / 60.0f, 0.1f, 1.0f, 20.0f, 600.0f};

    for (const float dt : deltas) {
        glm::vec3 p = rain_seed_position(t, cam, 0xFEEDu, 7);
        for (int step = 0; step < 40; ++step) {
            p = rain_advance(p, t, cam, dt);
            REQUIRE_MSG(inside_half_open(p.x, centre.x, t.span.x),
                        "drop escaped the box on X", "advance");
            REQUIRE_MSG(inside_half_open(p.y, centre.y, t.span.y),
                        "drop escaped the box on Y", "advance");
            REQUIRE_MSG(inside_half_open(p.z, centre.z, t.span.z),
                        "drop escaped the box on Z", "advance");
        }
    }
    apricot_test::pass("drops survive a 600-second frame without escaping");
}

void the_field_follows_a_camera_that_teleported() {
    RainTuning t;
    glm::vec3 p = rain_seed_position(t, glm::vec3{0.0f}, 0xABCDu, 3);

    // Cross the world in one frame. The next advance must pull the drop into
    // the new box; a field that only wraps on Y would leave it 5 km behind and
    // the player would drive out of the rain.
    const glm::vec3 far_away{5000.0f, -300.0f, -8000.0f};
    p = rain_advance(p, t, far_away, 1.0f / 60.0f);

    const glm::vec3 centre = far_away + glm::vec3{0.0f, t.span.y * 0.15f, 0.0f};
    REQUIRE(inside_half_open(p.x, centre.x, t.span.x));
    REQUIRE(inside_half_open(p.y, centre.y, t.span.y));
    REQUIRE(inside_half_open(p.z, centre.z, t.span.z));
    apricot_test::pass("the field catches up with a teleported camera in one step");
}

void a_dry_sky_costs_exactly_nothing() {
    RainTuning t;
    REQUIRE_MSG(rain_drop_count(t, 0.0f) == 0, "zero intensity means zero drops",
                "dry");
    REQUIRE_MSG(rain_drop_count(t, -1.0f) == 0, "negative clamps to dry", "dry");

    // And once it is raining at all, there is something to see.
    REQUIRE(rain_drop_count(t, 0.001f) >= 1);
    REQUIRE(rain_drop_count(t, 1.0f) == t.drop_count);
    REQUIRE(rain_drop_count(t, 2.0f) == t.drop_count);   // clamped, not scaled
    REQUIRE(rain_drop_count(t, 0.5f) < t.drop_count);
    apricot_test::pass("dry is free; intensity scales the count and clamps");
}

void the_field_is_deterministic() {
    RainTuning t;
    const glm::vec3 cam{3.0f, 9.0f, -2.0f};
    for (int i = 0; i < 50; ++i) {
        const glm::vec3 a = rain_seed_position(t, cam, 0x1234u, i);
        const glm::vec3 b = rain_seed_position(t, cam, 0x1234u, i);
        REQUIRE_MSG(a == b, "seeding must be pure in (seed, index)", "determinism");
    }
    // Different indices must not all land in the same place, which is what a
    // hash used wrong looks like.
    REQUIRE(rain_seed_position(t, cam, 0x1234u, 0) !=
            rain_seed_position(t, cam, 0x1234u, 1));
    apricot_test::pass("drop seeding is pure and actually varies");
}

void the_fall_direction_is_down_and_normalised() {
    RainTuning t;
    const glm::vec3 d = rain_fall_dir(t);
    REQUIRE_NEAR(static_cast<double>(glm::length(d)), 1.0, 1e-5);
    REQUIRE_MSG(d.y < -0.9f, "rain falls down", "fall");
    REQUIRE_MSG(d.x > 0.0f, "and slants with the wind", "fall");
    apricot_test::pass("rain falls down, on a slant, at unit speed");
}

}  // namespace

int main() {
    wrapping_always_lands_inside_the_half_open_window();
    a_degenerate_span_is_returned_untouched();
    drops_stay_in_the_box_however_long_the_frame_was();
    the_field_follows_a_camera_that_teleported();
    a_dry_sky_costs_exactly_nothing();
    the_field_is_deterministic();
    the_fall_direction_is_down_and_normalised();
    return apricot_test::done("rain_field_tests");
}
