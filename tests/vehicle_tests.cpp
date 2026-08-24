// Vehicle dynamics.
//
// Every case here drives a REAL Vehicle over a REAL TerrainCollider. Nothing is
// stubbed, no contact is hand-fed, and no ideal ground plane is invented: the
// expensive failure is a consumer test passing on tidy inputs while the real
// producer feeds garbage, and the tidy input this module would be tempted to
// invent is a flat floor, which is the one surface the car will never meet.
//
// What is deliberately NOT asserted: that any of this feels good. Nobody has
// driven this car. These tests pin behaviour that is true or false — it moves,
// it stops, it stays on the ground, it repeats exactly. Whether the slip curve
// is fun is a question for a human with a controller.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/fixed_step.h"
#include "core/input_frame.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr uint64_t kSeed = 20260823u;
const float kDt = static_cast<float>(kSimDt);

// The flattest spot in a wide search, so tests about ride height and rest are
// not silently testing "does the car roll down a hill". Deterministic: it is a
// scan of a pure function, not a search with a random start.
void flattest_spot(const TerrainCollider& c, float& out_x, float& out_z) {
    float best = 1e9f;
    out_x = 0.0f;
    out_z = 0.0f;
    // Flat under the WHEELBASE, not flat at a point.
    //
    // This used to score `1 - normal(x,z).y` at the centre. The contact normal
    // is the face normal of one drawn triangle and is piecewise constant, so
    // that scores "is this one triangle level" -- and the answer was yes at a
    // spot where the ground under the four wheels still spanned 0.26 m. The car
    // then spawned across a 26 cm step and settled with a lurch that reads as a
    // suspension bug. Score the thing the car actually cares about instead.
    const VehicleTuning probe_tuning;
    for (int i = -40; i <= 40; ++i) {
        for (int j = -40; j <= 40; ++j) {
            const float x = static_cast<float>(i) * 5.0f;
            const float z = static_cast<float>(j) * 5.0f;

            float lo = 1e9f;
            float hi = -1e9f;
            for (int w = 0; w < kWheelCount; ++w) {
                const float ox = (w < 2) ? -probe_tuning.half_track : probe_tuning.half_track;
                const float oz = (w % 2) ? -probe_tuning.half_wheelbase
                                         : probe_tuning.half_wheelbase;
                const float h = c.height(x + ox, z + oz);
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
            const float slope = hi - lo;
            if (slope < best) {
                best = slope;
                out_x = x;
                out_z = z;
            }
        }
    }
}

float ground_clearance(const TerrainCollider& c, const VehicleState& s) {
    return s.position.y - c.height(s.position.x, s.position.z);
}

float front_load(const VehicleState& s) {
    return s.wheels[kWheelFrontLeft].normal_force +
           s.wheels[kWheelFrontRight].normal_force;
}
float rear_load(const VehicleState& s) {
    return s.wheels[kWheelRearLeft].normal_force +
           s.wheels[kWheelRearRight].normal_force;
}
float left_load(const VehicleState& s) {
    return s.wheels[kWheelFrontLeft].normal_force +
           s.wheels[kWheelRearLeft].normal_force;
}
float right_load(const VehicleState& s) {
    return s.wheels[kWheelFrontRight].normal_force +
           s.wheels[kWheelRearRight].normal_force;
}

// Angle in degrees between where the car is pointing and where it is actually
// going, flattened to the horizontal. This is the number that says "the back
// has stepped out"; yaw rate alone cannot, because a car turning tidily on rails
// has a high yaw rate and zero slip.
float signed_body_slip_degrees(const VehicleState& s) {
    const glm::vec3 f = vehicle_forward(s);
    const glm::vec2 heading{f.x, f.z};
    const glm::vec2 travel{s.velocity.x, s.velocity.z};
    if (glm::length(travel) < 1.0f || glm::length(heading) < 1e-3f) return 0.0f;
    const glm::vec2 h = glm::normalize(heading);
    const glm::vec2 v = glm::normalize(travel);
    const float angle =
        std::acos(glm::clamp(glm::dot(h, v), -1.0f, 1.0f)) * 57.29577951f;
    // Sign from which side of the nose the car is actually travelling, so a
    // counter-steer controller knows which way to wind the wheel.
    return (h.x * v.y - h.y * v.x < 0.0f) ? -angle : angle;
}

float body_slip_degrees(const VehicleState& s) {
    return std::fabs(signed_body_slip_degrees(s));
}

// --- exact comparison --------------------------------------------------------
// `==` on floats, not a tolerance. A replay that is "nearly" the same is a
// replay that diverges: the error compounds every step, and the symptom is a
// ghost car that drifts off the road after a minute.
bool same_bits(float a, float b) {
    uint32_t ia = 0u;
    uint32_t ib = 0u;
    std::memcpy(&ia, &a, sizeof(ia));
    std::memcpy(&ib, &b, sizeof(ib));
    return ia == ib;
}
bool same_bits(const glm::vec3& a, const glm::vec3& b) {
    return same_bits(a.x, b.x) && same_bits(a.y, b.y) && same_bits(a.z, b.z);
}

// Field by field rather than a memcmp of the struct: padding bytes are not part
// of the state, and comparing them turns an unrelated compiler change into a
// mysterious replay failure.
bool identical(const VehicleState& a, const VehicleState& b) {
    if (!same_bits(a.position, b.position)) return false;
    if (!same_bits(a.velocity, b.velocity)) return false;
    if (!same_bits(a.angular_velocity, b.angular_velocity)) return false;
    if (!same_bits(a.orientation.w, b.orientation.w)) return false;
    if (!same_bits(a.orientation.x, b.orientation.x)) return false;
    if (!same_bits(a.orientation.y, b.orientation.y)) return false;
    if (!same_bits(a.orientation.z, b.orientation.z)) return false;
    if (!same_bits(a.steer_angle, b.steer_angle)) return false;
    if (!same_bits(a.engine_rpm, b.engine_rpm)) return false;
    if (a.gear != b.gear) return false;
    if (!same_bits(a.shift_timer, b.shift_timer)) return false;
    if (!same_bits(a.recovery_timer, b.recovery_timer)) return false;
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        const WheelState& x = a.wheels[i];
        const WheelState& y = b.wheels[i];
        if (!same_bits(x.contact_point, y.contact_point)) return false;
        if (!same_bits(x.contact_normal, y.contact_normal)) return false;
        if (!same_bits(x.suspension_length, y.suspension_length)) return false;
        if (!same_bits(x.spin, y.spin)) return false;
        if (!same_bits(x.angular_velocity, y.angular_velocity)) return false;
        if (!same_bits(x.normal_force, y.normal_force)) return false;
        if (!same_bits(x.slip, y.slip)) return false;
        if (x.grounded != y.grounded) return false;
    }
    return true;
}

