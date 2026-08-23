#include "physics/vehicle.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace apricot {
namespace {

constexpr float kRadPerSecToRpm = 9.549296585513720f;  // 60 / (2*pi)
constexpr float kTwoPi = 6.283185307179586f;

// Smallest cosine between chassis-up and the ground normal that the strut
// geometry is allowed to divide by. Past this the car is on its side and the
// suspension has been switched off anyway; the clamp only exists so the frame
// where it crosses over cannot produce an infinity.
constexpr float kMinAxisCos = 0.20f;

constexpr float kEpsilon = 1e-6f;

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

// Normalise, or fall back. Every normalise in a physics step is a division by a
// length that some legitimate input can drive to zero; a NaN that escapes here
// is in the position, the replay and the save file within a second.
glm::vec3 normalise_or(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    if (!(len > kEpsilon)) return fallback;
    return v / len;
}

bool is_finite(float v) { return std::isfinite(v); }
bool is_finite(const glm::vec3& v) {
    return is_finite(v.x) && is_finite(v.y) && is_finite(v.z);
}

// --- rigid body --------------------------------------------------------------
// A local scratch body, not a stored type. VehicleState is the state; this is
// the working set for one step, so there is nothing to keep in sync and nothing
// to forget to reset.
struct Body {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angular_velocity{0.0f};

    glm::mat3 basis{1.0f};
    glm::mat3 inv_inertia_world{1.0f};
    float mass = 1.0f;
    float inv_mass = 1.0f;

    glm::vec3 force{0.0f};
    glm::vec3 torque{0.0f};

