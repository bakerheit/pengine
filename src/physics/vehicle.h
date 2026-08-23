#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/input_frame.h"
#include "physics/terrain_collider.h"

namespace apricot {

inline constexpr int kWheelCount = 4;

// Wheel order is fixed and load-bearing — replay tapes and tuning files index
// by it. Front-left, front-right, rear-left, rear-right.
enum WheelIndex : int {
    kWheelFrontLeft = 0,
    kWheelFrontRight = 1,
    kWheelRearLeft = 2,
    kWheelRearRight = 3,
};

// Gear numbering. Reverse is -1 and neutral is 0 so that the ratio's SIGN is
// the direction of travel and the HUD can print the number it is given. The
// automatic shifter works only in 1..kForwardGearCount; getting into neutral or
// reverse is a deliberate act by the driver, because a gearbox that selects
// reverse on its own is a gearbox nobody trusts.
inline constexpr int32_t kGearReverse = -1;
inline constexpr int32_t kGearNeutral = 0;
inline constexpr int kForwardGearCount = 6;

struct WheelState {
    glm::vec3 contact_point{0.0f};
    glm::vec3 contact_normal{0.0f, 1.0f, 0.0f};

    // Current compressed length of the suspension, in metres: the distance
    // from the mount down to the wheel centre.
    float suspension_length = 0.0f;

    // Wheel rotation about its axle, in radians. Visual only — accumulate it
    // here rather than in the renderer so a replay reproduces the wheel
    // rotation exactly instead of deriving it from frame time.
    float spin = 0.0f;

    // Rotation RATE about the axle, rad/s. Not visual: this is the state the
    // tyre model integrates, so wheelspin, lock-up and engine RPM all come out
    // of it. Positive means the wheel is rolling the car forwards.
    float angular_velocity = 0.0f;

    // Load through the contact patch, newtons. The friction budget is
    // grip * normal_force, so this is where weight transfer becomes grip.
    // Exposed because "did the front actually load up under braking" is a
    // question a test and a debug overlay both need to ask.
    float normal_force = 0.0f;

    // Slip magnitude normalised so that 1.0 is the peak of the tyre curve.
    // Below 1 the tyre is gripping harder the more it slips; above 1 it is
    // giving up. That crossover is the whole feel of the car.
    float slip = 0.0f;

    bool grounded = false;
};

// Everything the car IS at an instant. Plain data on purpose: this is what a
// replay diff compares and what a checkpoint restore overwrites, so it must be
// copyable and complete. No pointers, no back-references, no cached handles.
struct VehicleState {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angular_velocity{0.0f};

    // Smoothed steering actually applied, in radians. Distinct from the raw
    // InputFrame::steer: a rally car's wheels do not snap to full lock, and
    // interpolating in the renderer instead would make the physics disagree
    // with what the player sees.
    float steer_angle = 0.0f;

    float engine_rpm = 0.0f;
    int32_t gear = 1;

    // Seconds since the shifter last moved. Blocks the automatic from
    // immediately undoing a manual shift, and stops a gear hunting on a
    // threshold. Counts DOWN to zero.
    float shift_timer = 0.0f;

    // Seconds the chassis has been continuously upside down. The righting
    // nudge only starts once this passes VehicleTuning::recovery_delay, so a
    // car that is merely airborne and cartwheeling is left alone to land.
    float recovery_timer = 0.0f;

    std::array<WheelState, kWheelCount> wheels{};
};

// Handling constants. Separated from VehicleState because tuning is shared and
// immutable during a run, while state is per-car and mutated every step.
//
// EVERY number that shapes the feel of the car is in here, and the maths in
// vehicle.cpp contains no magic constants that a driver would want to change.
// That is the point of the struct: dialling the car in should never mean
// editing the force loop.
struct VehicleTuning {
    // --- mass and body ------------------------------------------------------
    float mass_kg = 1250.0f;
    float gravity = 9.81f;

    // Chassis half-extents used to place the four suspension rays, in metres.
    float half_wheelbase = 1.35f;  // front/back of centre
    float half_track = 0.78f;      // left/right of centre

    // Collision box of the bodywork, in body-local metres. The centre of mass
    // is the body origin, so the floor is negative and the roof positive.
    // Used for the inertia tensor and for the last-resort ground guard, NOT
    // for the suspension.
    float chassis_half_width = 0.86f;
    float chassis_half_length = 2.05f;
    float chassis_floor = -0.34f;
    float chassis_roof = 0.85f;

    // Multipliers on the uniform-box inertia. A uniform box makes a car far
    // too eager to roll, because real mass sits low and inboard. Raise
    // roll_inertia_scale to make the car lazier side-to-side;
    // pitch_inertia_scale does the same for dive and squat.
    float pitch_inertia_scale = 1.0f;
    float roll_inertia_scale = 1.4f;
    float yaw_inertia_scale = 1.0f;