// A tape with something happening in it. A tape of "hold throttle" would pass a
// determinism test that a tape with shifts, handbrake and lift-off would fail.
std::vector<InputFrame> make_tape(int steps) {
    std::vector<InputFrame> tape;
    tape.reserve(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i) {
        const float t = static_cast<float>(i) * kDt;
        InputFrame f;
        f.throttle = (i % 300 < 210) ? 0.9f : 0.0f;
        f.brake = (i % 300 >= 260) ? 0.7f : 0.0f;
        f.steer = 0.8f * std::sin(t * 1.7f) + 0.2f * std::sin(t * 5.1f);
        f.handbrake = (i % 700 >= 660) ? 1.0f : 0.0f;
        if (i % 233 == 0) f.pressed |= kBtnShiftDown;
        if (i % 311 == 0) f.pressed |= kBtnShiftUp;
        tape.push_back(f);
    }
    return tape;
}

VehicleState run_tape(const VehicleTuning& tuning, const TerrainCollider& collider,
                      const VehicleState& start, const std::vector<InputFrame>& tape) {
    VehicleState s = start;
    for (const InputFrame& f : tape) s = step_vehicle(s, tuning, f, collider, kDt);
    return s;
}

// =============================================================================

void the_car_settles_to_a_stable_rest_height() {
    const TerrainCollider collider(kSeed);
    const VehicleTuning tuning;
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider, x, z);

    VehicleState s = spawn_vehicle(tuning, collider, x, z, 0.0f);
    const float spawn_y = s.position.y;

    InputFrame hold;
    hold.brake = 1.0f;  // parked, not merely stationary

    // --- no launch --------------------------------------------------------
    float peak_rise = 0.0f;
    float peak_speed = 0.0f;
    for (int i = 0; i < 120; ++i) {
        s = step_vehicle(s, tuning, hold, collider, kDt);
        peak_rise = std::max(peak_rise, s.position.y - spawn_y);
        peak_speed = std::max(peak_speed, glm::length(s.velocity));
    }
    REQUIRE_MSG(peak_rise < 0.01f, "the car jumped on spawn", "no spawn launch");
    REQUIRE_MSG(peak_speed < 0.05f, "the car was thrown on spawn",
                "no spawn velocity");

    // --- settle -----------------------------------------------------------
    for (int i = 0; i < 480; ++i) s = step_vehicle(s, tuning, hold, collider, kDt);

    std::vector<float> heights;
    heights.reserve(360);
    for (int i = 0; i < 360; ++i) {
        s = step_vehicle(s, tuning, hold, collider, kDt);
        heights.push_back(s.position.y);
    }

    float lo = heights.front();
    float hi = heights.front();
    int direction_changes = 0;
    float last_delta = 0.0f;
    for (std::size_t i = 0; i < heights.size(); ++i) {
        lo = std::min(lo, heights[i]);
        hi = std::max(hi, heights[i]);
        if (i > 0) {
            const float d = heights[i] - heights[i - 1];
            if (d != 0.0f && last_delta != 0.0f && ((d > 0.0f) != (last_delta > 0.0f))) {
                ++direction_changes;
            }
            if (d != 0.0f) last_delta = d;
        }
    }

    const float drift = heights.back() - heights.front();
    const float expected = collider.height(s.position.x, s.position.z) +
                           static_ride_height(tuning);

    std::printf("      (rest y=%.6f, expected %.6f, band %.2e m, drift %.2e m, "
                "%d reversals, N=%.0f)\n",
                static_cast<double>(heights.back()), static_cast<double>(expected),
                static_cast<double>(hi - lo), static_cast<double>(drift),
                direction_changes, static_cast<double>(front_load(s) + rear_load(s)));

    REQUIRE_MSG(hi - lo < 1e-4f, "the resting height is jittering",
                "no jitter at rest");
    REQUIRE_MSG(std::fabs(drift) < 1e-5f, "the car is still sinking (or rising)",
                "no slow sink");
    REQUIRE_MSG(direction_changes <= 2, "the ride height is oscillating",
                "not oscillating");
    REQUIRE_MSG(std::fabs(heights.back() - expected) < 0.02f,
                "settled height is not the height the spring maths predicts",
                "rest height is the computed one");

    // The whole car's weight is on the springs, and it is the whole car's
    // weight — not more, which would mean the guard is helping hold it up.
    const float total = front_load(s) + rear_load(s);
    REQUIRE_NEAR(static_cast<double>(total),
                 static_cast<double>(tuning.mass_kg * tuning.gravity), 60.0);

    apricot_test::pass("the car settles to a stable rest height and stays there");
}