    glm::vec3 to_world(const glm::vec3& local) const {
        return position + basis * local;
    }
    glm::vec3 point_velocity(const glm::vec3& world_pt) const {
        return velocity + glm::cross(angular_velocity, world_pt - position);
    }
    void add_force_at(const glm::vec3& f, const glm::vec3& world_pt) {
        force += f;
        torque += glm::cross(world_pt - position, f);
    }
    void add_impulse_at(const glm::vec3& j, const glm::vec3& world_pt) {
        velocity += j * inv_mass;
        angular_velocity += inv_inertia_world * glm::cross(world_pt - position, j);
    }
};

// --- geometry ----------------------------------------------------------------

bool wheel_is_front(int i) {
    return i == kWheelFrontLeft || i == kWheelFrontRight;
}
bool wheel_is_left(int i) {
    return i == kWheelFrontLeft || i == kWheelRearLeft;
}

glm::vec3 wheel_mount_local(const VehicleTuning& t, int i) {
    return glm::vec3{wheel_is_left(i) ? -t.half_track : t.half_track,
                     -t.com_height_above_mount,
                     wheel_is_front(i) ? -t.half_wheelbase : t.half_wheelbase};
}

// Body-local corners of the chassis box, in a fixed order so the ground guard
// visits them identically every step on every machine.
void chassis_corners(const VehicleTuning& t, glm::vec3 (&out)[8]) {
    const float xs[2] = {-t.chassis_half_width, t.chassis_half_width};
    const float ys[2] = {t.chassis_floor, t.chassis_roof};
    const float zs[2] = {-t.chassis_half_length, t.chassis_half_length};
    int n = 0;
    for (int xi = 0; xi < 2; ++xi) {
        for (int yi = 0; yi < 2; ++yi) {
            for (int zi = 0; zi < 2; ++zi) {
                out[n++] = glm::vec3{xs[xi], ys[yi], zs[zi]};
            }
        }
    }
}

glm::mat3 inverse_inertia_local(const VehicleTuning& t, float mass) {
    const float ex = std::max(2.0f * t.chassis_half_width, 0.05f);
    const float ey = std::max(t.chassis_roof - t.chassis_floor, 0.05f);
    const float ez = std::max(2.0f * t.chassis_half_length, 0.05f);

    // Uniform box, then the per-axis scales. X is pitch, Y is yaw, Z is roll —
    // forward is -Z, so the axis a car rolls about is its length.
    const float k = mass / 12.0f;
    const float ix = k * (ey * ey + ez * ez) * std::max(t.pitch_inertia_scale, 0.01f);
    const float iy = k * (ex * ex + ez * ez) * std::max(t.yaw_inertia_scale, 0.01f);
    const float iz = k * (ex * ex + ey * ey) * std::max(t.roll_inertia_scale, 0.01f);

    glm::mat3 m{0.0f};
    m[0][0] = 1.0f / ix;
    m[1][1] = 1.0f / iy;
    m[2][2] = 1.0f / iz;
    return m;
}

// --- drivetrain --------------------------------------------------------------

float gear_ratio(const VehicleTuning& t, int32_t gear) {
    if (gear == kGearReverse) return t.reverse_ratio;
    if (gear <= kGearNeutral) return 0.0f;
    const int32_t top = static_cast<int32_t>(kForwardGearCount);
    const std::size_t idx = static_cast<std::size_t>(std::min(gear, top) - 1);
    return t.gear_ratios[idx];
}

bool front_is_driven(const VehicleTuning& t) { return t.front_drive_bias > 0.001f; }
bool rear_is_driven(const VehicleTuning& t) { return t.front_drive_bias < 0.999f; }
bool wheel_is_driven(const VehicleTuning& t, int i) {
    return wheel_is_front(i) ? front_is_driven(t) : rear_is_driven(t);
}

// Mean rotation rate of the driven wheels. Signed, so reverse reads negative
// and the engine still sees positive revs once the negative gear ratio is
// applied.
float driven_wheel_rate(const VehicleTuning& t, const VehicleState& s) {
    float sum = 0.0f;
    int n = 0;
    for (int i = 0; i < kWheelCount; ++i) {
        if (!wheel_is_driven(t, i)) continue;
        sum += s.wheels[static_cast<std::size_t>(i)].angular_velocity;
        ++n;
    }
    if (n == 0) return 0.0f;
    return sum / static_cast<float>(n);
}

// Engine speed implied by the wheels through the current gear. In neutral there
// are no wheels to read, so the revs chase the throttle instead.
float engine_rpm_for(const VehicleTuning& t, const VehicleState& s,
                     float throttle, float previous_rpm, float dt) {
    const float ratio = gear_ratio(t, s.gear) * t.final_drive;
    if (std::fabs(ratio) < kEpsilon) {
        const float target =
            t.engine_idle_rpm + throttle * (t.engine_redline_rpm - t.engine_idle_rpm);
        const float blend = clampf(t.engine_free_rev_rate * dt, 0.0f, 1.0f);
        return previous_rpm + (target - previous_rpm) * blend;
    }
    const float rpm = std::fabs(driven_wheel_rate(t, s) * ratio) * kRadPerSecToRpm;
    // Floored at idle because the engine does not stop turning when the car
    // does, and the audio module pitches straight off this number.
    return clampf(rpm, t.engine_idle_rpm, t.engine_redline_rpm * 1.05f);
}

float engine_torque_at(const VehicleTuning& t, float rpm) {
    // Rev limiter. Cutting torque rather than clamping revs is what makes the
    // limiter feel like a limiter instead of a wall.
    if (rpm >= t.engine_redline_rpm) return 0.0f;
    const float peak = std::max(t.engine_peak_rpm, 1.0f);
    const float u = rpm / peak - 1.0f;
    const float shape = 1.0f - t.engine_torque_falloff * u * u;
    return t.engine_peak_torque *
           clampf(shape, t.engine_min_torque_frac, 1.0f);
}

// --- tyre --------------------------------------------------------------------

// Fraction of peak grip available at a normalised slip of `u`.
//
// Below 1 the tyre bites harder the more it slips, peaking at exactly 1. Past
// that it gives up toward tyre_tail_grip, and THAT is the whole handling model:
// a rear tyre pushed past the peak makes less force, which lets it slip more,
// which makes less force again. The car steps out. It is catchable because the
// tail is a floor and not a cliff — back off, slip falls, grip returns.
float slip_response(const VehicleTuning& t, float u) {
    if (!(u > 0.0f)) return 1.0f;  // no slip at all: static, see below
    float kinetic;
    if (u <= 1.0f) {
        kinetic = u * (2.0f - u);
    } else {
        const float over = u - 1.0f;
        kinetic = t.tyre_tail_grip +
                  (1.0f - t.tyre_tail_grip) / (1.0f + t.tyre_falloff * over * over);
    }
    return kinetic;
}

// --- validity ----------------------------------------------------------------

bool state_is_finite(const VehicleState& s) {
    if (!is_finite(s.position) || !is_finite(s.velocity) ||
        !is_finite(s.angular_velocity)) {
        return false;
    }
    if (!is_finite(s.orientation.w) || !is_finite(s.orientation.x) ||
        !is_finite(s.orientation.y) || !is_finite(s.orientation.z)) {
        return false;
    }
    if (!is_finite(s.engine_rpm)) return false;
    for (const WheelState& w : s.wheels) {
        if (!is_finite(w.angular_velocity) || !is_finite(w.suspension_length) ||
            !is_finite(w.normal_force) || !is_finite(w.contact_point)) {
            return false;
        }
    }
    return true;
}

// Per-wheel working set for one step. Filled by the suspension pass and read by
// the tyre pass, so the two agree about where the ground was by construction
// rather than by both asking again.
struct WheelContact {
    bool grounded = false;
    glm::vec3 mount_world{0.0f};
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float suspension_length = 0.0f;
    float normal_force = 0.0f;
    float grip = 1.0f;
    float rolling_scale = 1.0f;
};

}  // namespace

// --- derived geometry --------------------------------------------------------

float static_suspension_length(const VehicleTuning& tuning) {
    const float k = std::max(tuning.spring_k, 1.0f);
    const float sag = tuning.mass_kg * tuning.gravity / (4.0f * k);
    // Cannot sag past the bumpstop, however soft the spring is set.
    return std::max(tuning.suspension_rest - sag,
                    tuning.suspension_rest - tuning.suspension_travel);
}