    // Height of the centre of mass above the wheel mounts. Small on purpose —
    // engine, fuel and floor pan are all low. Raise it and the car rolls and
    // pitches more for the same forces; raise it far and it tips in corners.
    float com_height_above_mount = 0.06f;

    // --- suspension ---------------------------------------------------------
    // suspension_rest is the FREE length of the strut (mount to wheel centre,
    // unloaded). The car settles below it by mass*g / (4*spring_k), so the
    // resting ride height is rest - that, plus wheel_radius. Travel is
    // symmetric about rest: the strut may compress to rest - travel and hang
    // to rest + travel, and past either end the bumpstop takes over.
    float suspension_rest = 0.24f;
    float suspension_travel = 0.16f;
    float spring_k = 51000.0f;    // N/m per corner
    float damper_c = 6400.0f;     // N.s/m per corner (~0.55 critical)
    float bumpstop_k = 320000.0f; // N/m, the stiff bit past full travel

    // Sanity caps on the strut. Without them a spawn inside a hillside or a
    // landing off a cliff produces a single frame of impossible force and the
    // car leaves the map.
    float max_suspension_force = 90000.0f;  // N of push
    float max_rebound_force = 26000.0f;     // N of pull at full droop
    float max_compression_rate = 8.0f;      // m/s fed to the damper

    // Below this much chassis-up the suspension is switched off entirely and
    // the ground guard takes over. A strut probing downwards from a car on its
    // roof finds ground at absurd angles and shoves it sideways across the
    // world; better to admit the car is not on its wheels.
    float min_upright_dot = 0.25f;

    float wheel_radius = 0.32f;
    // Rotational inertia of one wheel and tyre, kg.m^2. Lower it and the wheels
    // spin up more easily; raise it and the car feels heavy off the line.
    float wheel_inertia = 1.6f;

    // Rotational inertia of the crank, flywheel and clutch, kg.m^2.
    //
    // Small, and it does not stay small. A driven wheel drags the engine round
    // with it through the gearing, so what actually resists that wheel is
    // wheel_inertia + engine_inertia * (total ratio)^2, shared across the
    // driven wheels — in first gear that is an order of magnitude more than the
    // wheel alone. Leave it out and a 400 N.m car simply cannot put its power
    // down: every launch is an instant, permanent burnout that pins the revs on
    // the limiter and upshifts the gearbox to top at walking pace.
    float engine_inertia = 0.22f;

    // --- steering -----------------------------------------------------------
    float max_steer = 0.58f;   // radians at full lock (~33 degrees)
    float steer_rate = 4.0f;   // radians/second toward the target
    // Lock is scaled by 1 / (1 + speed * this). At 0 the car has full lock at
    // 200 km/h, which is undriveable; raise it for a calmer car on straights.
    float steer_speed_falloff = 0.035f;

    // --- engine and gearbox -------------------------------------------------
    float engine_peak_torque = 420.0f;  // N.m
    float engine_peak_rpm = 4200.0f;
    float engine_idle_rpm = 900.0f;
    float engine_redline_rpm = 7200.0f;
    // Shape of the torque curve: torque = peak * (1 - falloff * (rpm/peak - 1)^2),
    // floored at engine_min_torque_frac. Bigger = peakier, more highly strung.
    float engine_torque_falloff = 0.55f;
    float engine_min_torque_frac = 0.25f;
    // Retarding torque at the crank on a closed throttle, scaled by revs.
    // This is what holds the car back on a downhill in gear.
    float engine_brake_torque = 55.0f;

    float final_drive = 4.10f;
    float drivetrain_efficiency = 0.92f;
    // Index 0 is first gear. Reverse has its own ratio and its own sign.
    std::array<float, static_cast<std::size_t>(kForwardGearCount)> gear_ratios{
        3.15f, 2.10f, 1.55f, 1.20f, 0.97f, 0.82f};
    float reverse_ratio = -3.20f;
    // How fast revs chase the throttle with no gear engaged, 1/s. Only ever
    // used in neutral, where there are no wheels to read a speed from.
    float engine_free_rev_rate = 6.0f;

    bool auto_gearbox = true;
    float shift_up_rpm = 6600.0f;
    float shift_down_rpm = 2600.0f;
    // The automatic refuses to upshift while the driven wheels are slipping
    // past this much of the tyre peak. Spinning wheels drive the revs, not road
    // speed, and a shifter that believes them ladders itself into top gear at
    // walking pace and then bogs.
    float shift_up_max_slip = 1.5f;
    // Seconds the shifter is locked out after any shift. Also what stops the
    // automatic from fighting a manual override on the very next step.
    float shift_cooldown = 0.35f;