void throttle_drives_and_brake_stops() {
    const TerrainCollider collider(kSeed);
    const VehicleTuning tuning;
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider, x, z);

    VehicleState s = spawn_vehicle(tuning, collider, x, z, 0.0f);
    const glm::vec3 start = s.position;

    InputFrame gas;
    gas.throttle = 1.0f;

    float last = 0.0f;
    for (int i = 0; i < 360; ++i) {
        s = step_vehicle(s, tuning, gas, collider, kDt);
        const float v = vehicle_forward_speed(s);
        // Not monotone step by step — terrain sees to that — but it must be
        // gaining over any half second.
        if (i % 60 == 59) {
            REQUIRE_MSG(v > last, "the car stopped gaining speed on full throttle",
                        "throttle accelerates");
            last = v;
        }
    }
    const float top = vehicle_forward_speed(s);
    REQUIRE_MSG(top > 8.0f, "full throttle for three seconds barely moved the car",
                "throttle actually drives");
    REQUIRE_MSG(glm::length(s.position - start) > 10.0f, "the car went nowhere",
                "throttle covers ground");

    // --- brake ------------------------------------------------------------
    InputFrame stop;
    stop.brake = 1.0f;
    const glm::vec3 brake_from = s.position;
    int steps = 0;
    float previous = glm::length(s.velocity);
    while (glm::length(s.velocity) > 0.15f && steps < 1200) {
        s = step_vehicle(s, tuning, stop, collider, kDt);
        ++steps;
    }
    REQUIRE_MSG(steps < 1200, "full brake never brought the car to rest",
                "brake stops the car");
    REQUIRE_MSG(glm::length(s.velocity) < previous, "braking did not slow the car",
                "brake decelerates");

    const float distance = glm::length(s.position - brake_from);
    std::printf("      (reached %.2f m/s, stopped in %.2f m / %.2f s)\n",
                static_cast<double>(top), static_cast<double>(distance),
                static_cast<double>(steps) * static_cast<double>(kDt));

    // --- and STAYS at rest -------------------------------------------------
    const glm::vec3 rest = s.position;
    for (int i = 0; i < 600; ++i) s = step_vehicle(s, tuning, stop, collider, kDt);
    const float crept = glm::length(s.position - rest);
    REQUIRE_MSG(crept < 0.25f,
                "the car creeps away while parked on the brakes",
                "stays at rest");
    std::printf("      (crept %.4f m over five more seconds on the brakes)\n",
                static_cast<double>(crept));

    apricot_test::pass("throttle drives, brake stops, and it stays stopped");
}

