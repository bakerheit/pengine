#include <cmath>
#include <limits>

#include "game/snowpack.h"
#include "test_assert.h"

using namespace apricot;

namespace {

SnowpackState simulate(SnowpackState state, double snowfall, double heat,
                       double dt_seconds, int steps) {
    for (int i = 0; i < steps; ++i) {
        state = advance_snowpack(state, snowfall, heat, dt_seconds);
    }
    return state;
}

void accumulation_is_gradual_and_tracks_intensity() {
    const SnowpackState light = simulate({}, 0.25, 0.0, 1.0, 60);
    const SnowpackState heavy = simulate({}, 1.0, 0.0, 1.0, 60);

    REQUIRE(light.depth_m() > 0.0);
    REQUIRE(heavy.depth_m() > light.depth_m());
    REQUIRE_NEAR(light.depth_m(), 0.00150, 1e-12);
    REQUIRE_NEAR(heavy.depth_m(), 0.015, 1e-12);
    REQUIRE(heavy.depth_m() < SnowpackTuning{}.max_depth_m);
    REQUIRE(visual_snow_cover_from_depth(heavy.depth_m()) < 1.0f);
    apricot_test::pass("snow intensity accumulates physical depth gradually");
}

void one_step_never_creates_an_immediate_full_pack() {
    const SnowpackState one_step = advance_snowpack({}, 1.0, 0.0, 1.0 / 120.0);
    REQUIRE(one_step.depth_m() > 0.0);
    REQUIRE(one_step.depth_m() < 0.00001);
    REQUIRE(one_step.depth_m() < SnowpackTuning{}.max_depth_m);
    REQUIRE(visual_snow_cover_from_depth(one_step.depth_m()) < 0.001f);
    apricot_test::pass("one fixed step cannot jump to a full snowpack");
}

void depth_stays_bounded() {
    SnowpackState state = advance_snowpack({}, 1.0, 0.0, 100000.0);
    REQUIRE(state.depth_m() == SnowpackTuning{}.max_depth_m);
    state = advance_snowpack(state, 1.0, 0.0, 100000.0);
    REQUIRE(state.depth_m() == SnowpackTuning{}.max_depth_m);

    state = advance_snowpack(state, 0.0, 1.0, 100000.0);
    REQUIRE(state.depth_m() == 0.0);
    state = advance_snowpack(state, 0.0, 1.0, 100000.0);
    REQUIRE(state.depth_m() == 0.0);
    apricot_test::pass("snowpack clamps between zero and 1.5 metres");
}

void snow_stops_and_heat_melts_gradually() {
    SnowpackState start;
    start.set_depth_m(0.50);
    const SnowpackState passive = advance_snowpack(start, 0.0, 0.0, 60.0);
    const SnowpackState heated = advance_snowpack(start, 0.0, 1.0, 60.0);

    REQUIRE(passive.depth_m() < start.depth_m());
    REQUIRE(passive.depth_m() > 0.0);
    REQUIRE(heated.depth_m() < passive.depth_m());
    REQUIRE(heated.depth_m() > 0.0);
    REQUIRE_NEAR(passive.depth_m(), 0.497, 1e-12);
    REQUIRE_NEAR(heated.depth_m(), 0.467, 1e-12);
    apricot_test::pass("clear weather melts slowly and heat melts faster");
}

void manual_depth_override_clamps_bad_values() {
    SnowpackState state;
    state.set_depth_m(0.42);
    REQUIRE(state.depth_m() == 0.42);
    state.set_depth_m(-10.0);
    REQUIRE(state.depth_m() == 0.0);
    state.set_depth_m(10.0);
    REQUIRE(state.depth_m() == SnowpackTuning{}.max_depth_m);
    state.set_depth_m(std::numeric_limits<double>::quiet_NaN());
    REQUIRE(state.depth_m() == 0.0);
    state.set_depth_m(std::numeric_limits<double>::infinity());
    REQUIRE(state.depth_m() == SnowpackTuning{}.max_depth_m);
    apricot_test::pass("manual snow depth is finite and safely clamped");
}

void visual_cover_is_derived_only_from_depth() {
    REQUIRE(visual_snow_cover_from_depth(0.0) == 0.0f);
    REQUIRE_NEAR(visual_snow_cover_from_depth(0.05), 0.5, 1e-6);
    REQUIRE(visual_snow_cover_from_depth(0.10) == 1.0f);
    REQUIRE(visual_snow_cover_from_depth(1.50) == 1.0f);
    REQUIRE(visual_snow_cover_from_depth(-1.0) == 0.0f);
    REQUIRE(visual_snow_cover_from_depth(
        std::numeric_limits<double>::quiet_NaN()) == 0.0f);
    apricot_test::pass("visual cover maps statelessly from physical depth");
}

void collision_is_derived_only_after_the_depth_threshold() {
    const SnowpackCollision dusting = snowpack_collision_from_depth(0.099);
    const SnowpackCollision threshold = snowpack_collision_from_depth(0.10);
    const SnowpackCollision deep = snowpack_collision_from_depth(0.60);
    const SnowpackCollision oversized = snowpack_collision_from_depth(9.0);

    REQUIRE(!dusting.active);
    REQUIRE(dusting.depth_m == 0.0);
    REQUIRE(!threshold.active);
    REQUIRE(threshold.depth_m == 0.0);
    REQUIRE(deep.active);
    REQUIRE_NEAR(deep.depth_m, 0.50, 1e-12);
    REQUIRE(oversized.active);
    REQUIRE_NEAR(oversized.depth_m, 1.40, 1e-12);
    apricot_test::pass("collision raises smoothly above a compressed 10 cm base");
}

void fixed_step_runs_are_deterministic_and_partition_stable() {
    const SnowpackState a = simulate({}, 0.72, 0.18, 1.0 / 120.0, 36000);
    const SnowpackState b = simulate({}, 0.72, 0.18, 1.0 / 120.0, 36000);
    const SnowpackState sixty_hz = simulate({}, 0.72, 0.18, 1.0 / 60.0, 18000);

    REQUIRE(a.depth_m() == b.depth_m());
    REQUIRE_NEAR(a.depth_m(), sixty_hz.depth_m(), 1e-10);
    const SnowpackState unchanged = advance_snowpack(
        a, 1.0, 1.0, std::numeric_limits<double>::infinity());
    REQUIRE(unchanged.depth_m() == a.depth_m());
    apricot_test::pass("fixed-step replay is exact and step partitioning is stable");
}

}  // namespace

int main() {
    accumulation_is_gradual_and_tracks_intensity();
    one_step_never_creates_an_immediate_full_pack();
    depth_stays_bounded();
    snow_stops_and_heat_melts_gradually();
    manual_depth_override_clamps_bad_values();
    visual_cover_is_derived_only_from_depth();
    collision_is_derived_only_after_the_depth_threshold();
    fixed_step_runs_are_deterministic_and_partition_stable();
    return apricot_test::done("snowpack_tests");
}