    // 0 = rear wheel drive, 1 = front wheel drive, 0.5 = even four wheel drive.
    // A rally car wants a rear bias so it rotates on the throttle.
    float front_drive_bias = 0.40f;

    // How hard the differentials tie the driven wheels together, 1/s. 0 is a
    // fully open diff; large is welded solid; a rally car runs close to locked.
    //
    // Without any coupling every driven wheel is its own flywheel, and one that
    // gets past the grip peak has nothing to pull it back — it stays lit
    // forever, pins the revs, and drags the automatic up through the gears at
    // walking pace. The coupling is what lets three wheels with grip haul the
    // fourth back down.
    float differential_coupling = 40.0f;

    // --- brakes -------------------------------------------------------------
    float brake_torque = 5200.0f;      // N.m total across all four wheels
    float brake_bias_front = 0.62f;    // fraction of that going to the front
    float handbrake_torque = 3000.0f;  // N.m total, rear wheels only
    // What the handbrake does to REAR lateral grip at full pull. This, not the
    // torque, is what makes the back end come round.
    float handbrake_grip_scale = 0.42f;

    // --- tyres --------------------------------------------------------------
    // The slip curve. `slip` is the speed difference between the contact patch
    // and the ground, in m/s, normalised by
    //     tyre_peak_slip + tyre_peak_slip_ratio * |forward speed|
    // so that the peak sits at a roughly constant slip ANGLE rather than a
    // constant speed — otherwise the car is permanently past the peak at
    // motorway speed and permanently under it in a car park.
    float tyre_peak_slip = 1.10f;        // m/s of slip at a standstill
    float tyre_peak_slip_ratio = 0.12f;  // extra slip allowed per m/s of speed
    // Grip retained a long way past the peak, as a fraction of peak grip.
    // THIS NUMBER IS THE DIFFERENCE between a car that snaps and a car you can
    // catch. Near 1.0 the tail never really lets go; near 0.3 a slide is
    // unrecoverable. Around 0.55 the back steps out and comes back.
    float tyre_tail_grip = 0.55f;
    // How fast grip decays past the peak. Bigger = a sharper edge.
    float tyre_falloff = 1.0f;

    // Slip speed below which the tyre is treated as STUCK rather than sliding,
    // in m/s. Inside this window grip ramps up to full peak instead of
    // following the curve down to zero.
    //
    // Without it the model is silently wrong at rest: the curve says a tyre
    // with no slip makes no force, so a parked car creeps down any slope
    // forever at whatever tiny slip balances gravity, and "the car never quite
    // stops" gets blamed on the brakes. Real static friction is at least as
    // strong as sliding friction, which is exactly what this restores.
    float tyre_static_slip = 0.30f;

    // Where tyre forces enter the chassis, as a fraction of the way from the
    // wheel mount (0) down to the contact patch (1).
    //
    // This is the roll-centre dial and it is the strongest single knob on the
    // whole car. At 0 the forces act at mount height, so there is almost no
    // lever into the centre of mass: no dive, no squat, no roll, and no weight
    // transfer worth the name. At 1 the full arm applies and the car rolls and
    // tips like the free body diagram says it should. Real suspensions land in
    // between because they feed load into the body through their links.
    float tyre_force_height = 0.90f;

    // --- resistance ---------------------------------------------------------
    float drag = 0.42f;                // quadratic, N per (m/s)^2
    float rolling_resistance = 0.018f; // dimensionless, multiplied by wheel load
    float angular_drag = 1.2f;         // 1/s, exponential decay on spin

    // --- rollover recovery --------------------------------------------------
    // Above this much chassis-up the car counts as the right way up and the
    // recovery timer resets.
    float recovery_up_dot = 0.30f;
    // Seconds upside down before the nudge starts.
    float recovery_delay = 1.5f;
    // How close the chassis has to be to the ground for the timer to run at
    // all, in metres. A car in mid-air is not stuck, it is falling, and helping
    // it round mid-somersault would both look wrong and quietly cancel a third
    // of gravity on the way down.
    float recovery_ground_reach = 2.5f;
    // Righting torque per kilogram of car, N.m/kg. This is a NUDGE: it rolls
    // the car back over across a second or two. It is never a teleport, and
    // raising it until it is one defeats the point.
    float recovery_torque = 9.0f;
    // Damping on the recovery, N.m per (rad/s) per kg, so the car settles the
    // right way up instead of rocking past and back.
    float recovery_damping = 3.0f;
    // A little lift while righting, as a fraction of gravity, so a car pinned
    // flat on its roof has something to pivot on.
    float recovery_lift = 0.35f;

