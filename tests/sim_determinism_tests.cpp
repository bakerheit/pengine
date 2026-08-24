// THE HEADLINE SUITE. The engine's central claim, tested against the ENGINE.
//
// Stated exactly: a recorded tape of InputFrames, replayed through
// step_vehicle against a TerrainCollider built from the same seed, reproduces
// the drive BIT FOR BIT — every component of every VehicleState at every step.
// Not "closely". Not "to within a tolerance". The same bits.
//
// WHY THIS FILE EXISTS SEPARATELY FROM rally_replay_tests.cpp.
//
// That suite proves the same claim, but it proves it through RallyState, Route
// and the gate sequencer, because a time-trial game happened to be the sample.
// The sample is being replaced. If the only proof of bit-exact replay leaves
// with the sample, the sim is unpinned at precisely the moment a new game is
// being built on top of it, and the first replay desync of the new game is
// discovered by a player rather than by CI.
//
// So nothing below includes anything from game/. The claim belongs to
// step_vehicle, TerrainCollider, InputFrame, hash_coord and FixedStep — all of
// which outlive any particular game — and this suite is written against
// exactly those. A future game gets the guarantee for free.
//
// WHAT IS DELIBERATELY NOT HERE: kReplayTapeVersion, and the ReplayTape /
// ReplayStart structs, which live in game/replay.h. The FORMAT they version is
// pinned below byte for byte; the version CONSTANT is not, because reaching
// into game/ to fetch it is the exact coupling this file exists to remove.
// Rehoming that constant into core/ is a follow-up, and until it happens the
// pin that matters — InputFrame's layout — is here.
//
// Every comparison is `==` on the value, never REQUIRE_NEAR. A tolerance here
// accepts exactly the drift this whole architecture exists to prevent: a wheel
// contact a millimetre out becomes a landing a metre out becomes a different
// race.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "core/fixed_step.h"
#include "core/input_frame.h"
#include "core/rng.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "terrain/chunk.h"
#include "terrain/heightmap.h"
#include "terrain/scatter.h"
#include "terrain/surface.h"
#include "test_assert.h"

// THOSE LAST TWO INCLUDES ARE A REGRESSION TEST IN THEIR OWN RIGHT.
//
// terrain/surface.h and terrain/scatter.h used to be excluded from this suite,
// with a comment explaining why: src/terrain/surface.h and src/physics/surface.h
// each defined a DIFFERENT struct called apricot::SurfaceProperties, and
// physics/surface.h arrives here through terrain_collider.h, which this suite
// cannot do without. Two definitions of one class name, both linked into
// apricot_sim, is an ODR violation in the shipped library — not a test-side
// inconvenience — and the visible cost was that prop scatter could not be
// pinned in the suite that carries the engine's central claim.
//
// PENG-40 removed it. There is one material enum (terrain's `Surface`) and
// physics' properties struct is `TyreSurface`. If somebody reintroduces a
// clashing name, THIS FILE STOPS COMPILING, which is the loudest and cheapest
// place for that to be discovered.

using namespace apricot;

namespace {

constexpr float kDt = static_cast<float>(kSimDt);

// Two worlds. kSeed is the one everything is recorded in; kOtherSeed exists
// only so the negative control has somewhere else to go.
constexpr uint64_t kSeed = 0x0A9C0DE7EA5Eull;
constexpr uint64_t kOtherSeed = 0xC0FFEE1234ull;

// 2400 steps = 20 seconds of sim at 120 Hz.
constexpr int kRecordSteps = 2400;

// Where the recorded run starts. Comfortably inside kHomeRadiusMetres (380 m),
// which is the band the height field guarantees is dry land — a spawn in a
// lagoon records a car bobbing in the sea, which reproduces perfectly and
// proves nothing.
constexpr float kSpawnX = -60.0f;
constexpr float kSpawnZ = 40.0f;
constexpr float kSpawnYaw = 0.7f;

// --- the recorded drive ------------------------------------------------------
//
// Every constant below was MEASURED against this seed, not guessed, and the
// measurement is written next to it because a tuning number without its
// evidence is a number the next person changes for free.

// Proportional gain from yaw error to stick.
constexpr float kSteerGain = 1.2f;

// Target speeds on a straight and through a turn, m/s.
//
// NOT flat out, and that is the whole point. This tuning will carry the car
// past 40 m/s, and at 40 m/s over this terrain it takes off on the first crest
// and spends the rest of the tape on its roof. An inverted car still replays
// bit-for-bit, so the suite would stay green while testing an integrator
// falling over. Governed to these speeds the measured run is 0 of 2400 steps
// inverted and 312 airborne — enough air to exercise suspension extension and
// re-contact, not enough to become a barrel roll.
constexpr float kDriveSpeed = 15.0f;
constexpr float kCornerSpeed = 11.0f;

// Yaw error past which the driver treats it as a turn and slows down, radians.
constexpr float kCornerYawError = 0.35f;

// The heading the driver chases is a slow sine about kSpawnYaw: a long S rather
// than a straight line, so the tape has to reproduce steering, weight transfer
// and both directions of slip instead of a car pointed at the horizon.
constexpr float kSwingRadians = 0.9f;
constexpr float kSwingSteps = 900.0f;

// Scheduled events, by step index. Open-loop on purpose — a handbrake pull that
// fires "when the car is sideways" is a different tape on every code change,
// and the point of these is that they land in known places so the assertions
// below can name them.
constexpr int kHandbrakeFrom = 900;
constexpr int kHandbrakeTo = 960;
constexpr int kHandbrakeAgainFrom = 1800;
constexpr int kHandbrakeAgainTo = 1840;
constexpr int kBrakeFrom = 1500;
constexpr int kBrakeTo = 1560;

// Manual gear presses. These ride in InputFrame::pressed, which is the LATCHED
// edge mask — the same mechanism the handbrake and respawn use — so a tape that
// reproduces these has reproduced the latched-edge path and not just the axes.
constexpr int kShiftDownAt[] = {300, 1200};
constexpr int kShiftUpAt[] = {330, 700, 1240};

// ANTI-VACUITY FLOORS. Measured on this seed: 222.7 m of path, 177.2 m of net
// displacement, top speed 15.7 m/s. The floors sit well under those, because
// their job is not to pin the trajectory — it is to fail loudly on the day a
// change quietly stops the car, at which point "the replay is bit-identical"
// becomes true of two cars sitting still and this suite becomes decoration.
constexpr double kMinPathMetres = 120.0;
constexpr double kMinNetMetres = 80.0;
constexpr float kMinTopSpeed = 8.0f;

bool in_window(int step, int from, int to) { return step >= from && step < to; }

bool step_is_in(int step, const int* list, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (list[i] == step) return true;
    }
    return false;
}