void the_car_stays_on_the_surface_over_a_long_drive() {
    const TerrainCollider collider(kSeed);
    const VehicleTuning tuning;
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider, x, z);

    VehicleState s = spawn_vehicle(tuning, collider, x, z, 0.0f);

    constexpr int kSteps = 120 * 90;  // ninety seconds
    int grounded_steps = 0;
    int all_four_samples = 0;
    float min_settled_clearance = 1e9f;
    float max_settled_clearance = -1e9f;
    float deepest = 1e9f;
    float worst_contact_error = 0.0f;
    float travelled = 0.0f;
    glm::vec3 previous = s.position;

    InputFrame in;
    in.throttle = 0.6f;
    for (int i = 0; i < kSteps; ++i) {
        const float t = static_cast<float>(i) * kDt;
        in.steer = 0.55f * std::sin(t * 0.42f) + 0.2f * std::sin(t * 1.31f);
        s = step_vehicle(s, tuning, in, collider, kDt);

        REQUIRE_MSG(std::isfinite(s.position.x) && std::isfinite(s.position.y) &&
                        std::isfinite(s.position.z),
                    "the car's position went non-finite", "no NaN over a long run");

        travelled += glm::length(s.position - previous);
        previous = s.position;

        deepest = std::min(deepest, ground_clearance(collider, s));

        // The load-bearing check, and it is per WHEEL, not per car: every wheel
        // that claims contact must be touching the actual drawn surface. A
        // chassis-height band cannot say this — a car straddling a dip on two
        // wheels sits legitimately high, and one landing nose-first sits
        // legitimately low.
        int down = 0;
        for (const WheelState& w : s.wheels) {
            if (!w.grounded) continue;
            ++down;
            worst_contact_error = std::max(
                worst_contact_error,
                std::fabs(w.contact_point.y -
                          collider.height(w.contact_point.x, w.contact_point.z)));
        }
        if (down > 0) ++grounded_steps;

        // Ride height is only a meaningful number with all four wheels down.
        if (down == kWheelCount && i > 240) {
            const float clearance = ground_clearance(collider, s);
            min_settled_clearance = std::min(min_settled_clearance, clearance);
            max_settled_clearance = std::max(max_settled_clearance, clearance);
            ++all_four_samples;
        }
    }

    const float grounded_fraction =
        static_cast<float>(grounded_steps) / static_cast<float>(kSteps);
    std::printf("      (%.0f m driven, a wheel down %.0f%% of steps, all four down "
                "%d times, clearance then %.3f..%.3f m)\n",
                static_cast<double>(travelled),
                static_cast<double>(grounded_fraction * 100.0f), all_four_samples,
                static_cast<double>(min_settled_clearance),
                static_cast<double>(max_settled_clearance));
    std::printf("      (lowest centre of mass clearance ever %.3f m; worst contact "
                "off the drawn surface %.2e m)\n",
                static_cast<double>(deepest),
                static_cast<double>(worst_contact_error));

    REQUIRE_MSG(travelled > 800.0f, "the long drive did not go anywhere",
                "test would be vacuous");
    REQUIRE(all_four_samples > 1000);
    // 0.55 was calibrated against the one-octave placeholder heightfield. Real
    // terrain (PENG-6) landed with 96 m peaks and ~42 m of relief every 96 m,
    // and at 0.6 throttle the same drive now measures 53% grounded: the car is
    // jumping crests, which is what a rally car does on that shape.
    //
    // This is deliberately NOT a weakening of the vehicle model. The two hard
    // guarantees below keep their exact thresholds and both still read zero:
    // no wheel contact ever leaves the drawn surface, and the centre of mass
    // never ends a step underneath it. Airborne TIME is a property of the
    // terrain's amplitude, which is a feel decision for a human -- PENG-16.
    // FAILING AS OF PENG-40, AND THE THRESHOLD HAS DELIBERATELY NOT BEEN MOVED.
    // It reads 0.231. It is not measuring airtime any more, so lowering it
    // would relabel a bug as terrain amplitude and bake it in forever.
    //
    // What the car actually does: at t=15 s it rolls, and from t=30 s it lies
    // on its side sliding downhill for the remaining sixty seconds with up.y
    // pinned at +0.30 and recovery_timer at 0.00. It is wedged in a dead band
    // between two tuning constants five hundredths apart:
    //
    //   min_upright_dot  = 0.25  -> below this the SUSPENSION is switched off
    //   recovery_up_dot  = 0.30  -> below this the RIGHTING TORQUE engages
    //
    // Resting between them, the car has neither. The ground guard holds the
    // bodywork out of the terrain and it skates. Crossing 0.30 also RESETS
    // recovery_timer to zero, so recovery_delay (1.5 s) can never elapse once
    // the car is balanced on the line.
    //
    // Pre-existing, not caused by the material fix. The same drive on the old
    // classifier already spent 29.9% of its ninety seconds on its side, and
    // finished doing 39.9 m/s across the SEA FLOOR 33.9 m below sea level —
    // which is where its healthier-looking 53% came from. Lower grip now means
    // less energy to knock the car back off the threshold, so what was
    // intermittent became permanent.
    //
    // Measured candidate fix (NOT applied — it is a feel decision and a new
    // VehicleTuning field): give the engage threshold hysteresis, so recovery
    // holds until the car is properly upright instead of releasing at the same
    // value that engaged it. With a release threshold of 0.75 this drive goes
    // to 81% grounded and 7398 all-four samples, passing at the 0.45 below
    // untouched. Needs its own ticket and a human on the number.
    REQUIRE_MSG(grounded_fraction > 0.45f,
                "the car spent most of the drive in the air", "mostly on the ground");

    // NEVER through the floor: the centre of mass never ends a step below the
    // surface, not once in ninety seconds of real terrain.
    REQUIRE_MSG(deepest > 0.0f, "the car sank through the terrain",
                "never falls through");

    // NEVER floating: every contact the car reported was on the surface the
    // player is looking at, to within float noise.
    REQUIRE_MSG(worst_contact_error < 1e-3f,
                "a wheel reported contact somewhere the ground is not",
                "contacts are on the drawn surface");

    // With all four wheels down the chassis has to be sitting on its springs.
    const float ride = static_ride_height(tuning);
    REQUIRE_MSG(min_settled_clearance > ride - tuning.suspension_travel - 0.2f,
                "the chassis dropped further than the suspension allows",
                "not sunk on all four");
    REQUIRE_MSG(max_settled_clearance < ride + tuning.suspension_travel + 0.6f,
                "the car floats well above its springs with all four wheels down",
                "not floating on all four");

    apricot_test::pass("ninety seconds of varied terrain, never through and never above");
}

void the_same_tape_replays_bit_for_bit() {
    const VehicleTuning tuning;
    // 60 s rather than 25. The steering pattern swings hard both ways, so net
    // displacement grows much more slowly than distance driven — and on real
    // terrain (PENG-6) the car spends part of the run climbing. At 25 s it
    // ended 29.3 m from spawn, which is real motion but a thin guard against
    // the "two cars that never moved are trivially identical" failure this
    // assertion exists to catch.
    const std::vector<InputFrame> tape = make_tape(120 * 60);

    // Two independent colliders built from the same seed. Sharing one object
    // would prove only that the collider is not mutating; separate objects
    // prove the world itself is reproduced from the seed.
    const TerrainCollider collider_a(kSeed);
    const TerrainCollider collider_b(kSeed);

    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider_a, x, z);

    const VehicleState spawn_a = spawn_vehicle(tuning, collider_a, x, z, 0.7f);
    const VehicleState spawn_b = spawn_vehicle(tuning, collider_b, x, z, 0.7f);
    REQUIRE_MSG(identical(spawn_a, spawn_b), "two identical seeds spawned differently",
                "spawn is deterministic");

    const VehicleState end_a = run_tape(tuning, collider_a, spawn_a, tape);
    const VehicleState end_b = run_tape(tuning, collider_b, spawn_b, tape);

    REQUIRE_MSG(identical(end_a, end_b),
                "the same tape produced a different car; replays and ghosts are "
                "worthless from here",
                "bit-identical replay");

    // The run has to have actually gone somewhere, or "identical" is trivially
    // true of two cars that never moved.
    const float tape_travel = glm::length(end_a.position - spawn_a.position);
    std::printf("      (tape moved the car %.1f m from spawn)\n",
                static_cast<double>(tape_travel));
    REQUIRE_MSG(tape_travel > 50.0f,
                "the replay tape barely moved the car", "test would be vacuous");

    // Stepping is pure in its arguments: calling it again from the same state
    // gives the same answer, and the state it was handed is untouched.
    VehicleState before = end_a;
    const VehicleState once = step_vehicle(before, tuning, tape[0], collider_a, kDt);
    const VehicleState twice = step_vehicle(before, tuning, tape[0], collider_a, kDt);
    REQUIRE(identical(once, twice));
    REQUIRE_MSG(identical(before, end_a), "step_vehicle mutated the state it was given",
                "step does not mutate its input");

    // This used to re-run the whole tape a third time through the rally's
    // step_rally, to prove the game layer did not perturb the physics. The
    // rally is gone (PENG-23) and the claim it was making — a tape driven
    // through ReplayTape/replay_input reproduces the run exactly — is pinned
    // against the ENGINE in tests/sim_determinism_tests.cpp, which includes
    // nothing from game/ and therefore survives the next game too.

    std::printf("      (%zu frames, ended at %.3f, %.3f, %.3f — bit for bit)\n",
                tape.size(), static_cast<double>(end_a.position.x),
                static_cast<double>(end_a.position.y),
                static_cast<double>(end_a.position.z));

    apricot_test::pass("the same tape twice gives a bit-identical car");
}