float static_ride_height(const VehicleTuning& tuning) {
    return static_suspension_length(tuning) + tuning.wheel_radius +
           tuning.com_height_above_mount;
}

// --- spawn -------------------------------------------------------------------

VehicleState spawn_vehicle(const VehicleTuning& tuning,
                           const TerrainCollider& collider, float x, float z,
                           float yaw) {
    VehicleState s;

    // Sample under all four wheels, not just under the centre.
    //
    // The drawn surface is FACETED -- piecewise-flat triangles -- so the four
    // wheels routinely sit on different planes, and the centre sample belongs
    // to none of them. Spawning from it drops the car in a pose no suspension
    // is at rest in, and it settles with a visible twitch. Averaging the four
    // contacts puts it where it is actually going to end up.
    const glm::quat heading = glm::angleAxis(yaw, glm::vec3{0.0f, 1.0f, 0.0f});

    float ground = 0.0f;
    glm::vec3 n{0.0f};
    for (int i = 0; i < kWheelCount; ++i) {
        const glm::vec3 mount = heading * wheel_mount_local(tuning, i);
        const float wx = x + mount.x;
        const float wz = z + mount.z;
        ground += collider.height(wx, wz);
        n += collider.normal(wx, wz);
    }
    ground /= static_cast<float>(kWheelCount);

    // Four face normals can only cancel on geometry a height field cannot make,
    // but a degenerate sample must not produce a NaN body axis.
    const float n_len = glm::length(n);
    n = (n_len > kEpsilon) ? n / n_len : glm::vec3{0.0f, 1.0f, 0.0f};

    s.position = glm::vec3{x, ground + static_ride_height(tuning), z};

    // Yaw first, then tilt the whole thing onto the slope. Doing it the other
    // way round yaws about the SLOPE normal, so a car spawned facing north on
    // a hillside points somewhere else.
    const glm::vec3 axis = glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, n);
    const float sin_tilt = glm::length(axis);
    if (sin_tilt > kEpsilon) {
        const float angle = std::atan2(sin_tilt, n.y);
        s.orientation = glm::angleAxis(angle, axis / sin_tilt) * heading;
    } else {
        s.orientation = heading;
    }

    // Springs pre-loaded to the load they will carry. A car spawned at free
    // length launches itself on step one, which everybody reads as a physics
    // bug and nobody reads as a spawn bug.
    const float susp = static_suspension_length(tuning);
    const float corner_load = tuning.mass_kg * tuning.gravity * 0.25f;
    for (int i = 0; i < kWheelCount; ++i) {
        WheelState& w = s.wheels[static_cast<std::size_t>(i)];
        const glm::vec3 mount = s.position + glm::mat3_cast(s.orientation) *
                                                 wheel_mount_local(tuning, i);
        w.suspension_length = susp;
        w.normal_force = corner_load;
        w.grounded = true;
        w.contact_point =
            glm::vec3{mount.x, collider.height(mount.x, mount.z), mount.z};
        w.contact_normal = collider.normal(mount.x, mount.z);
    }

    s.gear = 1;
    s.engine_rpm = tuning.engine_idle_rpm;
    return s;
}

// --- the step ----------------------------------------------------------------