// The input a driver would give, not a dice roll.
//
// Uniform random steer against real dynamics does not drive, it pirouettes:
// the car ends the tape roughly where it started and the anti-vacuity floors
// below catch it. So the RECORDING is closed-loop — it aims at a heading and
// governs a speed — with deterministic variety layered on top at reduced
// amplitude. The tape is still a plain stream of InputFrames and the REPLAY is
// still open-loop off it, which is exactly how a real ghost lap is captured.
//
// The variety is keyed by hash_coord rather than pulled from a sequential
// stream, so frame N is the same whether or not frames 0..N-1 were ever asked
// for. core/rng.h sets that rule out and this is what obeying it looks like.
InputFrame drive_input(const VehicleState& car, uint64_t seed, int step) {
    Rng rng = rng_at(seed, static_cast<int32_t>(step), 0, 77u);

    InputFrame f;
    f.look_dx = rng.range(-0.05f, 0.05f);
    f.look_dy = rng.range(-0.02f, 0.02f);
    f.held = rng.chance(0.2f) ? kBtnLookBack : 0u;

    const float phase = static_cast<float>(step) / kSwingSteps * kTwoPi;
    const float target_yaw = kSpawnYaw + kSwingRadians * std::sin(phase);

    // Transform::forward() is rotation * (0, 0, -1), so a yaw of theta about +Y
    // points at (-sin theta, 0, -cos theta). Both negations are load-bearing:
    // without them the driver chases a heading exactly behind the car, the yaw
    // error pins at pi, and the controller has nothing to turn against.
    const glm::vec3 want{-std::sin(target_yaw), 0.0f, -std::cos(target_yaw)};

    const glm::vec3 fwd = vehicle_forward(car);
    glm::vec3 flat{fwd.x, 0.0f, fwd.z};
    float err = 0.0f;
    if (glm::length(flat) > 1e-3f) {
        flat = glm::normalize(flat);
        // Signed yaw error: the cross product's Y tells us which way to turn.
        // The gain is POSITIVE — physics/vehicle.cpp builds the steered wheel
        // direction as forward*cos(steer) + right*sin(steer), so positive steer
        // goes the same way a positive error points. Negated it is not a sloppy
        // controller, it is a diverging one.
        const float sin_err = flat.x * want.z - flat.z * want.x;
        const float cos_err = glm::dot(flat, want);
        err = std::atan2(sin_err, cos_err);
    }
    f.steer =
        glm::clamp(err * kSteerGain + 0.12f * rng.range(-1.0f, 1.0f), -1.0f, 1.0f);

    // Governed to a target SPEED rather than held at a throttle opening. A
    // throttle number is a request; a speed is a promise, and the car only
    // stays on its wheels if somebody makes the second one.
    const float speed = glm::length(car.velocity);
    const float target =
        (std::fabs(err) > kCornerYawError) ? kCornerSpeed : kDriveSpeed;
    f.throttle = (speed < target) ? 0.85f : 0.0f;
    f.brake = (speed > target + 4.0f) ? 0.45f : 0.0f;

    if (in_window(step, kHandbrakeFrom, kHandbrakeTo)) f.handbrake = 0.8f;
    if (in_window(step, kHandbrakeAgainFrom, kHandbrakeAgainTo)) f.handbrake = 0.55f;
    if (in_window(step, kBrakeFrom, kBrakeTo)) {
        f.brake = 0.9f;
        f.throttle = 0.0f;
    }

    if (step_is_in(step, kShiftDownAt, sizeof kShiftDownAt / sizeof kShiftDownAt[0])) {
        f.pressed |= kBtnShiftDown;
    }
    if (step_is_in(step, kShiftUpAt, sizeof kShiftUpAt / sizeof kShiftUpAt[0])) {
        f.pressed |= kBtnShiftUp;
    }
    return f;
}

// ---------------------------------------------------------------------------
//  The comparison helper, and the proof that it reads everything
// ---------------------------------------------------------------------------
//
// A helper that quietly skips a field is WORSE than no test, because it reads
// as coverage. This is not hypothetical here: game/best_lap.cpp once dropped 14
// VehicleState fields on the way to disk — every wheel's angular_velocity among
// them — and it went unnoticed for a whole ticket because the suite's own
// comparison skipped those exact fields. The headline said "bit for bit, every
// component" and it was true of two thirds of the state.
//
// So the two functions below compare every declared field, and
// test_the_comparison_helper_reads_every_field() PROVES it by flipping one bit
// at every byte offset of a real VehicleState and requiring that every byte
// belonging to a declared member is noticed.

bool same_wheel(const WheelState& a, const WheelState& b) {
    return a.contact_point.x == b.contact_point.x &&
           a.contact_point.y == b.contact_point.y &&
           a.contact_point.z == b.contact_point.z &&
           a.contact_normal.x == b.contact_normal.x &&
           a.contact_normal.y == b.contact_normal.y &&
           a.contact_normal.z == b.contact_normal.z &&
           a.suspension_length == b.suspension_length &&  //
           a.spin == b.spin &&                            //
           a.angular_velocity == b.angular_velocity &&    //
           a.normal_force == b.normal_force &&            //
           a.slip == b.slip &&                            //
           a.grounded == b.grounded;
}

bool same_vehicle(const VehicleState& a, const VehicleState& b) {
    if (a.position.x != b.position.x || a.position.y != b.position.y ||
        a.position.z != b.position.z) {
        return false;
    }
    if (a.orientation.w != b.orientation.w || a.orientation.x != b.orientation.x ||
        a.orientation.y != b.orientation.y || a.orientation.z != b.orientation.z) {
        return false;
    }
    if (a.velocity.x != b.velocity.x || a.velocity.y != b.velocity.y ||
        a.velocity.z != b.velocity.z) {
        return false;
    }
    if (a.angular_velocity.x != b.angular_velocity.x ||
        a.angular_velocity.y != b.angular_velocity.y ||
        a.angular_velocity.z != b.angular_velocity.z) {
        return false;
    }
    if (a.steer_angle != b.steer_angle) return false;
    if (a.engine_rpm != b.engine_rpm) return false;
    if (a.gear != b.gear) return false;
    if (a.shift_timer != b.shift_timer) return false;
    if (a.recovery_timer != b.recovery_timer) return false;
    for (int i = 0; i < kWheelCount; ++i) {
        const std::size_t w = static_cast<std::size_t>(i);
        if (!same_wheel(a.wheels[w], b.wheels[w])) return false;
    }
    return true;
}