void low_grip_takes_longer_to_stop() {
    const VehicleTuning tuning;

    // One patch of world, painted four ways. Same seed, same terrain, same
    // entry state — the ONLY difference is what the tyres are standing on. A
    // comparison across two different bits of hillside would be measuring the
    // gradient, not the grip.
    TerrainCollider rock(kSeed);
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(rock, x, z);

    AABB stage;
    stage.expand(glm::vec3{x - 1200.0f, -2000.0f, z - 1200.0f});
    stage.expand(glm::vec3{x + 1200.0f, 2000.0f, z + 1200.0f});
    rock.paint_surface(stage, Surface::Rock);

    TerrainCollider gravel(kSeed);
    gravel.paint_surface(stage, Surface::Gravel);
    TerrainCollider sand(kSeed);
    sand.paint_surface(stage, Surface::Sand);
    TerrainCollider soaked(kSeed);
    soaked.paint_surface(stage, Surface::Rock);
    soaked.set_wetness(1.0f);

    // Build the entry state on rock so all four runs start identically.
    VehicleState entry = spawn_vehicle(tuning, rock, x, z, 0.0f);
    InputFrame gas;
    gas.throttle = 1.0f;
    for (int i = 0; i < 480; ++i) entry = step_vehicle(entry, tuning, gas, rock, kDt);
    const float entry_speed = vehicle_forward_speed(entry);
    REQUIRE_MSG(entry_speed > 12.0f, "not enough entry speed to measure a stop",
                "test would be vacuous");

    struct Result {
        const char* name;
        float distance;
        bool stopped;
    };
    const TerrainCollider* surfaces[4] = {&rock, &gravel, &soaked, &sand};
    const char* names[4] = {"rock", "gravel", "wet rock", "sand"};
    Result results[4];

    InputFrame stop;
    stop.brake = 1.0f;
    for (int m = 0; m < 4; ++m) {
        VehicleState s = entry;
        const glm::vec3 from = s.position;
        int steps = 0;
        // Measured down to 1 m/s rather than to zero: the last metre per second
        // is an exponential tail whose length says more about the static
        // friction window than about the surface.
        while (glm::length(s.velocity) > 1.0f && steps < 1500) {
            s = step_vehicle(s, tuning, stop, *surfaces[m], kDt);
            ++steps;
        }
        results[static_cast<std::size_t>(m)] =
            Result{names[m], glm::length(s.position - from), steps < 1500};
    }

    for (const Result& r : results) {
        std::printf("      (%-8s %6.2f m%s)\n", r.name, static_cast<double>(r.distance),
                    r.stopped ? "" : "  [did not reach 1 m/s]");
    }

    REQUIRE_MSG(results[0].stopped, "the car could not stop even on rock",
                "high grip stops");
    REQUIRE_MSG(results[1].distance > results[0].distance * 1.05f,
                "gravel stopped no longer than rock", "gravel is looser than rock");
    REQUIRE_MSG(results[2].distance > results[0].distance * 1.15f,
                "rain made no difference to stopping distance", "rain costs grip");
    REQUIRE_MSG(results[3].distance > results[0].distance * 1.4f,
                "sand stopped nearly as fast as rock", "sand is much looser");

    apricot_test::pass("stopping distance tracks the surface under the tyres");
}

void an_inverted_car_rights_itself() {
    const TerrainCollider collider(kSeed);
    const VehicleTuning tuning;
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider, x, z);

    VehicleState s = spawn_vehicle(tuning, collider, x, z, 0.0f);
    // Flat on its roof, at rest, with no input at all. If it needs the player
    // to help, it is not recovery.
    s.orientation = glm::angleAxis(3.14159265f, glm::vec3{0.0f, 0.0f, 1.0f}) *
                    s.orientation;
    s.position.y = collider.height(x, z) + 1.2f;
    s.velocity = glm::vec3{0.0f};
    s.angular_velocity = glm::vec3{0.0f};
    REQUIRE(vehicle_up(s).y < -0.9f);

    constexpr int kBudget = 120 * 12;
    int righted = -1;
    float biggest_jump = 0.0f;
    glm::vec3 previous = s.position;
    const InputFrame idle;

    for (int i = 1; i <= kBudget; ++i) {
        s = step_vehicle(s, tuning, idle, collider, kDt);
        biggest_jump = std::max(biggest_jump, glm::length(s.position - previous));
        previous = s.position;
        if (righted < 0 && vehicle_up(s).y > 0.8f) righted = i;
    }

    std::printf("      (upright after %d steps / %.2f s, largest single-step move "
                "%.4f m, final up.y=%.3f)\n",
                righted, static_cast<double>(righted) * static_cast<double>(kDt),
                static_cast<double>(biggest_jump),
                static_cast<double>(vehicle_up(s).y));

    REQUIRE_MSG(righted > 0, "an inverted car never recovered", "recovery happens");
    REQUIRE_MSG(righted <= kBudget, "recovery took longer than the budget",
                "recovery is bounded");
    // A nudge, not a teleport: at 120 Hz, 0.15 m in a step is 18 m/s.
    REQUIRE_MSG(biggest_jump < 0.15f, "the car was teleported upright",
                "recovery is a nudge");
    // Waited its turn rather than snapping over instantly.
    REQUIRE_MSG(static_cast<float>(righted) * kDt > tuning.recovery_delay,
                "recovery fired before the delay elapsed", "recovery waits");

    // And it stays up, on its wheels.
    for (int i = 0; i < 600; ++i) s = step_vehicle(s, tuning, idle, collider, kDt);
    REQUIRE(vehicle_up(s).y > 0.8f);
    REQUIRE_MSG(!vehicle_airborne(s), "the righted car never came back down",
                "recovery ends on the wheels");

    apricot_test::pass("an inverted car rights itself, slowly, without a teleport");
}

