// Session conditions and the HUD's data model.
//
// The HUD is plain data computed in apricot_sim, which is the whole reason it
// can be tested at all: a wrong split delta is a failing assertion here rather
// than something somebody notices on a stream three weeks later.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/fixed_step.h"
#include "core/rng.h"
#include "game/conditions.h"
#include "game/ghost.h"
#include "game/rally.h"
#include "game/rally_hud.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr int kGates = 8;
constexpr float kDt = static_cast<float>(kSimDt);

uint64_t nth_seed(int i) {
    return splitmix64_mix(0xB0A7C10Dull + static_cast<uint64_t>(i));
}

void place_and_step(RallyState& s, glm::vec3 p, const TerrainCollider& c) {
    s.car.position = p;
    step_rally(s, InputFrame{}, c, kDt);
}

void drive_through(RallyState& s, const Route& r, int g,
                   const TerrainCollider& c) {
    const Checkpoint& cp = r.checkpoints[static_cast<std::size_t>(g)];
    place_and_step(s, cp.position - cp.forward * 4.0f, c);
    place_and_step(s, cp.position + cp.forward * 4.0f, c);
}

// --- conditions -------------------------------------------------------------

void test_conditions_are_pure_and_bounded() {
    for (int i = 0; i < 8; ++i) {
        const uint64_t seed = nth_seed(i);

        // Sampled out of order on purpose: a pure function does not care, and
        // an accumulator would fall apart here immediately.
        const uint64_t steps[] = {0u, 500000u, 1u, 120u, 250000u, 7u, 999983u};
        for (const uint64_t s : steps) {
            const Conditions a = conditions_at(seed, s);
            const Conditions b = conditions_at(seed, s);

            REQUIRE_MSG(a.time_of_day == b.time_of_day, "time of day is pure",
                        "step");
            REQUIRE(a.rain == b.rain);
            REQUIRE(a.wetness == b.wetness);
            REQUIRE(a.grip == b.grip);
            REQUIRE(a.sun_elevation == b.sun_elevation);
            REQUIRE(a.weather == b.weather);
            REQUIRE(a.daylight == b.daylight);

            REQUIRE_MSG(a.time_of_day >= 0.0f && a.time_of_day < 1.0f,
                        "time of day stays on the clock face", "step");
            REQUIRE(a.rain >= 0.0f && a.rain <= 1.0f);
            REQUIRE(a.wetness >= 0.0f && a.wetness <= 1.0f);
            REQUIRE(a.grip >= kMinGrip && a.grip <= 1.0f);
            REQUIRE(a.headlight_level >= 0.0f && a.headlight_level <= 1.0f);
            REQUIRE(a.sun_elevation >= -1.0001f && a.sun_elevation <= 1.0001f);

            // Rain only ever falls on ground the front has already wet.
            if (a.wetness == 0.0f) REQUIRE(a.rain == 0.0f);
        }
    }
    apricot_test::pass("conditions are a pure, bounded function of (seed, step)");
}

void test_conditions_advance_over_a_session() {
    const uint64_t seed = nth_seed(2);

    // Quarter of a game day should visibly move the sun.
    const uint64_t quarter =
        static_cast<uint64_t>(kSimHz * kSecondsPerDay * 0.25);
    const Conditions dawn = conditions_at(seed, 0);
    const Conditions later = conditions_at(seed, quarter);

    REQUIRE_MSG(dawn.time_of_day != later.time_of_day,
                "the clock moves over a session", "day");
    REQUIRE_NEAR(static_cast<double>(later.time_of_day - dawn.time_of_day), 0.25,
                 1e-3);

    // And the weather is not a constant either. Walk a long session and check
    // that grip actually varies rather than sitting at 1.0 forever.
    float lo = 2.0f;
    float hi = -1.0f;
    bool saw_wet = false;
    bool saw_night = false;
    for (uint64_t s = 0; s < 2000000u; s += 601u) {
        const Conditions c = conditions_at(seed, s);
        if (c.grip < lo) lo = c.grip;
        if (c.grip > hi) hi = c.grip;
        if (c.weather != Weather::Dry) saw_wet = true;
        if (c.daylight == Daylight::Night) saw_night = true;
    }
    std::printf("      grip ranged %.3f..%.3f over the sampled session\n",
                static_cast<double>(lo), static_cast<double>(hi));
    REQUIRE_MSG(hi > lo, "grip is not a constant", "session");
    REQUIRE_MSG(saw_wet, "the session sees weather", "session");
    REQUIRE_MSG(saw_night, "the session sees night", "session");

    apricot_test::pass("time of day and rain advance over a session");
}