// --- the machinery that proves it -------------------------------------------

struct FieldSpan {
    std::string name;
    std::size_t offset;
    std::size_t size;
};

// Where a member sits inside its object. Spelled with pointer arithmetic rather
// than offsetof() because offsetof on a type carrying glm members is only
// conditionally supported; both types below are standard layout and trivially
// copyable (asserted), so this is exact.
template <typename Base, typename Member>
std::size_t member_offset(const Base& base, const Member& member) {
    return static_cast<std::size_t>(reinterpret_cast<const char*>(&member) -
                                    reinterpret_cast<const char*>(&base));
}

template <typename Base, typename Member>
void add_span(std::vector<FieldSpan>& out, const Base& base, const char* name,
              const Member& member) {
    out.push_back({std::string(name), member_offset(base, member), sizeof(Member)});
}

static_assert(std::is_trivially_copyable_v<VehicleState>,
              "the byte scan below copies a VehicleState through a byte buffer");
static_assert(std::is_standard_layout_v<VehicleState>,
              "member offsets below are taken by pointer arithmetic");
static_assert(std::is_trivially_copyable_v<WheelState>, "same, for a wheel");
static_assert(std::is_standard_layout_v<WheelState>, "same, for a wheel");

// PINNED SIZES. These are here so that ADDING A FIELD fails this suite rather
// than passing it. The byte scan catches a field appended to either struct on
// its own; these catch the one case the scan cannot see unaided — a small field
// tucked into an existing alignment hole, which changes neither size.
//
// If you are reading this because one of them fired: you added a field. Add it
// to the span table below AND to same_wheel()/same_vehicle(), then update the
// number. Do not just update the number.
static_assert(sizeof(WheelState) == 48,
              "WheelState changed shape: update kWheelSpans and same_wheel()");
static_assert(sizeof(VehicleState) == 264,
              "VehicleState changed shape: update vehicle_spans() and same_vehicle()");

// Every declared member of a WheelState, in order.
std::vector<FieldSpan> wheel_spans(const WheelState& w) {
    std::vector<FieldSpan> out;
    add_span(out, w, "contact_point", w.contact_point);
    add_span(out, w, "contact_normal", w.contact_normal);
    add_span(out, w, "suspension_length", w.suspension_length);
    add_span(out, w, "spin", w.spin);
    add_span(out, w, "angular_velocity", w.angular_velocity);
    add_span(out, w, "normal_force", w.normal_force);
    add_span(out, w, "slip", w.slip);
    add_span(out, w, "grounded", w.grounded);
    return out;
}