// --- the rest: the handling requirements, each pinned to a number ------------

void load_transfers_under_acceleration_braking_and_cornering() {
    const TerrainCollider collider(kSeed);
    const VehicleTuning tuning;
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider, x, z);

    VehicleState rolling = spawn_vehicle(tuning, collider, x, z, 0.0f);
    InputFrame gas;
    gas.throttle = 0.9f;
    for (int i = 0; i < 300; ++i) rolling = step_vehicle(rolling, tuning, gas, collider, kDt);

    // Branch from ONE state so both samples sit on the same patch of hillside.
    // Comparing two runs that ended up in different places measures the slope.
    VehicleState accelerating = rolling;
    VehicleState braking = rolling;
    VehicleState straight = rolling;
    VehicleState turning = rolling;

    InputFrame stop;
    stop.brake = 1.0f;
    InputFrame turn = gas;
    turn.steer = 0.9f;

    for (int i = 0; i < 90; ++i) {
        accelerating = step_vehicle(accelerating, tuning, gas, collider, kDt);
        braking = step_vehicle(braking, tuning, stop, collider, kDt);
        straight = step_vehicle(straight, tuning, gas, collider, kDt);
        turning = step_vehicle(turning, tuning, turn, collider, kDt);
    }

    std::printf("      (accel F=%.0f R=%.0f | brake F=%.0f R=%.0f | turn L=%.0f R=%.0f)\n",
                static_cast<double>(front_load(accelerating)),
                static_cast<double>(rear_load(accelerating)),
                static_cast<double>(front_load(braking)),
                static_cast<double>(rear_load(braking)),
                static_cast<double>(left_load(turning)),
                static_cast<double>(right_load(turning)));

    REQUIRE_MSG(rear_load(accelerating) > front_load(accelerating),
                "the car does not squat onto its rear under power",
                "weight transfers rearward under acceleration");
    REQUIRE_MSG(front_load(braking) > rear_load(braking),
                "the car does not dive onto its nose under braking",
                "weight transfers forward under braking");
    REQUIRE_MSG(front_load(braking) > front_load(accelerating),
                "braking and accelerating load the front axle the same",
                "braking loads the front more than power does");

    // Steering right rolls the load onto the left-hand wheels.
    const float roll = left_load(turning) - right_load(turning);
    const float baseline = left_load(straight) - right_load(straight);
    REQUIRE_MSG(roll > baseline + 500.0f,
                "cornering does not transfer load across the car",
                "weight transfers outward in a corner");

    apricot_test::pass("load transfers under power, brakes and cornering");
}

void the_handbrake_breaks_the_rear_loose() {
    const TerrainCollider collider(kSeed);
    const VehicleTuning tuning;
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider, x, z);

    VehicleState entry = spawn_vehicle(tuning, collider, x, z, 0.0f);
    InputFrame gas;
    gas.throttle = 0.9f;
    for (int i = 0; i < 480; ++i) entry = step_vehicle(entry, tuning, gas, collider, kDt);

    float peak_slip[2] = {0.0f, 0.0f};
    float peak_rear_slip[2] = {0.0f, 0.0f};
    for (int pull = 0; pull < 2; ++pull) {
        VehicleState s = entry;
        InputFrame in;
        in.throttle = 0.3f;
        in.steer = 0.75f;
        in.handbrake = (pull != 0) ? 1.0f : 0.0f;
        for (int i = 0; i < 200; ++i) {
            s = step_vehicle(s, tuning, in, collider, kDt);
            peak_slip[pull] = std::max(peak_slip[pull], body_slip_degrees(s));
            peak_rear_slip[pull] =
                std::max(peak_rear_slip[pull], s.wheels[kWheelRearLeft].slip);
        }
    }

    std::printf("      (body slip %.1f deg -> %.1f deg with the handbrake; rear tyre "
                "slip %.2f -> %.2f)\n",
                static_cast<double>(peak_slip[0]), static_cast<double>(peak_slip[1]),
                static_cast<double>(peak_rear_slip[0]),
                static_cast<double>(peak_rear_slip[1]));

    REQUIRE_MSG(peak_slip[1] > peak_slip[0] * 1.5f,
                "the handbrake did not make the car step out",
                "handbrake rotates the car");
    REQUIRE_MSG(peak_rear_slip[1] > peak_rear_slip[0] * 1.5f,
                "the rear tyres are not slipping any more with the handbrake up",
                "handbrake breaks rear traction");
    // Past the peak of the curve, which is what "let go" means.
    REQUIRE_MSG(peak_rear_slip[1] > 1.0f,
                "the rear tyres never left the grippy side of the slip curve",
                "rear is genuinely sliding");

    apricot_test::pass("the handbrake breaks the rear loose");
}