void test_conditions_feed_the_grip_term() {
    VehicleTuning base;

    Conditions dry;
    dry.grip = 1.0f;
    dry.wetness = 0.0f;
    const VehicleTuning on_dry = conditioned_tuning(base, dry);
    REQUIRE(on_dry.engine_force == base.engine_force);
    REQUIRE(on_dry.brake_force == base.brake_force);
    REQUIRE(on_dry.rolling_resistance == base.rolling_resistance);

    Conditions soaked;
    soaked.grip = 0.6f;
    soaked.wetness = 1.0f;
    const VehicleTuning on_wet = conditioned_tuning(base, soaked);
    REQUIRE_MSG(on_wet.engine_force < base.engine_force,
                "a wet track puts less power down", "grip");
    REQUIRE_MSG(on_wet.brake_force < base.brake_force,
                "a wet track stops worse", "grip");
    REQUIRE_MSG(on_wet.rolling_resistance > base.rolling_resistance,
                "standing water drags", "grip");

    // The rest of the setup is passed through untouched — this is a grip
    // term, not a second tuning file.
    REQUIRE(on_wet.mass_kg == base.mass_kg);
    REQUIRE(on_wet.max_steer == base.max_steer);
    REQUIRE(on_wet.suspension_rest == base.suspension_rest);

    apricot_test::pass("conditions feed the grip term handed to the vehicle");
}

void test_the_active_condition_is_named() {
    // A renderer and a HUD both need to say what is going on, not infer it.
    REQUIRE(std::strcmp(weather_name(Weather::Dry), "dry") == 0);
    REQUIRE(std::strcmp(weather_name(Weather::Damp), "damp") == 0);
    REQUIRE(std::strcmp(weather_name(Weather::Wet), "wet") == 0);
    REQUIRE(std::strcmp(daylight_name(Daylight::Night), "night") == 0);
    REQUIRE(std::strcmp(daylight_name(Daylight::Dawn), "dawn") == 0);
    REQUIRE(std::strcmp(daylight_name(Daylight::Day), "day") == 0);
    REQUIRE(std::strcmp(daylight_name(Daylight::Dusk), "dusk") == 0);
    apricot_test::pass("the active condition is exposed by name");
}

// --- HUD ---------------------------------------------------------------------

void test_hud_reports_the_car_and_the_route() {
    const uint64_t seed = nth_seed(4);
    const TerrainCollider collider(seed);
    const Route route = build_route(seed, collider, kGates);
    REQUIRE(route_ok(route, kGates));

    RallyState rally;
    rally_reset(rally, route, collider);
    rally.car.velocity = glm::vec3{0.0f, 0.0f, -30.0f};
    rally.car.gear = 3;
    rally.car.engine_rpm = 3900.0f;

    HudModel hud;
    build_hud(rally, nullptr, hud);

    REQUIRE_NEAR(static_cast<double>(hud.speed_ms), 30.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(hud.speed_kph), 108.0, 1e-3);
    REQUIRE(hud.gear == 3);
    REQUIRE(hud.engine_rpm == 3900.0f);
    REQUIRE_NEAR(static_cast<double>(hud.rpm_fraction),
                 3900.0 / static_cast<double>(kDisplayRedlineRpm), 1e-6);

    REQUIRE(hud.gate_count == kGates);
    REQUIRE(hud.gate_index == 0);
    REQUIRE(!hud.lap_started);
    REQUIRE(!hud.has_best);
    REQUIRE(!hud.has_split_delta);
    REQUIRE(!hud.has_ghost);

    // Minimap: one point per gate, in route order, with the bounds already
    // worked out so a renderer does not walk the polyline again.
    REQUIRE(hud.route_points.size() == static_cast<std::size_t>(kGates));
    REQUIRE(hud.route_closed);
    for (int g = 0; g < kGates; ++g) {
        const std::size_t i = static_cast<std::size_t>(g);
        REQUIRE(hud.route_points[i].x == route.checkpoints[i].position.x);
        REQUIRE(hud.route_points[i].y == route.checkpoints[i].position.z);
        REQUIRE(hud.route_points[i].x >= hud.bounds_min.x);
        REQUIRE(hud.route_points[i].x <= hud.bounds_max.x);
        REQUIRE(hud.route_points[i].y >= hud.bounds_min.y);
        REQUIRE(hud.route_points[i].y <= hud.bounds_max.y);
    }
    REQUIRE(hud.car_xz.x == rally.car.position.x);
    REQUIRE(hud.car_xz.y == rally.car.position.z);
    REQUIRE_NEAR(static_cast<double>(glm::length(hud.car_heading_xz)), 1.0, 1e-5);

    // The conditions on show are the ones the step actually used.
    REQUIRE(hud.conditions.grip == rally.conditions.grip);
    REQUIRE(hud.conditions.time_of_day == rally.conditions.time_of_day);

    apricot_test::pass("the HUD reports the car, the route and the conditions");
}