VehicleState step_vehicle(const VehicleState& state, const VehicleTuning& tuning,
                          const InputFrame& input,
                          const TerrainCollider& collider, float dt) {
    VehicleState next = state;
    // A non-positive or non-finite dt is not a small step, it is a broken
    // caller. Returning the state unchanged keeps a replay in lockstep with
    // whatever produced it instead of integrating garbage.
    if (!(dt > 0.0f) || !std::isfinite(dt)) return next;

    const float throttle = clampf(input.throttle, 0.0f, 1.0f);
    const float brake = clampf(input.brake, 0.0f, 1.0f);
    const float handbrake = clampf(input.handbrake, 0.0f, 1.0f);
    const float steer_in = clampf(input.steer, -1.0f, 1.0f);

    // --- body ---------------------------------------------------------------
    Body body;
    body.mass = std::max(tuning.mass_kg, 1.0f);
    body.inv_mass = 1.0f / body.mass;
    body.position = state.position;
    body.velocity = state.velocity;
    body.angular_velocity = state.angular_velocity;

    body.orientation = state.orientation;
    const float quat_len2 = glm::dot(body.orientation, body.orientation);
    body.orientation = (quat_len2 > kEpsilon)
                           ? body.orientation * (1.0f / std::sqrt(quat_len2))
                           : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};

    body.basis = glm::mat3_cast(body.orientation);
    const glm::mat3 inv_i_local = inverse_inertia_local(tuning, body.mass);
    body.inv_inertia_world = body.basis * inv_i_local * glm::transpose(body.basis);

    const glm::vec3 forward = body.basis * glm::vec3{0.0f, 0.0f, -1.0f};
    const glm::vec3 right = body.basis * glm::vec3{1.0f, 0.0f, 0.0f};
    const glm::vec3 up = body.basis * glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::vec3 world_up{0.0f, 1.0f, 0.0f};

    // --- steering -----------------------------------------------------------
    // Lock shrinks with speed. Full lock at 200 km/h is not a rally car, it is
    // a spin, and no amount of tyre tuning rescues it.
    const float speed = glm::length(body.velocity);
    const float lock =
        tuning.max_steer / (1.0f + speed * std::max(tuning.steer_speed_falloff, 0.0f));
    const float steer_target = steer_in * lock;
    const float steer_delta = tuning.steer_rate * dt;
    next.steer_angle =
        state.steer_angle +
        clampf(steer_target - state.steer_angle, -steer_delta, steer_delta);

    // --- gearbox ------------------------------------------------------------
    // Shifts run off LAST step's revs, which is the honest ordering: a shifter
    // reacts to what the engine did, it does not predict.
    next.shift_timer = std::max(0.0f, state.shift_timer - dt);

    const int32_t top_gear = static_cast<int32_t>(kForwardGearCount);
    // Latched edges. The frame loop clears these only after the LAST step of a
    // frame, so a multi-step frame sees the same press several times over — the
    // cooldown below is what stops one tap becoming three gears.
    if (was_pressed(input, kBtnShiftUp) && next.shift_timer <= 0.0f) {
        next.gear = std::min(next.gear + 1, top_gear);
        next.shift_timer = tuning.shift_cooldown;
    } else if (was_pressed(input, kBtnShiftDown) && next.shift_timer <= 0.0f) {
        next.gear = std::max(next.gear - 1, kGearReverse);
        next.shift_timer = tuning.shift_cooldown;
    } else if (tuning.auto_gearbox && next.shift_timer <= 0.0f &&
               next.gear >= 1) {
        float driven_slip = 0.0f;
        for (int i = 0; i < kWheelCount; ++i) {
            if (!wheel_is_driven(tuning, i)) continue;
            driven_slip =
                std::max(driven_slip, state.wheels[static_cast<std::size_t>(i)].slip);
        }
        const bool traction = driven_slip <= tuning.shift_up_max_slip;
        if (traction && state.engine_rpm > tuning.shift_up_rpm &&
            next.gear < top_gear) {
            ++next.gear;
            next.shift_timer = tuning.shift_cooldown;
        } else if (state.engine_rpm < tuning.shift_down_rpm && next.gear > 1) {
            --next.gear;
            next.shift_timer = tuning.shift_cooldown;
        }
    }

    // --- engine -------------------------------------------------------------
    const float rpm =
        engine_rpm_for(tuning, next, throttle, state.engine_rpm, dt);
    const float ratio = gear_ratio(tuning, next.gear) * tuning.final_drive;
    const float crank_torque = engine_torque_at(tuning, rpm);

    // Axle torque, split front/rear by the drive bias and then between the two
    // wheels of each axle. There is no torque cut across a shift: the gearbox
    // hands over instantly, so an upshift is a step change in wheel torque
    // rather than the brief lift a real one gives you.
    const float axle_torque =
        crank_torque * throttle * ratio * tuning.drivetrain_efficiency;
    const float front_share =
        front_is_driven(tuning) ? tuning.front_drive_bias * 0.5f : 0.0f;
    const float rear_share =
        rear_is_driven(tuning) ? (1.0f - tuning.front_drive_bias) * 0.5f : 0.0f;

    // Engine braking, referred to the axle. Kept alive at low revs rather than
    // fading to nothing, because the whole point of it is holding the car back
    // on a descent, which happens at low revs.
    const float rev_frac = clampf(rpm / std::max(tuning.engine_peak_rpm, 1.0f),
                                  0.0f, 2.0f);
    const float engine_brake_axle =
        tuning.engine_brake_torque * (0.35f + 0.65f * rev_frac) *
        std::fabs(ratio) * tuning.drivetrain_efficiency * (1.0f - throttle);

    // --- suspension ---------------------------------------------------------
    // The probe is VERTICAL, not along chassis-down. The world is a height
    // field, so a vertical probe is exact — one triangle evaluation — where a
    // tilted one has to be marched and can miss. The tilt is then paid for
    // geometrically: the vertical gap is converted to a distance along the
    // strut, which is exact for a flat ground plane and correct to first order
    // for anything else.
    const bool upright = glm::dot(up, world_up) > tuning.min_upright_dot;
    const float ray_length =
        tuning.suspension_rest + tuning.suspension_travel + tuning.wheel_radius;

    WheelContact contacts[kWheelCount];

    for (int i = 0; i < kWheelCount; ++i) {
        WheelContact& c = contacts[static_cast<std::size_t>(i)];
        c.mount_world = body.to_world(wheel_mount_local(tuning, i));
        c.suspension_length = tuning.suspension_rest + tuning.suspension_travel;

        if (!upright) continue;

        const TerrainCollider::GroundHit hit =
            collider.probe_down(c.mount_world, ray_length);
        if (!hit.hit) continue;

        const float axis_cos = std::max(glm::dot(up, hit.normal), kMinAxisCos);
        const float along = hit.distance * std::max(hit.normal.y, 0.0f) / axis_cos;
        const float susp_len = along - tuning.wheel_radius;

        // Out of reach below: the wheel is hanging in the air.
        if (susp_len > tuning.suspension_rest + tuning.suspension_travel) continue;

        // Out of reach ABOVE: the mount itself is under the ground, so this is
        // not a compressed spring, it is a car in the wrong place. Treating the
        // overlap as travel is catastrophic — probe_down honestly reports a
        // wheel fifteen metres inside a hill as fifteen metres of compression,
        // which through the bumpstop is five meganewtons and fires the car a
        // hundred metres into the sky. That is not hypothetical: a
        // default-constructed VehicleState sits at the world origin, and on any
        // seed whose terrain is above sea level there, this is the very first
        // step. Let the ground guard walk the car out instead.
        if (susp_len < -tuning.suspension_travel) continue;

        c.grounded = true;
        c.point = hit.point;
        c.normal = hit.normal;
        c.grip = hit.grip * tuning.grip_scale;
        c.rolling_scale = surface_rolling_scale(hit.material);
        c.suspension_length =
            clampf(susp_len, 0.0f, tuning.suspension_rest + tuning.suspension_travel);

        // Signed about the free length: positive compresses, negative droops
        // and pulls the chassis back down. A compression-only spring leaves the
        // body floating above its wheels after every landing.
        const float compression = tuning.suspension_rest - susp_len;
        const float main = clampf(compression, -tuning.suspension_travel,
                                  tuning.suspension_travel);
        const float past_bumpstop =
            std::max(0.0f, compression - tuning.suspension_travel);
        const float spring =
            main * tuning.spring_k + past_bumpstop * tuning.bumpstop_k;

        // Damper input is the BODY's velocity at the mount along chassis-down,
        // not a finite difference of the probe distance. The difference form
        // spikes to tens of m/s crossing a sharp crest even though the body
        // barely moved, and the car launches off a bump it should have soaked.
        const float compress_rate =
            clampf(glm::dot(body.point_velocity(c.mount_world), -up),
                   -tuning.max_compression_rate, tuning.max_compression_rate);

        const float strut = clampf(spring + compress_rate * tuning.damper_c,
                                   -tuning.max_rebound_force,
                                   tuning.max_suspension_force);

        body.add_force_at(up * strut, c.mount_world);
        // A drooping strut is pulling, not pushing, so the tyre carries no load
        // and its friction budget collapses with it.
        c.normal_force = std::max(0.0f, strut);
    }

    // --- gravity and aero ---------------------------------------------------
    body.force += glm::vec3{0.0f, -tuning.gravity * body.mass, 0.0f};
    if (speed > kEpsilon) {
        body.force -= body.velocity * (tuning.drag * speed);
    }

    // --- rollover recovery --------------------------------------------------
    // A nudge, deliberately. It builds a righting torque only after the car has
    // been genuinely upside down for a while, so an airborne car mid-somersault
    // is left to land on its own, and it never moves the car directly — a
    // teleport is instant, unearned, and reads as the game giving up.
    const float up_dot = glm::dot(up, world_up);
    // Upside down AND actually resting on something. The height check is not
    // belt and braces: a car cartwheeling off a jump can hold up_dot below the
    // threshold for well over the delay, and helping it round in mid-air both
    // looks wrong and cancels a third of gravity on the way down.
    const bool inverted =
        up_dot < tuning.recovery_up_dot &&
        collider.probe_down(body.position, tuning.recovery_ground_reach).hit;
    next.recovery_timer = inverted ? state.recovery_timer + dt : 0.0f;

    if (next.recovery_timer > tuning.recovery_delay) {
        const glm::vec3 cross_up = glm::cross(up, world_up);
        const float sin_tilt = glm::length(cross_up);
        // Exactly inverted has no unique righting axis, so pick the car's own
        // length. Without this the one pose that most needs recovering is the
        // one pose that never recovers.
        const glm::vec3 axis =
            (sin_tilt > kEpsilon) ? cross_up / sin_tilt : forward;
        const float tilt = std::atan2(sin_tilt, up_dot);

        const float strength = tuning.recovery_torque * body.mass;
        const float damping =
            glm::dot(body.angular_velocity, axis) * tuning.recovery_damping * body.mass;
        body.torque += axis * (strength * clampf(tilt, 0.0f, 3.2f) - damping);
        // Something to pivot on when pinned flat on the roof.
        body.force += world_up * (tuning.recovery_lift * tuning.gravity * body.mass);
    }

    // --- integrate the accumulated forces -----------------------------------
    body.velocity += body.force * body.inv_mass * dt;
    body.angular_velocity += body.inv_inertia_world * body.torque * dt;

    // --- tyres --------------------------------------------------------------
    const float corner_mass = body.mass * 0.25f;

    // Engine inertia referred to a driven wheel: geared up by the square of the
    // ratio, and shared out between whichever wheels are actually driven.
    int driven_count = 0;
    for (int i = 0; i < kWheelCount; ++i) {
        if (wheel_is_driven(tuning, i)) ++driven_count;
    }
    const float reflected_inertia =
        (driven_count > 0)
            ? tuning.engine_inertia * ratio * ratio / static_cast<float>(driven_count)
            : 0.0f;

    for (int i = 0; i < kWheelCount; ++i) {
        const std::size_t wi = static_cast<std::size_t>(i);
        const WheelContact& c = contacts[wi];
        WheelState& w = next.wheels[wi];

        // What this wheel actually has to spin up. A driven wheel is dragging
        // the engine round with it; an undriven one is not.
        const float wheel_inertia =
            std::max(tuning.wheel_inertia, 0.01f) +
            (wheel_is_driven(tuning, i) ? reflected_inertia : 0.0f);

        // Reduced mass of the coupled wheel-and-corner system. An impulse along
        // the contact patch has to change BOTH the car's speed and the wheel's
        // spin, so the impulse that removes their relative slip is smaller than
        // either alone would suggest — and using the corner mass here instead
        // makes the model stiff enough to explode at 120 Hz.
        const float reduced_mass =
            1.0f / (1.0f / corner_mass +
                    tuning.wheel_radius * tuning.wheel_radius / wheel_inertia);

        float omega = state.wheels[wi].angular_velocity;

        // --- torques into the wheel ---------------------------------------
        const float share = wheel_is_front(i) ? front_share : rear_share;
        omega += axle_torque * share / wheel_inertia * dt;

        // Everything that resists rotation is applied as a torque that cannot
        // push omega past zero within the step. Letting it overshoot is how a
        // braked wheel ends up spinning backwards under the car.
        float resist = engine_brake_axle * (wheel_is_driven(tuning, i) ? share : 0.0f);
        resist += tuning.brake_torque * brake *
                  (wheel_is_front(i) ? tuning.brake_bias_front * 0.5f
                                     : (1.0f - tuning.brake_bias_front) * 0.5f);
        if (!wheel_is_front(i)) {
            resist += tuning.handbrake_torque * handbrake * 0.5f;
        }
        resist += tuning.rolling_resistance * c.rolling_scale * c.normal_force *
                  tuning.wheel_radius;

        // Torque this resistance would need just to bring the wheel to a stop
        // within the step. Whatever is left over is spare capacity: torque the
        // brake still has in hand to HOLD the wheel against the ground pushing
        // back on it. That number decides, below, whether this wheel behaves
        // like part of the car or like a flywheel bolted to it.
        const float torque_to_stop = std::fabs(omega) * wheel_inertia / dt;
        const float resist_spare = std::max(0.0f, resist - torque_to_stop);

        const float d_omega = std::fabs(resist) / wheel_inertia * dt;
        if (omega > 0.0f) {
            omega = std::max(0.0f, omega - d_omega);
        } else if (omega < 0.0f) {
            omega = std::min(0.0f, omega + d_omega);
        }

        if (!c.grounded) {
            w.grounded = false;
            w.contact_point = c.mount_world;
            w.contact_normal = world_up;
            w.suspension_length = c.suspension_length;
            w.normal_force = 0.0f;
            w.slip = 0.0f;
            w.angular_velocity = omega;
            continue;
        }

        // --- contact axes -------------------------------------------------
        glm::vec3 wheel_forward = forward;
        if (wheel_is_front(i)) {
            wheel_forward = forward * std::cos(next.steer_angle) +
                            right * std::sin(next.steer_angle);
        }
        const glm::vec3 fwd_g = normalise_or(
            wheel_forward - c.normal * glm::dot(wheel_forward, c.normal), forward);
        const glm::vec3 lat_g = normalise_or(glm::cross(c.normal, fwd_g), right);

        const glm::vec3 v_contact = body.point_velocity(c.point);
        const float v_long = glm::dot(v_contact, fwd_g);
        const float v_lat = glm::dot(v_contact, lat_g);

        // Slip is the contact patch's velocity RELATIVE TO THE GROUND: how fast
        // the rubber is being dragged across it.
        const float slip_long = v_long - omega * tuning.wheel_radius;
        const float slip_mag = std::sqrt(slip_long * slip_long + v_lat * v_lat);

        // Normalised against a reference that grows with speed, so the peak
        // sits at a roughly constant slip ANGLE. A fixed slip speed would put
        // the car permanently past the peak on a straight and permanently
        // under it in a car park.
        const float slip_ref =
            std::max(tuning.tyre_peak_slip +
                         tuning.tyre_peak_slip_ratio * std::fabs(v_long),
                     kEpsilon);
        const float u = slip_mag / slip_ref;

        // Static friction floor: see VehicleTuning::tyre_static_slip.
        const float static_ramp =
            clampf(1.0f - slip_mag / std::max(tuning.tyre_static_slip, kEpsilon),
                   0.0f, 1.0f);
        float mu = c.grip * std::max(slip_response(tuning, u), static_ramp);

        // The handbrake's job is not the extra torque, it is this: the rear
        // tyres stop being able to hold a line.
        if (!wheel_is_front(i)) {
            mu *= 1.0f - handbrake * (1.0f - tuning.handbrake_grip_scale);
        }

        // --- how much of the car is behind this contact patch --------------
        // A free wheel absorbs most of a longitudinal impulse by spinning, so
        // the impulse that cancels the slip is small — that is `reduced_mass`,
        // and it is what makes wheelspin and lock-up work. But a wheel the
        // brake can HOLD cannot spin, so the same impulse has to move the car
        // instead, and the effective mass is the whole corner.
        //
        // Without this distinction the longitudinal tyre is a pure damper: it
        // bleeds off a quarter of the slip per step and nothing more, so it
        // cannot resist a SUSTAINED load. The symptom is a car parked on a
        // slope with the brake buried creeping downhill forever at a tenth of a
        // metre per second, which reads as "the brakes don't work" and is
        // actually "the brakes were never asked to hold anything".
        const float free_impulse = std::fabs(slip_long) * reduced_mass;
        const float reaction_torque = free_impulse * tuning.wheel_radius / dt;
        const float held = (reaction_torque > kEpsilon)
                               ? clampf(resist_spare / reaction_torque, 0.0f, 1.0f)
                               : 1.0f;
        const float longitudinal_mass =
            reduced_mass + (corner_mass - reduced_mass) * held;

        // --- the friction circle ------------------------------------------
        // Impulses that exactly cancel the slip, then clipped to what the
        // surface can actually supply. Cancelling velocity can never overshoot,
        // which is why this stays stable where a force proportional to slip
        // would ring.
        const float want_long = -slip_long * longitudinal_mass;
        const float want_lat = -v_lat * corner_mass;
        const float want_mag =
            std::sqrt(want_long * want_long + want_lat * want_lat);
        const float budget = mu * c.normal_force * dt;

        float j_long = want_long;
        float j_lat = want_lat;
        if (want_mag > budget && want_mag > kEpsilon) {
            const float scale = budget / want_mag;
            j_long *= scale;
            j_lat *= scale;
        }

        // Where the tyre load enters the chassis. See tyre_force_height: this
        // single lever is what turns braking into dive and cornering into roll,
        // and next step those become different spring loads. That is the whole
        // of weight transfer, and there is no term for it anywhere else.
        const glm::vec3 apply_at =
            c.mount_world + (c.point - c.mount_world) *
                                clampf(tuning.tyre_force_height, 0.0f, 1.0f);
        body.add_impulse_at(fwd_g * j_long + lat_g * j_lat, apply_at);

        // Equal and opposite at the rim: a forward impulse at the bottom of a
        // wheel slows its spin.
        omega -= j_long * tuning.wheel_radius / wheel_inertia;

        w.grounded = true;
        w.contact_point = c.point;
        w.contact_normal = c.normal;
        w.suspension_length = c.suspension_length;
        w.normal_force = c.normal_force;
        w.slip = u;
        w.angular_velocity = omega;
    }

    // --- differential -------------------------------------------------------
    // Drag the driven wheels toward their common speed. Blending toward the
    // MEAN conserves angular momentum exactly (the driven wheels all carry the
    // same inertia), so this is a clutch pack bleeding off the difference, not
    // a free source of spin.
    if (driven_count > 1 && tuning.differential_coupling > 0.0f) {
        float sum = 0.0f;
        for (int i = 0; i < kWheelCount; ++i) {
            if (!wheel_is_driven(tuning, i)) continue;
            sum += next.wheels[static_cast<std::size_t>(i)].angular_velocity;
        }
        const float mean = sum / static_cast<float>(driven_count);
        const float blend = clampf(tuning.differential_coupling * dt, 0.0f, 1.0f);
        for (int i = 0; i < kWheelCount; ++i) {
            if (!wheel_is_driven(tuning, i)) continue;
            float& w = next.wheels[static_cast<std::size_t>(i)].angular_velocity;
            w += (mean - w) * blend;
        }
    }

    // --- integrate the pose -------------------------------------------------
    const float new_speed = glm::length(body.velocity);
    if (new_speed > tuning.max_speed && new_speed > kEpsilon) {
        // Nothing should ever reach this. If it does, something upstream is
        // already wrong and this only stops the position from leaving the
        // representable world before anyone notices.
        body.velocity *= tuning.max_speed / new_speed;
    }

    body.position += body.velocity * dt;

    const glm::quat spin{0.0f, body.angular_velocity.x, body.angular_velocity.y,
                         body.angular_velocity.z};
    glm::quat oriented = body.orientation + (spin * body.orientation) * (0.5f * dt);
    const float oriented_len2 = glm::dot(oriented, oriented);
    body.orientation = (oriented_len2 > kEpsilon)
                           ? oriented * (1.0f / std::sqrt(oriented_len2))
                           : body.orientation;

    body.angular_velocity *= std::exp(-tuning.angular_drag * dt);

    // --- props: horizontal push-out -----------------------------------------
    // Vertical is left alone on purpose. Driving ONTO a prop is the
    // suspension's business; resolving it here as well would fight the springs
    // and buzz the car on every kerb.
    {
        const float r = std::max(tuning.chassis_collision_radius, 0.0f);
        // The chassis' own vertical extent, not just its centre of mass. A wall
        // taller than the car has to block it, and a kerb shorter than the
        // floor pan has to be driven over — testing the CoM point alone gets
        // the first of those wrong, and a wall the car sails through is not a
        // wall.
        const float chassis_low = body.position.y + tuning.chassis_floor;
        const float chassis_high = body.position.y + tuning.chassis_roof;

        for (const StaticBox& b : collider.static_boxes()) {
            if (b.bounds.max.y <= chassis_low || b.bounds.min.y >= chassis_high) {
                continue;
            }
            const float nx = clampf(body.position.x, b.bounds.min.x, b.bounds.max.x);
            const float nz = clampf(body.position.z, b.bounds.min.z, b.bounds.max.z);
            float dx = body.position.x - nx;
            float dz = body.position.z - nz;
            const float d2 = dx * dx + dz * dz;
            if (d2 >= r * r) continue;

            float push;
            if (d2 > kEpsilon) {
                const float d = std::sqrt(d2);
                dx /= d;
                dz /= d;
                push = r - d;
            } else {
                // Centre is inside the footprint. Leave by the nearest face,
                // which is the only exit that does not shove the car through
                // the whole prop.
                const float to_min_x = body.position.x - b.bounds.min.x;
                const float to_max_x = b.bounds.max.x - body.position.x;
                const float to_min_z = body.position.z - b.bounds.min.z;
                const float to_max_z = b.bounds.max.z - body.position.z;
                const float best =
                    std::min(std::min(to_min_x, to_max_x), std::min(to_min_z, to_max_z));
                dx = 0.0f;
                dz = 0.0f;
                if (best == to_min_x) dx = -1.0f;
                else if (best == to_max_x) dx = 1.0f;
                else if (best == to_min_z) dz = -1.0f;
                else dz = 1.0f;
                push = best + r;
            }

            body.position.x += dx * push;
            body.position.z += dz * push;
            const float closing = body.velocity.x * dx + body.velocity.z * dz;
            if (closing < 0.0f) {
                body.velocity.x -= dx * closing;
                body.velocity.z -= dz * closing;
            }
        }
    }

    // --- ground guard -------------------------------------------------------
    // LAST RESORT, and it should almost never fire. The suspension is what
    // holds the car up; this only refuses to let a step END with the bodywork
    // below the TERRAIN — a landing that outruns the strut travel, a spawn
    // inside a hill, a car on its roof with the suspension switched off.
    // Translation only, so the roll angle survives and the car does not get
    // silently straightened out.
    //
    // Terrain only, deliberately: prop boxes are NOT consulted here. When they
    // were, the guard lifted the car the instant any chassis corner overhung
    // one — so a car still two metres short of a wall was hoisted onto its top
    // in a single step and then drove along it. Standing on a prop is the
    // suspension's job and bumping into one is the push-out's; this has no
    // business being a third opinion about the same geometry.
    {
        glm::vec3 corners[8];
        chassis_corners(tuning, corners);
        const glm::mat3 pose = glm::mat3_cast(body.orientation);

        float deepest = 0.0f;
        for (const glm::vec3& local : corners) {
            const glm::vec3 p = body.position + pose * local;
            const float ground = collider.height(p.x, p.z);
            deepest = std::max(deepest, ground + tuning.ground_skin - p.y);
        }
        if (deepest > 0.0f) {
            const float lift =
                std::min(deepest, std::max(tuning.ground_correction_rate, 0.0f) * dt);
            body.position.y += lift;
            if (body.velocity.y < 0.0f) body.velocity.y = 0.0f;
        }
    }

    // --- write back ---------------------------------------------------------
    next.position = body.position;
    next.orientation = body.orientation;
    next.velocity = body.velocity;
    next.angular_velocity = body.angular_velocity;
    next.engine_rpm = engine_rpm_for(tuning, next, throttle, rpm, dt);

    for (int i = 0; i < kWheelCount; ++i) {
        WheelState& w = next.wheels[static_cast<std::size_t>(i)];
        // Wrapped so a long session cannot grow the angle until float precision
        // makes the wheels visibly stutter.
        float spin_angle =
            std::fmod(state.wheels[static_cast<std::size_t>(i)].spin +
                          w.angular_velocity * dt,
                      kTwoPi);
        if (spin_angle < 0.0f) spin_angle += kTwoPi;
        w.spin = spin_angle;
    }

    if (!state_is_finite(next)) {
        // CONTAINMENT, not a fix. Reaching here means something above produced
        // a NaN and the honest thing is to say so loudly in a review, not to
        // let it into the position, the replay and the save file. Freezing the
        // car keeps the sim deterministic while it is investigated.
        VehicleState safe = state;
        safe.velocity = glm::vec3{0.0f};
        safe.angular_velocity = glm::vec3{0.0f};
        return safe;
    }
    return next;
}

}  // namespace apricot