// The other half of the requirement, and the half that is easy to get wrong:
// the back stepping out is only good if the driver can gather it back up.
//
// Catchability cannot be tested with a fixed input — hold full opposite lock
// for two seconds and you swing the car the other way, which says nothing about
// the car. So this closes the loop: a plain proportional counter-steer, the
// crudest possible driver. If the slide is recoverable at all, that is enough
// to recover it; if the model snapped, no gain would save it.
void a_slide_can_be_caught() {
    const TerrainCollider collider(kSeed);
    const VehicleTuning tuning;
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider, x, z);

    VehicleState entry = spawn_vehicle(tuning, collider, x, z, 0.0f);
    InputFrame gas;
    gas.throttle = 0.9f;
    for (int i = 0; i < 480; ++i) entry = step_vehicle(entry, tuning, gas, collider, kDt);

    // Provoke: half a second of handbrake and lock.
    InputFrame yank;
    yank.throttle = 0.3f;
    yank.steer = 0.75f;
    yank.handbrake = 1.0f;
    VehicleState sliding = entry;
    for (int i = 0; i < 60; ++i) {
        sliding = step_vehicle(sliding, tuning, yank, collider, kDt);
    }
    const float provoked = body_slip_degrees(sliding);
    REQUIRE_MSG(provoked > 5.0f, "the handbrake never got the car sideways",
                "test would be vacuous");

    // Branch A: a driver catches it.
    constexpr float kCounterSteerGain = 0.04f;  // lock per degree of slip
    VehicleState caught = sliding;
    float caught_peak = provoked;
    for (int i = 0; i < 360; ++i) {
        InputFrame in;
        in.throttle = 0.3f;
        in.steer = glm::clamp(-kCounterSteerGain * signed_body_slip_degrees(caught),
                              -1.0f, 1.0f);
        caught = step_vehicle(caught, tuning, in, collider, kDt);
        caught_peak = std::max(caught_peak, body_slip_degrees(caught));
    }

    // Branch B: nobody touches anything.
    VehicleState abandoned = sliding;
    for (int i = 0; i < 360; ++i) {
        InputFrame in;
        in.throttle = 0.3f;
        abandoned = step_vehicle(abandoned, tuning, in, collider, kDt);
    }

    std::printf("      (provoked to %.1f deg; counter-steered back to %.1f deg at "
                "%.1f m/s, peaking at %.1f; ignored it settles at %.1f deg)\n",
                static_cast<double>(provoked),
                static_cast<double>(body_slip_degrees(caught)),
                static_cast<double>(glm::length(caught.velocity)),
                static_cast<double>(caught_peak),
                static_cast<double>(body_slip_degrees(abandoned)));

    REQUIRE_MSG(body_slip_degrees(caught) < 3.0f,
                "counter-steering could not gather the slide back up",
                "a slide is catchable");
    REQUIRE_MSG(glm::length(caught.velocity) > 8.0f,
                "the car only stopped sliding by stopping", "caught, not parked");
    // And the catch has to be the DRIVER's doing. If the car straightened
    // itself out with the wheel left alone, the slip curve is not really
    // falling off past the peak and none of this is a handling model.
    REQUIRE_MSG(body_slip_degrees(abandoned) > 10.0f,
                "the car recovered on its own; the tyre model is not losing grip "
                "past the peak at all",
                "a slide does not fix itself");

    apricot_test::pass("a slide can be caught, and does not fix itself");
}

void the_gearbox_exposes_real_revs_and_a_gear() {
    const TerrainCollider collider(kSeed);
    const VehicleTuning tuning;
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider, x, z);

    VehicleState s = spawn_vehicle(tuning, collider, x, z, 0.0f);
    REQUIRE(s.gear == 1);
    REQUIRE_NEAR(static_cast<double>(s.engine_rpm),
                 static_cast<double>(tuning.engine_idle_rpm), 1e-3);

    InputFrame gas;
    gas.throttle = 1.0f;
    int32_t highest = s.gear;
    float highest_rpm = s.engine_rpm;
    for (int i = 0; i < 120 * 12; ++i) {
        s = step_vehicle(s, tuning, gas, collider, kDt);
        highest = std::max(highest, s.gear);
        highest_rpm = std::max(highest_rpm, s.engine_rpm);
        REQUIRE_MSG(s.engine_rpm >= tuning.engine_idle_rpm - 1e-3f,
                    "revs fell below idle; the engine stalled", "idle floor");
        REQUIRE_MSG(s.engine_rpm <= tuning.engine_redline_rpm * 1.06f,
                    "revs went past the limiter", "rev limiter holds");
    }
    std::printf("      (climbed to gear %d, peak %.0f rpm at %.1f m/s)\n", highest,
                static_cast<double>(highest_rpm),
                static_cast<double>(vehicle_forward_speed(s)));
    REQUIRE_MSG(highest >= 3, "the gearbox never got out of second",
                "gearbox climbs under power");
    REQUIRE_MSG(highest_rpm > tuning.engine_peak_rpm,
                "the engine never revved past its torque peak", "revs are real");

    // Revs must follow the wheels, not a clock: at the same road speed in a
    // lower gear the engine has to be turning faster.
    const int32_t tall = s.gear;
    VehicleState lower = s;
    InputFrame down;
    down.pressed |= kBtnShiftDown;
    lower = step_vehicle(lower, tuning, down, collider, kDt);
    REQUIRE_MSG(lower.gear == tall - 1, "a manual downshift did nothing",
                "manual shift works");
    REQUIRE_MSG(lower.engine_rpm > s.engine_rpm,
                "dropping a gear at the same speed did not raise the revs",
                "rpm is derived from the wheels");

    // The cooldown stops one held button from walking through the whole box —
    // a frame that owes several steps sees the same latched press every time.
    VehicleState spam = s;
    for (int i = 0; i < 6; ++i) spam = step_vehicle(spam, tuning, down, collider, kDt);
    REQUIRE_MSG(spam.gear == tall - 1, "one latched press shifted several gears",
                "shift cooldown holds");

    apricot_test::pass("gear and RPM are real values the HUD and audio can use");
}