void test_hud_split_delta_against_the_best_lap() {
    const uint64_t seed = nth_seed(5);
    const TerrainCollider collider(seed);
    const Route route = build_route(seed, collider, kGates);
    REQUIRE(route_ok(route, kGates));

    RallyState rally;
    rally_reset(rally, route, collider);

    // A best lap to measure against, hand-built so the expected delta is
    // arithmetic rather than a second run of the thing under test.
    rally.best.valid = true;
    rally.best.seed = seed;
    rally.best.lap_time = 100.0;
    rally.best.splits.assign(static_cast<std::size_t>(kGates), 0.0);
    for (int g = 1; g < kGates; ++g) {
        rally.best.splits[static_cast<std::size_t>(g)] = 10.0 * g;
    }

    HudModel hud;
    build_hud(rally, nullptr, hud);
    REQUIRE(hud.has_best);
    REQUIRE(hud.best_lap == 100.0);
    REQUIRE_MSG(!hud.has_split_delta,
                "no gate crossed yet means no delta, not a delta of zero",
                "delta");

    drive_through(rally, route, 0, collider);
    build_hud(rally, nullptr, hud);
    REQUIRE_MSG(!hud.has_split_delta,
                "the start line is not a split to compare", "delta");

    drive_through(rally, route, 1, collider);
    build_hud(rally, nullptr, hud);
    REQUIRE(hud.has_split_delta);
    REQUIRE(hud.split_gate == 1);

    const double actual = rally.timing.splits[1];
    REQUIRE_NEAR(hud.split_delta, actual - 10.0, 1e-12);
    REQUIRE_MSG(hud.split_delta < 0.0,
                "the teleporting harness is comically faster than 10 s a gate",
                "delta");
    REQUIRE(hud.gate_index == 2);

    apricot_test::pass("the HUD's split delta is measured against the best lap's split");
}

void test_hud_shows_the_ghost_when_there_is_one() {
    const uint64_t seed = nth_seed(6);
    const TerrainCollider collider(seed);
    const Route route = build_route(seed, collider, kGates);
    REQUIRE(route_ok(route, kGates));

    RallyState rally;
    rally_reset(rally, route, collider);

    // A tiny hand-made tape. The ghost only has to exist for the HUD to have
    // something to place.
    ReplayTape tape;
    tape.seed = seed;
    tape.start.step = 0;
    tape.start.checkpoint = 1;
    tape.start.car.position = route.checkpoints[1].position + glm::vec3{0.0f, 5.0f, 0.0f};
    tape.frames.assign(30u, InputFrame{});

    GhostCar ghost;
    ghost_reset(ghost, tape, route, rally.tuning);
    REQUIRE(ghost.active);

    HudModel hud;
    build_hud(rally, &ghost, hud);
    REQUIRE(hud.has_ghost);
    REQUIRE(hud.ghost_xz.x == ghost_car(ghost).position.x);
    REQUIRE(hud.ghost_xz.y == ghost_car(ghost).position.z);

    // Run the ghost out and it stops being drawn.
    while (!ghost.finished) step_ghost(ghost, collider, kDt);
    build_hud(rally, &ghost, hud);
    REQUIRE_MSG(!hud.has_ghost, "a finished ghost is not drawn", "ghost");

    apricot_test::pass("the HUD carries the ghost's position while it is running");
}

void test_time_formatting() {
    char buf[32];

    format_lap_time(0.0, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "0:00.000") == 0);

    format_lap_time(83.456, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "1:23.456") == 0);

    format_lap_time(59.9996, buf, sizeof(buf));
    REQUIRE_MSG(std::strcmp(buf, "1:00.000") == 0,
                "rounding up past a minute rolls the minute, not to :60",
                "format");

    // Nonsense in, something printable out — a HUD must not be the thing that
    // crashes because a clock went backwards.
    format_lap_time(-5.0, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "0:00.000") == 0);
    format_lap_time(std::nan(""), buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "0:00.000") == 0);

    format_delta(1.234, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "+1.23") == 0);
    format_delta(-0.4449, buf, sizeof(buf));
    REQUIRE(std::strcmp(buf, "-0.44") == 0);
    format_delta(0.0, buf, sizeof(buf));
    REQUIRE_MSG(std::strcmp(buf, "+0.00") == 0, "dead level still shows a sign",
                "format");

    apricot_test::pass("lap times and deltas format without surprises");
}

}  // namespace

int main() {
    test_conditions_are_pure_and_bounded();
    test_conditions_advance_over_a_session();
    test_conditions_feed_the_grip_term();
    test_the_active_condition_is_named();
    test_hud_reports_the_car_and_the_route();
    test_hud_split_delta_against_the_best_lap();
    test_hud_shows_the_ghost_when_there_is_one();
    test_time_formatting();
    return apricot_test::done("rally_hud_tests");
}