// Every declared member of a VehicleState, with the wheel array expanded so
// each wheel's fields get their own span.
std::vector<FieldSpan> vehicle_spans(const VehicleState& v) {
    std::vector<FieldSpan> out;
    add_span(out, v, "position", v.position);
    add_span(out, v, "orientation", v.orientation);
    add_span(out, v, "velocity", v.velocity);
    add_span(out, v, "angular_velocity", v.angular_velocity);
    add_span(out, v, "steer_angle", v.steer_angle);
    add_span(out, v, "engine_rpm", v.engine_rpm);
    add_span(out, v, "gear", v.gear);
    add_span(out, v, "shift_timer", v.shift_timer);
    add_span(out, v, "recovery_timer", v.recovery_timer);

    for (int i = 0; i < kWheelCount; ++i) {
        const std::size_t w = static_cast<std::size_t>(i);
        const std::size_t base = member_offset(v, v.wheels[w]);
        for (FieldSpan& f : wheel_spans(v.wheels[w])) {
            out.push_back({"wheels[" + std::to_string(i) + "]." + f.name,
                           base + f.offset, f.size});
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Recording and replay
// ---------------------------------------------------------------------------

// A tape, in engine terms and nothing else: a seed, the exact state the run
// began from, and one InputFrame per sim step. That is the entire format. It
// stores no positions along the way, because the trajectory is RECOMPUTED — a
// stored copy of it would be a second source of truth, and the only thing two
// sources of truth ever do is disagree.
struct DriveTape {
    uint64_t seed = 0;
    VehicleState start;
    std::vector<InputFrame> frames;
};

struct Recording {
    DriveTape tape;
    // states[i] is the car AFTER frames[i] was applied.
    std::vector<VehicleState> states;

    double path_metres = 0.0;
    float net_metres = 0.0f;
    float top_speed = 0.0f;
    int airborne_steps = 0;
    int inverted_steps = 0;
    int32_t min_gear = 0;
    int32_t max_gear = 0;
};

Recording record_a_run(uint64_t seed, const TerrainCollider& collider) {
    const VehicleTuning tuning;

    Recording rec;
    rec.tape.seed = seed;
    // spawn_vehicle settles the car on its springs and aligns it to the slope.
    // Assigning a position by hand instead drops the car in with its struts at
    // free length, and the first step launches it — which looks like a physics
    // bug and is not one.
    rec.tape.start = spawn_vehicle(tuning, collider, kSpawnX, kSpawnZ, kSpawnYaw);
    rec.min_gear = rec.tape.start.gear;
    rec.max_gear = rec.tape.start.gear;

    VehicleState car = rec.tape.start;
    const glm::vec3 origin = car.position;

    for (int i = 0; i < kRecordSteps; ++i) {
        const InputFrame in = drive_input(car, seed, i);
        const glm::vec3 was = car.position;
        car = step_vehicle(car, tuning, in, collider, kDt);

        rec.tape.frames.push_back(in);
        rec.states.push_back(car);

        rec.path_metres += static_cast<double>(glm::length(car.position - was));
        const float speed = glm::length(car.velocity);
        if (speed > rec.top_speed) rec.top_speed = speed;
        if (vehicle_airborne(car)) ++rec.airborne_steps;
        if (vehicle_up(car).y < 0.0f) ++rec.inverted_steps;
        if (car.gear < rec.min_gear) rec.min_gear = car.gear;
        if (car.gear > rec.max_gear) rec.max_gear = car.gear;
    }

    rec.net_metres = glm::length(car.position - origin);
    return rec;
}

// Open-loop playback: the tape drives, nothing reads the car back. This is the
// only replay function in the file, so every test below is comparing against
// the same code path a ghost car would run.
std::vector<VehicleState> replay(const DriveTape& tape,
                                 const TerrainCollider& collider) {
    const VehicleTuning tuning;
    std::vector<VehicleState> out;
    out.reserve(tape.frames.size());

    VehicleState car = tape.start;
    for (const InputFrame& in : tape.frames) {
        car = step_vehicle(car, tuning, in, collider, kDt);
        out.push_back(car);
    }
    return out;
}

// Index of the first step at which two runs disagree, or -1 for none. Returning
// the index rather than a bool is what lets a failure say WHEN it went wrong,
// which is most of the debugging.
long first_divergence(const std::vector<VehicleState>& a,
                      const std::vector<VehicleState>& b) {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (!same_vehicle(a[i], b[i])) return static_cast<long>(i);
    }
    return a.size() == b.size() ? -1 : static_cast<long>(n);
}

// ---------------------------------------------------------------------------
//  1. The comparison helper reads every field
// ---------------------------------------------------------------------------

void test_the_comparison_helper_reads_every_field(const Recording& rec) {
    // A state from the MIDDLE of a real run, so every field carries a live
    // value rather than a spawn default. A helper can skip a field that is
    // zero on both sides and nobody notices.
    const VehicleState ref = rec.states[rec.states.size() / 2];

    // If this fails, the reference state holds a NaN — which compares unequal
    // to itself and would make every byte below look "detected".
    REQUIRE_MSG(same_vehicle(ref, ref),
                "the reference state must equal itself before it can prove "
                "anything about the comparison",
                "self");

    const std::vector<FieldSpan> spans = vehicle_spans(ref);

    std::vector<bool> declared(sizeof(VehicleState), false);
    std::vector<std::string> owner(sizeof(VehicleState));
    std::size_t declared_bytes = 0;
    for (const FieldSpan& f : spans) {
        REQUIRE_MSG(f.offset + f.size <= sizeof(VehicleState),
                    "a declared field runs off the end of the struct", "span");
        for (std::size_t b = f.offset; b < f.offset + f.size; ++b) {
            REQUIRE_MSG(!declared[b], "two declared fields overlap", "span");
            declared[b] = true;
            owner[b] = f.name;
            ++declared_bytes;
        }
    }

    // Flip the low bit of every byte in turn and require that the comparison
    // notices exactly the bytes that belong to a declared field.
    //
    // Bit 0 of a byte is never a float's SIGN bit on any layout, which matters:
    // flipping a sign bit turns +0.0f into -0.0f, and those two compare EQUAL,
    // so a sign-bit probe would falsely accuse a live field of being padding.
    std::size_t detected = 0;
    std::vector<std::size_t> missed;
    std::vector<std::size_t> padding;

    for (std::size_t i = 0; i < sizeof(VehicleState); ++i) {
        unsigned char buf[sizeof(VehicleState)];
        std::memcpy(buf, &ref, sizeof buf);
        buf[i] = static_cast<unsigned char>(buf[i] ^ 0x01u);

        VehicleState mutated;
        std::memcpy(&mutated, buf, sizeof mutated);

        const bool noticed = !same_vehicle(ref, mutated);
        if (noticed) {
            ++detected;
            if (!declared[i]) padding.push_back(i);  // noticed a padding byte?!
        } else if (declared[i]) {
            missed.push_back(i);
        }
    }

    if (!missed.empty()) {
        std::fprintf(stderr,
                     "      same_vehicle() ignored %zu byte(s) of declared state:\n",
                     missed.size());
        for (const std::size_t b : missed) {
            std::fprintf(stderr, "        offset %zu, inside %s\n", b,
                         owner[b].c_str());
        }
    }
    REQUIRE_MSG(missed.empty(),
                "same_vehicle() skipped a declared field — a comparison that "
                "skips a field reports green about the field it skipped",
                "coverage");
    REQUIRE_MSG(padding.empty(),
                "the comparison noticed a byte no declared field owns, so the "
                "span table is out of date",
                "coverage");
    REQUIRE(detected == declared_bytes);

    std::printf(
        "      same_vehicle() reads %zu of %zu declared bytes across %zu "
        "fields; %zu alignment padding bytes ignored\n",
        detected, declared_bytes, spans.size(),
        sizeof(VehicleState) - declared_bytes);
    apricot_test::pass("the comparison helper reads every field of VehicleState");
}

// ---------------------------------------------------------------------------
//  2. The tape format is a flat block of bytes
// ---------------------------------------------------------------------------

void test_the_tape_format_is_a_flat_block_of_bytes(const Recording& rec,
                                                   const TerrainCollider& collider) {
    // The layout, pinned. InputFrame is serialised field-by-field into every
    // recorded tape, so these offsets ARE the format: reordering or resizing a
    // field silently invalidates every tape ever recorded, and the failure
    // presents as "the physics changed".
    const InputFrame probe;
    REQUIRE(sizeof(InputFrame) == 32u);
    REQUIRE(member_offset(probe, probe.steer) == 0u);
    REQUIRE(member_offset(probe, probe.throttle) == 4u);
    REQUIRE(member_offset(probe, probe.brake) == 8u);
    REQUIRE(member_offset(probe, probe.handbrake) == 12u);
    REQUIRE(member_offset(probe, probe.look_dx) == 16u);
    REQUIRE(member_offset(probe, probe.look_dy) == 20u);
    REQUIRE(member_offset(probe, probe.held) == 24u);
    REQUIRE(member_offset(probe, probe.pressed) == 28u);

    // No padding: eight four-byte fields fill the struct exactly. This is what
    // makes a tape a memcpy rather than a serialiser.
    REQUIRE_MSG(sizeof(probe.steer) + sizeof(probe.throttle) + sizeof(probe.brake) +
                        sizeof(probe.handbrake) + sizeof(probe.look_dx) +
                        sizeof(probe.look_dy) + sizeof(probe.held) +
                        sizeof(probe.pressed) ==
                    sizeof(InputFrame),
                "InputFrame gained padding", "layout");

    // Button bits are serialised numbers, not names. Renumbering one rewrites
    // history: an old tape's respawn press comes back as a pause.
    REQUIRE(kBtnNone == 0u);
    REQUIRE(kBtnRespawn == 1u);
    REQUIRE(kBtnLookBack == 2u);
    REQUIRE(kBtnCamCycle == 4u);
    REQUIRE(kBtnPause == 8u);
    REQUIRE(kBtnShiftUp == 16u);
    REQUIRE(kBtnShiftDown == 32u);
    REQUIRE(kBtnAccept == 64u);
    REQUIRE(kBtnBack == 128u);
    REQUIRE(kBtnQuit == 256u);

    // And the consequence: a tape written out as raw bytes and read back drives
    // the identical run. If InputFrame ever stops being a flat block, this is
    // where it is caught, rather than in a save file six months old.
    const std::size_t bytes = rec.tape.frames.size() * sizeof(InputFrame);
    std::vector<unsigned char> blob(bytes);
    std::memcpy(blob.data(), rec.tape.frames.data(), bytes);

    DriveTape from_bytes;
    from_bytes.seed = rec.tape.seed;
    from_bytes.start = rec.tape.start;
    from_bytes.frames.resize(rec.tape.frames.size());
    std::memcpy(from_bytes.frames.data(), blob.data(), bytes);

    const std::vector<VehicleState> played = replay(from_bytes, collider);
    const long bad = first_divergence(rec.states, played);
    REQUIRE_MSG(bad < 0,
                "a tape round-tripped through raw bytes replayed differently",
                "bytes");

    std::printf("      %zu frames = %zu bytes round-tripped and replayed identically\n",
                rec.tape.frames.size(), bytes);
    apricot_test::pass("the InputFrame tape format is a flat block of bytes");
}

// ---------------------------------------------------------------------------
//  3. THE HEADLINE: a recorded tape replays bit for bit
// ---------------------------------------------------------------------------

void test_a_recorded_tape_replays_bit_for_bit(const Recording& rec,
                                              const TerrainCollider& collider) {
    // --- anti-vacuity, FIRST ------------------------------------------------
    // Everything below this block is trivially true of a car that never moved.
    // So prove the run went somewhere before proving it went there twice.
    std::printf(
        "      recorded %zu steps: %.1f m of path, %.1f m net, top speed %.1f m/s, "
        "%d/%d steps airborne, %d inverted, gears %d..%d\n",
        rec.tape.frames.size(), rec.path_metres, static_cast<double>(rec.net_metres),
        static_cast<double>(rec.top_speed), rec.airborne_steps, kRecordSteps,
        rec.inverted_steps, rec.min_gear, rec.max_gear);

    REQUIRE(rec.tape.frames.size() == static_cast<std::size_t>(kRecordSteps));
    REQUIRE(rec.states.size() == rec.tape.frames.size());
    REQUIRE_MSG(rec.path_metres > kMinPathMetres,
                "the recorded car actually drove somewhere", "travel");
    REQUIRE_MSG(static_cast<double>(rec.net_metres) > kMinNetMetres,
                "and ended up somewhere else, rather than driving in a circle "
                "back to the spawn",
                "travel");
    REQUIRE_MSG(rec.top_speed > kMinTopSpeed, "at a speed worth reproducing",
                "travel");
    REQUIRE_MSG(rec.inverted_steps == 0,
                "on its wheels the whole way — an inverted car replays "
                "perfectly and proves nothing about a driving game",
                "travel");

    // --- the tape used every input channel ----------------------------------
    // A tape of nothing but throttle reproduces bit-for-bit while brake, steer
    // and the latched edges go completely untested.
    int throttle = 0, brake = 0, handbrake = 0, left = 0, right = 0, shifts = 0;
    for (const InputFrame& f : rec.tape.frames) {
        if (f.throttle > 0.0f) ++throttle;
        if (f.brake > 0.0f) ++brake;
        if (f.handbrake > 0.0f) ++handbrake;
        if (f.steer < -0.05f) ++left;
        if (f.steer > 0.05f) ++right;
        if (was_pressed(f, kBtnShiftUp | kBtnShiftDown)) ++shifts;
    }
    std::printf(
        "      tape uses: throttle %d, brake %d, handbrake %d, steer L%d/R%d, "
        "gear-change edges %d\n",
        throttle, brake, handbrake, left, right, shifts);

    REQUIRE_MSG(throttle > 0, "the tape contains throttle", "variety");
    REQUIRE_MSG(brake > 0, "the tape contains braking", "variety");
    REQUIRE_MSG(handbrake > 0, "the tape contains handbrake", "variety");
    REQUIRE_MSG(left > 0 && right > 0, "the tape steers both ways", "variety");
    REQUIRE_MSG(shifts > 0, "the tape contains latched gear-change edges",
                "variety");
    REQUIRE_MSG(rec.min_gear != rec.max_gear,
                "and the gearbox actually moved, so those edges reached the sim",
                "variety");

    // --- the claim ----------------------------------------------------------
    const std::vector<VehicleState> played = replay(rec.tape, collider);
    REQUIRE(played.size() == rec.states.size());

    // Every step, not just the last one. An end-state comparison passes even
    // when the trajectory wandered off and happened to come back.
    const long bad = first_divergence(rec.states, played);
    if (bad >= 0) {
        std::fprintf(stderr, "      first divergence at step %ld of %zu\n", bad,
                     rec.states.size());
    }
    REQUIRE_MSG(bad < 0, "the replayed state is bit-identical at every step",
                "replay");

    // And the finishing state, field for field, said out loud.
    REQUIRE_MSG(same_vehicle(rec.states.back(), played.back()),
                "the finishing VehicleState matches field for field", "replay");

    apricot_test::pass("a recorded tape replays through step_vehicle bit for bit");
}

// ---------------------------------------------------------------------------
//  4. Two independently constructed colliders from one seed
// ---------------------------------------------------------------------------

void test_two_colliders_from_one_seed_agree(const Recording& rec) {
    const TerrainCollider a(kSeed);

    // Build the second one only AFTER hammering the generators somewhere else
    // entirely. If any of this had a cache, a static or an accumulated stream
    // position in it, the second collider would answer differently from the
    // first and the divergence would show up here rather than as "the world
    // looks different after I drove around for a while".
    for (int32_t z = -3; z <= 3; ++z) {
        for (int32_t x = -3; x <= 3; ++x) {
            (void)build_chunk(kOtherSeed, ChunkCoord{x * 7, z * 5});
        }
    }
    const TerrainCollider b(kSeed);

    const std::vector<VehicleState> on_a = replay(rec.tape, a);
    const std::vector<VehicleState> on_b = replay(rec.tape, b);

    const long bad = first_divergence(on_a, on_b);
    if (bad >= 0) {
        std::fprintf(stderr, "      first divergence at step %ld\n", bad);
    }
    REQUIRE_MSG(bad < 0,
                "two TerrainColliders built from one seed must drive the same "
                "tape to the same bits",
                "collider");
    REQUIRE(same_vehicle(on_a.back(), on_b.back()));

    // The recorded run used a third collider object entirely. All three agree.
    REQUIRE(first_divergence(rec.states, on_a) < 0);

    std::printf("      three separately built colliders, one seed, %zu identical steps\n",
                on_a.size());
    apricot_test::pass("two colliders from one seed reproduce the drive exactly");
}

// ---------------------------------------------------------------------------
//  5. Negative control: a different seed diverges
// ---------------------------------------------------------------------------

void test_a_different_seed_diverges(const Recording& rec) {
    // Without this, every assertion above would still pass if step_vehicle
    // ignored the terrain entirely — which is exactly what a stubbed collider
    // would do, and exactly the kind of green nobody notices.
    const TerrainCollider here(kSeed);
    const TerrainCollider elsewhere(kOtherSeed);

    const std::vector<VehicleState> a = replay(rec.tape, here);
    const std::vector<VehicleState> b = replay(rec.tape, elsewhere);
    REQUIRE(a.size() == b.size());

    const long bad = first_divergence(a, b);
    REQUIRE_MSG(bad >= 0,
                "the same tape on a different seed must NOT reproduce the run",
                "control");

    const float apart = glm::length(a.back().position - b.back().position);
    REQUIRE_MSG(apart > 1.0f,
                "and the two runs must end up somewhere genuinely different, "
                "not a last-bit apart",
                "control");

    std::printf("      diverged at step %ld; finished %.1f m apart\n", bad,
                static_cast<double>(apart));
    apricot_test::pass("negative control: a different seed diverges");
}

// ---------------------------------------------------------------------------
//  6. Negative control: altering one InputFrame changes the replay
// ---------------------------------------------------------------------------

void test_altering_one_input_frame_changes_the_replay(const Recording& rec,
                                                      const TerrainCollider& collider) {
    // The other half of the anti-vacuity argument: the tape is genuinely being
    // READ, rather than the replay re-deriving the drive from the seed and the
    // start state and ignoring the frames entirely.
    //
    // IT PERTURBS THROTTLE, AND THE FIRST DRAFT PERTURBING STEER WAS WRONG.
    // Steering is RATE LIMITED — vehicle.cpp moves steer_angle toward its
    // target by at most steer_rate * dt, which at 120 Hz is 0.033 rad. Two
    // different stick positions that are both further away than that in the
    // same direction produce the IDENTICAL steer_angle, so the altered frame
    // was swallowed whole: measured, zero divergence across all 2400 steps, and
    // the negative control quietly asserted nothing.
    //
    // Throttle has no such filter. It multiplies crank torque straight into
    // axle torque on the same step, so the wheels it drives come out different
    // immediately and unconditionally.
    constexpr std::size_t kTampered = 5;
    REQUIRE(rec.tape.frames.size() > kTampered);
    REQUIRE_MSG(rec.tape.frames[kTampered].throttle > 0.0f,
                "the frame being tampered with has throttle to remove — "
                "without it this control tests nothing",
                "control");

    DriveTape tampered = rec.tape;
    tampered.frames[kTampered].throttle = 0.0f;

    const std::vector<VehicleState> clean = replay(rec.tape, collider);
    const std::vector<VehicleState> dirty = replay(tampered, collider);

    const long bad = first_divergence(clean, dirty);
    REQUIRE_MSG(bad >= 0, "one altered input frame must change the replay",
                "control");
    REQUIRE_MSG(bad <= static_cast<long>(kTampered),
                "and it must change it on the step it was altered, not later",
                "control");

    // Every other frame is untouched, so a divergence here is the one byte.
    std::size_t differing_frames = 0;
    for (std::size_t i = 0; i < rec.tape.frames.size(); ++i) {
        if (std::memcmp(&rec.tape.frames[i], &tampered.frames[i],
                        sizeof(InputFrame)) != 0) {
            ++differing_frames;
        }
    }
    REQUIRE(differing_frames == 1u);

    std::printf("      one frame of %zu altered; replay diverged at step %ld\n",
                rec.tape.frames.size(), bad);
    apricot_test::pass("negative control: altering one input frame alters the replay");
}

// ---------------------------------------------------------------------------
//  7. Terrain generates identically from one seed
// ---------------------------------------------------------------------------

void test_terrain_generates_identically_from_one_seed() {
    // The ground under all of the above. If two generations of one seed differ,
    // the replay claim is false no matter how pure step_vehicle is.
    //
    // Sampled through every door the car and the renderer use: the smooth field
    // (height_at / normal_at), the DRAWN triangle (mesh_height_at /
    // mesh_normal_at), the built chunk the renderer uploads, and the collider a
    // wheel actually asks. Those are four different evaluations of one world;
    // determinism has to hold at all four or the car and the picture disagree.
    //
    // hash_coord() itself, which every one of those bottoms out in, is pinned by
    // GOLDEN VALUES in tests/rng_determinism_tests.cpp, and its order
    // independence in tests/terrain_determinism_tests.cpp. Both suites are
    // engine-only and neither is affected by the rally coming out, so this one
    // deliberately does NOT restate their golden values: a second copy is a
    // second place to regenerate, and the whole point of a golden value is that
    // changing it is a single deliberate act with a known cost attached.
    const TerrainCollider ca(kSeed);
    const TerrainCollider cb(kSeed);
    const TerrainCollider other(kOtherSeed);

    std::size_t samples = 0;
    std::size_t differed_from_other = 0;

    for (int ix = -12; ix <= 12; ++ix) {
        for (int iz = -12; iz <= 12; ++iz) {
            const float x = static_cast<float>(ix) * 23.5f;
            const float z = static_cast<float>(iz) * 19.25f;
            ++samples;

            REQUIRE_MSG(height_at(kSeed, x, z) == height_at(kSeed, x, z),
                        "height_at is not reproducible", "height");
            REQUIRE_MSG(mesh_height_at(kSeed, x, z) == mesh_height_at(kSeed, x, z),
                        "the drawn surface height is not reproducible", "height");

            const glm::vec3 na = normal_at(kSeed, x, z);
            const glm::vec3 nb = normal_at(kSeed, x, z);
            REQUIRE_MSG(na.x == nb.x && na.y == nb.y && na.z == nb.z,
                        "normal_at is not reproducible", "normal");

            const glm::vec3 ma = mesh_normal_at(kSeed, x, z);
            const glm::vec3 mb = mesh_normal_at(kSeed, x, z);
            REQUIRE_MSG(ma.x == mb.x && ma.y == mb.y && ma.z == mb.z,
                        "the drawn face normal is not reproducible", "normal");

            // And through the collider, which is what a wheel asks.
            REQUIRE_MSG(ca.height(x, z) == cb.height(x, z),
                        "two colliders disagree on meshed height", "collider");
            const glm::vec3 cna = ca.normal(x, z);
            const glm::vec3 cnb = cb.normal(x, z);
            REQUIRE_MSG(cna.x == cnb.x && cna.y == cnb.y && cna.z == cnb.z,
                        "two colliders disagree on the contact normal", "collider");
            REQUIRE_MSG(ca.material(x, z) == cb.material(x, z),
                        "two colliders disagree on the surface material", "collider");
            REQUIRE_MSG(ca.grip(x, z) == cb.grip(x, z),
                        "two colliders disagree on grip", "collider");

            if (ca.height(x, z) != other.height(x, z)) ++differed_from_other;
        }
    }

    // The built chunk, which is where MATERIAL lives as data: every vertex
    // carries the four splat weights the terrain shader mixes with. A whole
    // vertex buffer compared with memcmp covers position, normal, uv and those
    // weights at once — and covers any field added to TerrainVertex later
    // without anyone remembering to extend this loop.
    const ChunkCoord chunks[] = {{0, 0}, {4, -6}, {-9, 3}, {17, 21}};
    std::size_t verts = 0;
    for (const ChunkCoord c : chunks) {
        const ChunkMesh a = build_chunk(kSeed, c);
        const ChunkMesh b = build_chunk(kSeed, c);
        REQUIRE_MSG(a.vertices.size() == b.vertices.size(),
                    "vertex count differed between generations", "chunk");
        REQUIRE_MSG(std::memcmp(a.vertices.data(), b.vertices.data(),
                                a.vertices.size() * sizeof(TerrainVertex)) == 0,
                    "vertex bytes differed between generations", "chunk");
        REQUIRE_MSG(a.indices == b.indices, "index buffer differed", "chunk");
        verts += a.vertices.size();

        // Say the material part out loud, so a failure names it rather than
        // reporting "some bytes differed".
        for (std::size_t i = 0; i < a.vertices.size(); ++i) {
            const glm::vec4& wa = a.vertices[i].material_weights;
            const glm::vec4& wb = b.vertices[i].material_weights;
            REQUIRE_MSG(wa.x == wb.x && wa.y == wb.y && wa.z == wb.z && wa.w == wb.w,
                        "material weights differed between generations", "material");
        }
    }

    // Anti-vacuity again: all of the above is true of a flat world. The seed has
    // to be doing something.
    REQUIRE_MSG(differed_from_other * 2u > samples,
                "a different seed must produce a genuinely different world, not "
                "the same one with a different number attached",
                "control");

    std::printf(
        "      %zu ground samples and %zu chunk vertices identical across "
        "generations; %zu/%zu samples differ on a second seed\n",
        samples, verts, differed_from_other, samples);
    apricot_test::pass("terrain height, normal and material reproduce exactly");
}

// The coverage that could not live here until PENG-40, because including
// terrain/scatter.h alongside terrain_collider.h did not compile.
//
// Props are world state a replay has to reproduce: they are solid, the route
// runs between them, and a tree that exists in one generation and not another
// is a car that hits nothing in the replay of a run that ended in a tree. The
// whole prop is compared, not just the position -- kind, variant, ground
// material, yaw and scale all feed what the player sees and hits.
void test_prop_scatter_is_bit_identical() {
    const ChunkCoord chunks[] = {{0, 0}, {4, -6}, {-9, 3}, {17, 21}};

    std::size_t props = 0;
    std::size_t differed_from_other = 0;
    std::size_t other_props = 0;

    for (const ChunkCoord c : chunks) {
        const std::vector<ScatterProp> a = scatter_chunk(kSeed, c);
        const std::vector<ScatterProp> b = scatter_chunk(kSeed, c);
        REQUIRE_MSG(a.size() == b.size(),
                    "one seed produced a different number of props twice",
                    "scatter");
        for (std::size_t i = 0; i < a.size(); ++i) {
            REQUIRE_MSG(a[i].kind == b[i].kind, "prop kind differed", "scatter");
            REQUIRE_MSG(a[i].variant == b[i].variant, "prop variant differed",
                        "scatter");
            REQUIRE_MSG(a[i].ground == b[i].ground,
                        "prop ground material differed", "scatter");
            REQUIRE_MSG(a[i].position.x == b[i].position.x &&
                            a[i].position.y == b[i].position.y &&
                            a[i].position.z == b[i].position.z,
                        "prop position differed", "scatter");
            REQUIRE_MSG(a[i].yaw == b[i].yaw, "prop yaw differed", "scatter");
            REQUIRE_MSG(a[i].scale == b[i].scale, "prop scale differed", "scatter");

            // The ground a prop stands on is the same ground a wheel is told
            // about. This is the seam PENG-40 closed, asserted end to end: the
            // scatter placer and the collider are now reading one classifier.
            const TerrainCollider collider(kSeed);
            REQUIRE_MSG(a[i].ground == collider.material(a[i].position.x,
                                                        a[i].position.z),
                        "a prop's ground material disagrees with the collider's",
                        "one classifier");
        }
        props += a.size();

        const std::vector<ScatterProp> o = scatter_chunk(kOtherSeed, c);
        other_props += o.size();
        if (o.size() != a.size()) {
            ++differed_from_other;
        } else {
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (a[i].position != o[i].position) { ++differed_from_other; break; }
            }
        }
    }

    // Anti-vacuity, twice over: four empty chunks would satisfy every equality
    // above, and a scatter that ignored the seed would satisfy the control.
    REQUIRE_MSG(props > 0, "no props were generated at all, so nothing was compared",
                "test would be vacuous");
    REQUIRE_MSG(other_props > 0, "the control seed generated no props", "control");
    REQUIRE_MSG(differed_from_other > 0,
                "a different seed scattered props identically", "control");

    std::printf("      %zu props over %zu chunks bit-identical across "
                "generations; %zu/%zu chunks differ on a second seed\n",
                props, sizeof(chunks) / sizeof(chunks[0]), differed_from_other,
                sizeof(chunks) / sizeof(chunks[0]));
    apricot_test::pass("prop scatter reproduces exactly and follows the seed");
}

// ---------------------------------------------------------------------------
//  8. FixedStep: a zero-step frame must not drop a latched edge
// ---------------------------------------------------------------------------

void test_a_zero_step_frame_does_not_drop_a_gear_change(const TerrainCollider& collider) {
    // core/fixed_step.h and core/input_frame.h both state this rule, and
    // fixed_step_tests.cpp pins it on the accounting. What is pinned HERE is
    // the consequence: a press sampled on a frame that owes no sim step must
    // still reach step_vehicle. A 240 Hz display against a 120 Hz sim owes a
    // step every other frame, so roughly half of all frames are that frame —
    // and the bug it produces is invisible on any machine whose display runs at
    // or below the sim rate, which is the machine it gets written on.
    const VehicleTuning tuning;
    const VehicleState spawn =
        spawn_vehicle(tuning, collider, kSpawnX, kSpawnZ, kSpawnYaw);

    constexpr double kFrame = 1.0 / 240.0;

    // --- the rule, obeyed ---------------------------------------------------
    FixedStep clock;
    InputFrame in;
    VehicleState car = spawn;

    // Frame 0: the platform layer latches the press. This frame owes nothing.
    in.pressed |= kBtnShiftDown;
    FixedStep::Tick tick = clock.advance(kFrame);
    REQUIRE_MSG(tick.steps == 0, "the first half-frame owes no sim step", "latch");
    // consume_edges() is NOT called, because tick.steps == 0. That guard is the
    // whole design.

    // Frame 1: a step is owed, and it sees the press from the previous frame.
    tick = clock.advance(kFrame);
    REQUIRE(tick.steps == 1);
    for (int i = 0; i < tick.steps; ++i) {
        car = step_vehicle(car, tuning, in, collider, kDt);
    }
    if (tick.steps > 0) clear_edges(in);

    REQUIRE_MSG(car.gear == spawn.gear - 1,
                "a gear change latched on a zero-step frame still reaches the sim",
                "latch");
    REQUIRE(in.pressed == 0u);

    // --- the rule, broken, so the test shows what it is defending ------------
    FixedStep wrong_clock;
    InputFrame wrong_in;
    VehicleState wrong_car = spawn;

    wrong_in.pressed |= kBtnShiftDown;
    tick = wrong_clock.advance(kFrame);
    REQUIRE(tick.steps == 0);
    clear_edges(wrong_in);  // <-- cleared per FRAME instead of per STEP

    tick = wrong_clock.advance(kFrame);
    REQUIRE(tick.steps == 1);
    for (int i = 0; i < tick.steps; ++i) {
        wrong_car = step_vehicle(wrong_car, tuning, wrong_in, collider, kDt);
    }

    REQUIRE_MSG(wrong_car.gear == spawn.gear,
                "clearing edges on the render cadence drops the press outright "
                "— this is the bug, reproduced on purpose",
                "latch");

    std::printf("      gear %d with the guard, %d without it\n", car.gear,
                wrong_car.gear);
    apricot_test::pass("a latched edge survives a frame that owes no sim step");
}

// ---------------------------------------------------------------------------
//  9. FixedStep: the frame rate must not reach the sim
// ---------------------------------------------------------------------------

void test_a_jittering_frame_rate_replays_identically(const Recording& rec,
                                                     const TerrainCollider& collider) {
    // The tape is indexed by SIM STEP, and FixedStep is the only thing that
    // decides how many steps a render frame owes. Feed it a deliberately awful
    // frame rate and the sim must land on the identical bits — otherwise a
    // replay recorded on a 144 Hz machine desyncs on a 60 Hz one, and the bug
    // report reads as "the physics is different on my PC".
    const VehicleTuning tuning;

    VehicleState car = rec.tape.start;
    std::vector<VehicleState> states;
    states.reserve(rec.tape.frames.size());

    FixedStep clock;
    std::size_t consumed = 0;
    int frames = 0, zero_step_frames = 0, multi_step_frames = 0, clamped_frames = 0;

    while (consumed < rec.tape.frames.size()) {
        // Deterministic jitter from 2 ms to 22 ms — from well above the sim rate
        // down to well below it, with the occasional hitch.
        Rng rng = rng_at(kSeed, static_cast<int32_t>(frames), 0, 91u);
        const double frame_dt = static_cast<double>(rng.range(0.002f, 0.022f));
        const FixedStep::Tick tick = clock.advance(frame_dt);
        ++frames;

        if (tick.steps == 0) ++zero_step_frames;
        if (tick.steps > 1) ++multi_step_frames;
        if (tick.clamped) ++clamped_frames;

        for (int i = 0; i < tick.steps && consumed < rec.tape.frames.size(); ++i) {
            car = step_vehicle(car, tuning, rec.tape.frames[consumed], collider, kDt);
            states.push_back(car);
            ++consumed;
        }
    }

    // Anti-vacuity for this test specifically: if every frame owed exactly one
    // step, it proved nothing about the accounting it is here to exercise.
    REQUIRE_MSG(zero_step_frames > 0, "some frames owed no step at all", "jitter");
    REQUIRE_MSG(multi_step_frames > 0, "and some owed several", "jitter");
    REQUIRE(clamped_frames == 0);  // 22 ms is nowhere near kMaxStepsPerFrame

    REQUIRE(states.size() == rec.states.size());
    const long bad = first_divergence(rec.states, states);
    if (bad >= 0) {
        std::fprintf(stderr, "      first divergence at step %ld\n", bad);
    }
    REQUIRE_MSG(bad < 0,
                "the sim must land on the same bits regardless of how the frames "
                "that drove it were carved up",
                "jitter");

    std::printf(
        "      %zu steps over %d jittering frames (%d owed none, %d owed several); "
        "identical\n",
        consumed, frames, zero_step_frames, multi_step_frames);
    apricot_test::pass("a jittering frame rate does not reach the sim");
}

}  // namespace

int main() {
    // One recording, shared. It is the same 2400 steps every test wants, and
    // re-driving it per test buys nothing but seconds.
    const TerrainCollider collider(kSeed);
    const Recording rec = record_a_run(kSeed, collider);

    test_the_comparison_helper_reads_every_field(rec);
    test_the_tape_format_is_a_flat_block_of_bytes(rec, collider);
    test_a_recorded_tape_replays_bit_for_bit(rec, collider);
    test_two_colliders_from_one_seed_agree(rec);
    test_a_different_seed_diverges(rec);
    test_altering_one_input_frame_changes_the_replay(rec, collider);
    test_terrain_generates_identically_from_one_seed();
    test_prop_scatter_is_bit_identical();
    test_a_zero_step_frame_does_not_drop_a_gear_change(collider);
    test_a_jittering_frame_rate_replays_identically(rec, collider);
    return apricot_test::done("sim_determinism_tests");
}