// The case the rest of the engine actually produces today.
//
// A DEFAULT-constructed VehicleState sits at the world origin with no regard
// for where the ground is, and on roughly half of all seeds that is inside a
// hill. Any shell that declares a car as a member and starts stepping it hands
// the physics exactly this. src/app/ does park its car on the terrain first,
// deliberately — but that is app being careful, not the module being safe, and
// the next caller will not be. This module does not
// get to call that the caller's problem — a car that is somewhere impossible
// has to end up somewhere possible, without being fired into orbit on the way.
void a_car_put_somewhere_impossible_ends_up_somewhere_possible() {
    const VehicleTuning tuning;
    const InputFrame idle;
    const float ride = static_ride_height(tuning);

    struct Case {
        const char* name;
        uint64_t seed;
        bool bury;
    };
    const Case cases[] = {
        {"default state at the origin", 0u, false},
        {"default state at the origin", 1u, false},
        {"default state at the origin", kSeed, false},
        {"buried twelve metres deep", kSeed, true},
        {"buried twelve metres deep", 1u, true},
    };

    for (const Case& c : cases) {
        const TerrainCollider collider(c.seed);
        VehicleState s;  // exactly what a default-constructed shell hands over
        if (c.bury) {
            float x = 0.0f;
            float z = 0.0f;
            flattest_spot(collider, x, z);
            s.position = glm::vec3{x, collider.height(x, z) - 12.0f, z};
        }
        const float start_offset =
            s.position.y - collider.height(s.position.x, s.position.z);

        float fastest_upward = 0.0f;
        for (int i = 0; i < 120 * 20; ++i) {
            s = step_vehicle(s, tuning, idle, collider, kDt);
            fastest_upward = std::max(fastest_upward, s.velocity.y);
            REQUIRE_MSG(std::isfinite(s.position.y) && std::isfinite(s.velocity.y),
                        "the car went non-finite recovering from a bad spawn",
                        "no NaN from a bad spawn");
        }

        const float clearance = ground_clearance(collider, s);
        std::printf("      (%s, seed %llu: started %+.1f m from the surface, fastest "
                    "climb %.2f m/s, settled at %.3f m)\n",
                    c.name, static_cast<unsigned long long>(c.seed),
                    static_cast<double>(start_offset),
                    static_cast<double>(fastest_upward),
                    static_cast<double>(clearance));

        // The bug this pins: reading burial depth as spring compression put
        // five meganewtons through the bumpstops and threw the car a hundred
        // metres up. Upward velocity is the tell — a car that is merely falling
        // or being walked out of the ground never gains any.
        REQUIRE_MSG(fastest_upward < 5.0f,
                    "the car was launched recovering from a bad spawn",
                    "no launch out of the ground");
        REQUIRE_MSG(vehicle_up(s).y > 0.9f, "the car ended up on its roof",
                    "settles upright");
        REQUIRE_MSG(!vehicle_airborne(s), "the car never found the ground",
                    "settles on its wheels");
        REQUIRE_MSG(std::fabs(clearance - ride) < 0.15f,
                    "the car did not settle to its ride height",
                    "settles to ride height");
    }

    apricot_test::pass("a car spawned inside a hill walks out instead of launching");
}

void props_are_solid() {
    TerrainCollider collider(kSeed);
    const VehicleTuning tuning;
    float x = 0.0f;
    float z = 0.0f;
    flattest_spot(collider, x, z);

    // A wall 25 m ahead. Forward is -Z.
    const float wall_z = z - 25.0f;
    AABB wall;
    wall.expand(glm::vec3{x - 30.0f, collider.height(x, wall_z) - 2.0f, wall_z - 1.5f});
    wall.expand(glm::vec3{x + 30.0f, collider.height(x, wall_z) + 3.0f, wall_z + 1.5f});
    collider.add_static_box(wall, Surface::Rock);

    VehicleState s = spawn_vehicle(tuning, collider, x, z, 0.0f);
    InputFrame gas;
    gas.throttle = 1.0f;

    float closest = 1e9f;
    bool went_through = false;
    for (int i = 0; i < 120 * 15; ++i) {
        s = step_vehicle(s, tuning, gas, collider, kDt);
        closest = std::min(closest, s.position.z - wall.max.z);
        if (s.position.z < wall.min.z) went_through = true;
    }

    std::printf("      (closest approach to the wall %.3f m, radius %.2f m)\n",
                static_cast<double>(closest),
                static_cast<double>(tuning.chassis_collision_radius));
    REQUIRE_MSG(!went_through, "the car drove straight through a solid prop",
                "props stop the car");
    REQUIRE_MSG(closest < 5.0f, "the car never reached the wall",
                "test would be vacuous");

    apricot_test::pass("static prop boxes are solid");
}

}  // namespace

int main() {
    std::printf("vehicle_tests\n");
    the_car_settles_to_a_stable_rest_height();
    throttle_drives_and_brake_stops();
    the_car_stays_on_the_surface_over_a_long_drive();
    the_same_tape_replays_bit_for_bit();
    low_grip_takes_longer_to_stop();
    an_inverted_car_rights_itself();
    load_transfers_under_acceleration_braking_and_cornering();
    the_handbrake_breaks_the_rear_loose();
    a_slide_can_be_caught();
    the_gearbox_exposes_real_revs_and_a_gear();
    a_car_put_somewhere_impossible_ends_up_somewhere_possible();
    props_are_solid();
    return apricot_test::done("vehicle_tests");
}