    // --- props --------------------------------------------------------------
    // Radius of the cylinder used to shove the chassis out of static prop
    // boxes, in metres. HORIZONTAL only: standing on top of a prop is the
    // suspension's job, and resolving vertically here would fight it.
    // Deliberately a cylinder and not the real box — a rally car brushing a
    // rock wants a forgiving kerb, not a corner to snag on.
    float chassis_collision_radius = 1.15f;

    // --- safety net ---------------------------------------------------------
    // Minimum gap kept between the chassis box and the ground by the last
    // resort guard, in metres.
    float ground_skin = 0.02f;
    // Fastest the guard may lift the car out of the ground, m/s.
    //
    // A cap, because a guard with no limit is not a safety net, it is a
    // catapult: one chassis corner clipping something tall hoists the whole car
    // metres in a single step and puts it somewhere it could never have driven.
    // Generous enough to cover any real landing (60 m/s of correction is far
    // more than a car can penetrate in one step) and mean enough that a
    // pathological case surfaces over several steps instead of teleporting.
    float ground_correction_rate = 60.0f;
    // Hard ceiling on speed, m/s. Nothing should ever reach it; if the car
    // does, something upstream has already gone wrong and this stops the
    // position from becoming unrepresentable.
    float max_speed = 200.0f;
};

// --- derived geometry --------------------------------------------------------
// Free functions rather than members so VehicleTuning stays an aggregate.

// Strut length the car settles at under its own weight, in metres.
float static_suspension_length(const VehicleTuning& tuning);

// Height of the centre of mass above the ground when the car is at rest on a
// level surface. Spawn at this and the springs are already in balance, so the
// car does not drop, and does not launch.
float static_ride_height(const VehicleTuning& tuning);

// --- queries -----------------------------------------------------------------

inline glm::vec3 vehicle_forward(const VehicleState& s) {
    return s.orientation * glm::vec3{0.0f, 0.0f, -1.0f};
}
inline glm::vec3 vehicle_right(const VehicleState& s) {
    return s.orientation * glm::vec3{1.0f, 0.0f, 0.0f};
}
inline glm::vec3 vehicle_up(const VehicleState& s) {
    return s.orientation * glm::vec3{0.0f, 1.0f, 0.0f};
}
inline float vehicle_speed(const VehicleState& s) {
    return glm::length(s.velocity);
}
// Speed along the direction the car is pointing. Negative when reversing, and
// the number a speedometer should show rather than |velocity|, which reads
// positive while the car is sliding backwards down a hill.
inline float vehicle_forward_speed(const VehicleState& s) {
    return glm::dot(s.velocity, vehicle_forward(s));
}
inline bool vehicle_airborne(const VehicleState& s) {
    for (const WheelState& w : s.wheels) {
        if (w.grounded) return false;
    }
    return true;
}

// Place a car on the ground at a world XZ, facing `yaw` radians about +Y,
// settled on its springs and aligned to the slope it is standing on.
//
// Use this rather than assigning a position by hand. A car dropped in with its
// springs at free length launches on the first step, and one dropped in below
// the surface is fired out of the hillside; both look like a physics bug and
// neither is one.
VehicleState spawn_vehicle(const VehicleTuning& tuning,
                           const TerrainCollider& collider, float x, float z,
                           float yaw);

// Advance the car by exactly one sim step.
//
// PURE FUNCTION OF ITS ARGUMENTS. Returns the next state rather than mutating,
// and reads NO wall clock, no global, no random source, and nothing about
// frame rate. `dt` is always kSimDt in practice, but it is a parameter so the
// function stays testable at other rates and so nobody is tempted to reach for
// a timer inside.
//
// That purity is the whole feature: replaying a tape of InputFrames through
// this function must reproduce a run exactly, and a ghost car is just the same
// function fed a different tape. The first `std::chrono` call added below this
// line silently ends that, and the symptom is "replays desync after a minute".
//
// The model, in the order it runs:
//   1. steering, rate limited and speed sensitive
//   2. gearbox, then engine torque from the resulting RPM
//   3. four vertical suspension probes -> spring, damper, and a normal force
//   4. per wheel tyre impulses: longitudinal against the wheel's own spin,
//      lateral against sideways slip, both inside one friction circle whose
//      radius is the slip curve times the surface grip times the normal force
//   5. integrate, then a last-resort guard that refuses to let the chassis end
//      a step below the ground
//
// Weight transfer is not a term anywhere in that list. It falls out of step 4
// applying its forces below the centre of mass, which pitches and rolls the
// body, which changes the strut compressions in step 3 of the NEXT step, which
// changes the normal forces. That is why it behaves correctly on a crest and
// mid-corner and not only in the two cases somebody thought to special-case.
VehicleState step_vehicle(const VehicleState& state, const VehicleTuning& tuning,
                          const InputFrame& input,
                          const TerrainCollider& collider, float dt);

}  // namespace apricot
