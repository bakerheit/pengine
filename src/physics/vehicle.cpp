#include "physics/vehicle.h"
#include "physics/breakaway_contact.h"

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

bool front_is_driven(float front_drive_bias) { return front_drive_bias > 0.001f; }
bool rear_is_driven(float front_drive_bias) { return front_drive_bias < 0.999f; }
bool wheel_is_driven(float front_drive_bias, int i) {
    return wheel_is_front(i) ? front_is_driven(front_drive_bias)
                             : rear_is_driven(front_drive_bias);
}
bool wheel_is_driven(const VehicleTuning& t, int i) {
    return wheel_is_driven(t.front_drive_bias, i);
}

// Mean rotation rate of the driven wheels. Signed, so reverse reads negative
// and the engine still sees positive revs once the negative gear ratio is
// applied.
float driven_wheel_rate(float front_drive_bias, const VehicleState& s) {
    float sum = 0.0f;
    int n = 0;
    for (int i = 0; i < kWheelCount; ++i) {
        if (!wheel_is_driven(front_drive_bias, i)) continue;
        sum += s.wheels[static_cast<std::size_t>(i)].angular_velocity;
        ++n;
    }
    if (n == 0) return 0.0f;
    return sum / static_cast<float>(n);
}

// Engine speed implied by the wheels through the current gear. In neutral there
// are no wheels to read, so the revs chase the throttle instead.
float engine_rpm_for(const VehicleTuning& t, const VehicleState& s,
                     float front_drive_bias, float throttle, float previous_rpm,
                     float dt) {
    const float ratio = gear_ratio(t, s.gear) * t.final_drive;
    if (std::fabs(ratio) < kEpsilon) {
        const float target =
            t.engine_idle_rpm + throttle * (t.engine_redline_rpm - t.engine_idle_rpm);
        const float blend = clampf(t.engine_free_rev_rate * dt, 0.0f, 1.0f);
        return previous_rpm + (target - previous_rpm) * blend;
    }
    const float rpm =
        std::fabs(driven_wheel_rate(front_drive_bias, s) * ratio) * kRadPerSecToRpm;
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
// Up to 1 the tyre has its full friction budget. The impulse it WANTS is already
// proportional to slip, so scaling the budget down as well creates a bogus
// low-slip grip hole: the tyre falls from static grip to roughly 20% grip just
// as the car starts moving sideways, which feels exactly like ice. Past 1 it
// gives up toward tyre_tail_grip; that post-limit falloff is what lets a
// deliberate handbrake pull rotate the car.
float slip_response(const VehicleTuning& t, float u) {
    if (!(u > 0.0f)) return 1.0f;  // no slip at all: static, see below
    if (u <= 1.0f) return 1.0f;

    const float over = u - 1.0f;
    return t.tyre_tail_grip +
           (1.0f - t.tyre_tail_grip) /
               (1.0f + t.tyre_falloff * over * over);
}

// --- validity ----------------------------------------------------------------

bool state_is_finite(const VehicleState& s) {
    if (!is_finite(s.position) || !is_finite(s.velocity) ||
        !is_finite(s.angular_velocity) || !is_finite(s.breakaway_velocity)) {
        return false;
    }
    if (!is_finite(s.orientation.w) || !is_finite(s.orientation.x) ||
        !is_finite(s.orientation.y) || !is_finite(s.orientation.z)) {
        return false;
    }
    if (!is_finite(s.engine_rpm) || !is_finite(s.health) ||
        !is_finite(s.pitch_recovery_timer) ||
        !is_finite(s.last_impact_speed) || !is_finite(s.last_impact_damage) ||
        !is_finite(s.car_contact_speed) ||
        !is_finite(s.mechanical.oil_remaining) ||
        !is_finite(s.mechanical.fuel_remaining) ||
        !is_finite(s.mechanical.oil_lifetime_s) ||
        !is_finite(s.mechanical.fuel_lifetime_s)) {
        return false;
    }
    for (float damage : s.body_damage.zones) {
        if (!is_finite(damage)) return false;
    }
    for (const VehicleDentStamp& stamp : s.body_damage.stamps) {
        if (!is_finite(stamp.contact_xz.x) ||
            !is_finite(stamp.contact_xz.y) || !is_finite(stamp.severity) ||
            !is_finite(stamp.motion_angle) || !is_finite(stamp.radius) ||
            !is_finite(stamp.height) || !is_finite(stamp.glancing)) {
            return false;
        }
    }
    for (const WheelState& w : s.wheels) {
        if (!is_finite(w.angular_velocity) || !is_finite(w.suspension_length) ||
            !is_finite(w.normal_force) || !is_finite(w.contact_point) ||
            surface_index(w.contact_material) >= kSurfaceCount) {
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
    Surface material = Surface::Rock;
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

void repair_vehicle(VehicleState& state) {
    state.health = 100.0f;
    state.last_impact_speed = 0.0f;
    state.last_impact_damage = 0.0f;
    state.car_contact_speed = 0.0f;
    state.breakaway_id = UINT32_MAX;
    state.breakaway_velocity = glm::vec3{0.0f};
    state.body_damage = {};
    state.mechanical = {};
}

float wheel_steer_angle(const VehicleTuning& tuning, float central_angle,
                        int wheel_index) {
    if (!wheel_is_front(wheel_index)) return 0.0f;

    const float magnitude = std::fabs(central_angle);
    if (magnitude < kEpsilon) return central_angle;

    const float wheelbase = std::max(2.0f * tuning.half_wheelbase, 0.01f);
    const float half_track = std::max(tuning.half_track, 0.0f);
    const float radius = wheelbase / std::max(std::tan(magnitude), kEpsilon);
    const bool inside =
        (central_angle > 0.0f && wheel_index == kWheelFrontRight) ||
        (central_angle < 0.0f && wheel_index == kWheelFrontLeft);
    const float wheel_radius = std::max(radius + (inside ? -half_track : half_track),
                                        0.01f);
    return std::copysign(std::atan(wheelbase / wheel_radius), central_angle);
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
        w.contact_material = collider.material(mount.x, mount.z);
    }

    s.gear = 1;
    s.engine_rpm = tuning.engine_idle_rpm;
    s.mechanical_key = vehicle_mechanical_key(collider.seed(), x, z);
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
    next.car_contact_speed = 0.0f;
    next.breakaway_id = UINT32_MAX;
    next.breakaway_velocity = glm::vec3{0.0f};
    step_vehicle_mechanical(next.mechanical, state.body_damage, dt,
                             state.mechanical_key);
    const bool engine_running = !vehicle_engine_failed(next.mechanical);

    float throttle = clampf(input.throttle, 0.0f, 1.0f);
    float brake = clampf(input.brake, 0.0f, 1.0f);
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
    const float horizontal_speed =
        glm::length(glm::vec2{body.velocity.x, body.velocity.z});
    const float burnout_pedal =
        clampf(tuning.burnout_min_pedal, 0.0f, 1.0f);
    const bool burnout_requested =
        tuning.auto_gearbox && tuning.arcade_reverse &&
        horizontal_speed <= std::max(tuning.burnout_max_speed, 0.0f) &&
        throttle >= burnout_pedal && brake >= burnout_pedal;
    const float base_lock =
        tuning.max_steer /
        (1.0f + speed * std::max(tuning.steer_speed_falloff, 0.0f));
    const float brake_steer_start =
        std::max(tuning.service_brake_steer_start_speed, 0.0f);
    const float brake_steer_full =
        std::max(tuning.service_brake_steer_full_speed,
                 brake_steer_start + 0.01f);
    const float brake_speed_blend = clampf(
        (horizontal_speed - brake_steer_start) /
            (brake_steer_full - brake_steer_start),
        0.0f, 1.0f);
    // Read the raw brake here because arcade direction selection happens
    // below. Only a forward gear at road speed can be a service-brake turn;
    // S while reversing is throttle and must retain normal steering lock.
    const float brake_steer_input =
        state.gear >= 1 ? brake * brake * brake : 0.0f;
    const float brake_steer_floor =
        clampf(tuning.service_brake_steer_scale, 0.05f, 1.0f);
    const float brake_steer_scale =
        1.0f - brake_steer_input * brake_speed_blend *
                   (1.0f - brake_steer_floor);
    const float lock = base_lock * brake_steer_scale;
    const float steer_exponent = std::max(tuning.steer_input_exponent, 0.01f);
    const float shaped_steer =
        std::copysign(std::pow(std::fabs(steer_in), steer_exponent), steer_in);
    const float front_left_damage =
        vehicle_wheel_damage(state.body_damage, kWheelFrontLeft);
    const float front_right_damage =
        vehicle_wheel_damage(state.body_damage, kWheelFrontRight);
    // A bent front corner drags the rack toward that side. Keep the pull
    // inside the current speed-sensitive lock so damage cannot ask the tyre
    // model for an impossible angle at speed.
    const float damage_pull =
        (front_right_damage - front_left_damage) *
        std::max(tuning.wheel_damage_steer_pull, 0.0f);
    const float steer_target = clampf(shaped_steer * lock + damage_pull,
                                      -lock, lock);
    const bool returning = std::fabs(steer_target) < std::fabs(state.steer_angle);
    const float steer_speed = returning ? tuning.steer_return_rate : tuning.steer_rate;
    const float steer_delta = std::max(steer_speed, 0.0f) * dt;
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
    } else if (tuning.auto_gearbox) {
        const bool nearly_stopped =
            horizontal_speed <=
            std::max(tuning.arcade_direction_change_speed, 0.0f);

        if (tuning.arcade_reverse) {
            if (burnout_requested) {
                // W+S from rest is intentional wheelspin. Put the player in
                // drive and leave both pedals alive for the axle split below.
                next.gear = 1;
            } else if (next.gear == kGearReverse) {
                if (throttle > 0.05f) {
                    if (nearly_stopped) {
                        // W takes the car back into drive after it has braked
                        // the reverse motion away.
                        next.gear = 1;
                    } else {
                        brake = std::max(brake, throttle);
                        throttle = 0.0f;
                    }
                } else {
                    // In reverse, the player's S/brake axis is the accelerator.
                    throttle = brake;
                    brake = 0.0f;
                }
            } else if (brake > 0.05f) {
                if (nearly_stopped && throttle <= 0.05f) {
                    next.gear = kGearReverse;
                    throttle = brake;
                    brake = 0.0f;
                } else {
                    // Still rolling forward: S remains a service brake. Brake
                    // wins if both pedals are down.
                    throttle = 0.0f;
                }
            } else if (next.gear <= kGearNeutral && throttle > 0.05f &&
                       nearly_stopped) {
                next.gear = 1;
            }
        }

        if (!burnout_requested && next.shift_timer <= 0.0f && next.gear >= 1) {
            float driven_slip = 0.0f;
            for (int i = 0; i < kWheelCount; ++i) {
                if (!wheel_is_driven(tuning, i)) continue;
                driven_slip = std::max(
                    driven_slip, state.wheels[static_cast<std::size_t>(i)].slip);
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
    }

    // Preserve useful analogue modulation below full pedal. The player-facing
    // keyboard ramp reaches 1 quickly, while AI and controller half-brake
    // inputs remain gentle instead of receiving half of an enormous arcade
    // stop. Full brake is unchanged. Calculated AFTER arcade direction control
    // because S is throttle, not a service brake, once reverse engages.
    const float service_brake_input = brake * brake * brake;
    const float active_front_drive_bias = clampf(
        burnout_requested ? tuning.burnout_front_drive_bias
                          : tuning.front_drive_bias,
        0.0f, 1.0f);

    // --- engine -------------------------------------------------------------
    const float rpm = engine_running
        ? engine_rpm_for(tuning, next, active_front_drive_bias, throttle,
                         state.engine_rpm, dt) : 0.0f;
    const float ratio = gear_ratio(tuning, next.gear) * tuning.final_drive;
    const float crank_torque = engine_running ? engine_torque_at(tuning, rpm) : 0.0f;

    // Axle torque, split front/rear by the drive bias and then between the two
    // wheels of each axle. There is no torque cut across a shift: the gearbox
    // hands over instantly, so an upshift is a step change in wheel torque
    // rather than the brief lift a real one gives you.
    const float axle_torque =
        crank_torque * throttle * ratio * tuning.drivetrain_efficiency;
    const float front_share =
        front_is_driven(active_front_drive_bias)
            ? active_front_drive_bias * 0.5f
            : 0.0f;
    const float rear_share =
        rear_is_driven(active_front_drive_bias)
            ? (1.0f - active_front_drive_bias) * 0.5f
            : 0.0f;

    // Engine braking, referred to the axle. Kept alive at low revs rather than
    // fading to nothing, because the whole point of it is holding the car back
    // on a descent, which happens at low revs.
    const float rev_frac = clampf(rpm / std::max(tuning.engine_peak_rpm, 1.0f),
                                  0.0f, 2.0f);
    const float engine_brake_axle =
        (engine_running ? tuning.engine_brake_torque : 0.0f) * (0.35f + 0.65f * rev_frac) *
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
        c.material = hit.material;
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

    // --- anti-roll bars ----------------------------------------------------
    // Each axle couples its two struts. Equal and opposite forces add no net
    // lift; they only oppose roll. Clamp the transfer so the bar cannot invent
    // a negative tyre load and make an inside wheel pull on the road.
    const auto apply_anti_roll = [&](int left_index, int right_index,
                                     float stiffness) {
        WheelContact& left = contacts[static_cast<std::size_t>(left_index)];
        WheelContact& right_contact =
            contacts[static_cast<std::size_t>(right_index)];
        if (!left.grounded || !right_contact.grounded || !(stiffness > 0.0f)) return;

        const float travel_delta =
            right_contact.suspension_length - left.suspension_length;
        const float raw = travel_delta * stiffness;
        const float transfer =
            clampf(raw, -left.normal_force, right_contact.normal_force);

        body.add_force_at(up * transfer, left.mount_world);
        body.add_force_at(-up * transfer, right_contact.mount_world);
        left.normal_force += transfer;
        right_contact.normal_force -= transfer;
    };
    apply_anti_roll(kWheelFrontLeft, kWheelFrontRight, tuning.anti_roll_front);
    apply_anti_roll(kWheelRearLeft, kWheelRearRight, tuning.anti_roll_rear);

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
    // Hysteresis: engage below recovery_up_dot, and stay engaged until the car
    // is properly upright at recovery_release_dot. Once the timer is running,
    // the higher threshold holds it — see the note in vehicle.h for the stable
    // state this exists to break out of.
    const float engage_dot = (state.recovery_timer > 0.0f)
                                 ? tuning.recovery_release_dot
                                 : tuning.recovery_up_dot;
    int recovery_grounded_wheels = 0;
    for (const WheelContact& contact : contacts) {
        if (contact.grounded) ++recovery_grounded_wheels;
    }
    bool chassis_near_ground = false;
    {
        glm::vec3 corners[8];
        chassis_corners(tuning, corners);
        for (const glm::vec3& local : corners) {
            const glm::vec3 p = body.to_world(local);
            const float clearance =
                p.y - collider.height(p.x, p.z) - tuning.ground_skin;
            if (clearance < 0.08f) {
                chassis_near_ground = true;
                break;
            }
        }
    }
    // Nose/tail balancing is a separate dead state from rolling onto a side.
    // At roughly sixty degrees the chassis still counts as upright, so the
    // suspension stays alive, one axle keeps driving, and the translation-only
    // ground guard can let the bumper skate forever. Detect pitch directly
    // from chassis-forward. Requiring an incomplete tyre set and a body corner
    // almost touching terrain keeps this off ordinary crest jumps and
    // all-four-wheel steep climbs.
    const bool bumper_balanced =
        recovery_grounded_wheels < kWheelCount &&
        chassis_near_ground &&
        std::fabs(glm::dot(forward, world_up)) >
            std::max(tuning.recovery_pitch_sine, 0.0f);
    const bool inverted =
        up_dot < engage_dot &&
        collider.probe_down(body.position, tuning.recovery_ground_reach).hit;
    next.recovery_timer = inverted ? state.recovery_timer + dt : 0.0f;
    next.pitch_recovery_timer =
        bumper_balanced ? state.pitch_recovery_timer + dt : 0.0f;

    if (next.recovery_timer > tuning.recovery_delay ||
        next.pitch_recovery_timer > tuning.recovery_delay) {
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
        if (wheel_is_driven(active_front_drive_bias, i)) ++driven_count;
    }
    const float reflected_inertia =
        (engine_running && driven_count > 0)
            ? tuning.engine_inertia * ratio * ratio / static_cast<float>(driven_count)
            : 0.0f;

    for (int i = 0; i < kWheelCount; ++i) {
        const std::size_t wi = static_cast<std::size_t>(i);
        const WheelContact& c = contacts[wi];
        WheelState& w = next.wheels[wi];
        const float wheel_damage =
            vehicle_wheel_damage(state.body_damage, i);

        // What this wheel actually has to spin up. A driven wheel is dragging
        // the engine round with it; an undriven one is not.
        const float wheel_inertia =
            std::max(tuning.wheel_inertia, 0.01f) +
            (wheel_is_driven(active_front_drive_bias, i) ? reflected_inertia
                                                         : 0.0f);

        // Reduced mass of the coupled wheel-and-corner system. An impulse along
        // the contact patch has to change BOTH the car's speed and the wheel's
        // spin, so the impulse that removes their relative slip is smaller than
        // either alone would suggest — and using the corner mass here instead
        // makes the model stiff enough to explode at 120 Hz.
        const float reduced_mass =
            1.0f / (1.0f / corner_mass +
                    tuning.wheel_radius * tuning.wheel_radius / wheel_inertia);

        float omega = state.wheels[wi].angular_velocity;

        // Contact axes are needed before braking so the service-brake ABS can
        // compare road speed with rim speed. They are reused by the tyre force
        // calculation below; the handbrake deliberately bypasses this logic.
        glm::vec3 fwd_g = forward;
        glm::vec3 lat_g = right;
        float v_long = 0.0f;
        float v_lat = 0.0f;
        if (c.grounded) {
            const float wheel_angle =
                wheel_steer_angle(tuning, next.steer_angle, i);
            const glm::vec3 wheel_forward =
                forward * std::cos(wheel_angle) + right * std::sin(wheel_angle);
            fwd_g = normalise_or(
                wheel_forward - c.normal * glm::dot(wheel_forward, c.normal),
                forward);
            lat_g = normalise_or(glm::cross(c.normal, fwd_g), right);
            const glm::vec3 v_contact = body.point_velocity(c.point);
            v_long = glm::dot(v_contact, fwd_g);
            v_lat = glm::dot(v_contact, lat_g);
        }
        const float slip_ref =
            std::max(tuning.tyre_peak_slip +
                         tuning.tyre_peak_slip_ratio * std::fabs(v_long),
                     kEpsilon);

        // --- torques into the wheel ---------------------------------------
        const float share = wheel_is_front(i) ? front_share : rear_share;
        omega += axle_torque * share / wheel_inertia * dt;

        // Everything that resists rotation is applied as a torque that cannot
        // push omega past zero within the step. Letting it overshoot is how a
        // braked wheel ends up spinning backwards under the car.
        float resist =
            engine_brake_axle *
            (wheel_is_driven(active_front_drive_bias, i) ? share : 0.0f);
        const float brake_bias_front = clampf(
            burnout_requested ? tuning.burnout_brake_bias_front
                              : tuning.brake_bias_front,
            0.0f, 1.0f);
        const float service_brake_share =
            wheel_is_front(i) ? brake_bias_front * 0.5f
                              : (1.0f - brake_bias_front) * 0.5f;
        float service_brake =
            tuning.brake_torque * service_brake_input * service_brake_share;

        // Service-brake ABS: cap this step's brake torque at the wheel speed
        // that puts longitudinal slip on the peak of the tyre curve. More
        // pedal still reaches that peak sooner, but cannot lock the wheel and
        // throw away roughly a third of its available grip. The handbrake is
        // added afterwards and stays free to lock the rear wheels for a slide.
        if (!burnout_requested && c.grounded && service_brake > 0.0f &&
            tuning.service_abs_target_slip > 0.0f) {
            const float target_patch_speed =
                std::max(0.0f, std::fabs(v_long) -
                                   slip_ref * tuning.service_abs_target_slip);
            const float target_omega = target_patch_speed / tuning.wheel_radius;
            const float torque_to_target =
                std::max(0.0f, (std::fabs(omega) - target_omega) *
                                   wheel_inertia / dt);
            service_brake = std::min(service_brake,
                                     std::max(0.0f, torque_to_target - resist));
        }
        resist += service_brake;
        if (!wheel_is_front(i)) {
            resist += tuning.handbrake_torque * handbrake * 0.5f;
        }
        resist += tuning.rolling_resistance * c.rolling_scale * c.normal_force *
                  tuning.wheel_radius;
        resist += std::max(tuning.wheel_damage_rolling_resistance, 0.0f) *
                  wheel_damage * c.normal_force * tuning.wheel_radius;

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
            w.contact_material = Surface::Rock;
            w.suspension_length = c.suspension_length;
            w.normal_force = 0.0f;
            w.slip = 0.0f;
            w.angular_velocity = omega;
            continue;
        }

        // Slip is the contact patch's velocity RELATIVE TO THE GROUND: how fast
        // the rubber is being dragged across it.
        const float slip_long = v_long - omega * tuning.wheel_radius;
        const float slip_mag = std::sqrt(slip_long * slip_long + v_lat * v_lat);

        // Normalised against a reference that grows with speed, so the peak
        // sits at a roughly constant slip ANGLE. A fixed slip speed would put
        // the car permanently past the peak on a straight and permanently
        // under it in a car park.
        const float u = slip_mag / slip_ref;

        float mu = c.grip * slip_response(tuning, u);
        mu *= 1.0f - wheel_damage *
                         (1.0f - clampf(tuning.wheel_damage_grip_floor,
                                        0.05f, 1.0f));

        // Classic crime-game service brakes get extra LONGITUDINAL authority
        // from the tyre, not a larger lateral budget. Multiplying the whole
        // friction circle let a hard brake-turn generate several g sideways;
        // when a wall removed the remaining travel, that impossible grip
        // tripped the chassis onto its edge. A wider longitudinal axis keeps
        // the short stop while the normal lateral tyre limit still decides
        // whether the car carves or scrubs.
        // The unbraked burnout axle must stay allowed to spin. Giving those
        // tyres the normal 3x stopping grip would turn the brake stand into a
        // launch even though the wheel torques are split correctly.
        float service_brake_grip_scale = 1.0f;
        if (!burnout_requested || service_brake_share > 0.001f) {
            service_brake_grip_scale =
                1.0f + service_brake_input *
                           (std::max(tuning.service_brake_grip_boost, 1.0f) - 1.0f);
        }

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
        const float budget = mu * c.normal_force * dt;
        const float lateral_scale =
            std::max(tuning.lateral_grip_scale, 0.05f);

        // An ellipse lets profiles tune braking and cornering independently.
        // Scale both demands into the base friction-circle space for clipping,
        // then apply one fraction to the real impulse so its direction stays
        // honest and combined braking/cornering still shares finite grip.
        const float scaled_long = want_long / service_brake_grip_scale;
        const float scaled_lat = want_lat / lateral_scale;
        const float want_mag =
            std::sqrt(scaled_long * scaled_long + scaled_lat * scaled_lat);

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
        w.contact_material = c.material;
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
            if (!wheel_is_driven(active_front_drive_bias, i)) continue;
            sum += next.wheels[static_cast<std::size_t>(i)].angular_velocity;
        }
        const float mean = sum / static_cast<float>(driven_count);
        const float blend = clampf(tuning.differential_coupling * dt, 0.0f, 1.0f);
        for (int i = 0; i < kWheelCount; ++i) {
            if (!wheel_is_driven(active_front_drive_bias, i)) continue;
            float& w = next.wheels[static_cast<std::size_t>(i)].angular_velocity;
            w += (mean - w) * blend;
        }
    }

    // Old-school arcade stability under the SERVICE brake. The tyre model
    // still owns stopping distance and surface response; this gathers the
    // sideways motion and yaw left after the contact impulses, which is the
    // part a player reads as a long skid. Keeping it off the handbrake
    // preserves deliberate slides.
    if (service_brake_input > 0.0f) {
        const float lateral_blend =
            1.0f - std::exp(-std::max(tuning.service_brake_lateral_damping, 0.0f) *
                            service_brake_input * dt);
        body.velocity -= right * glm::dot(body.velocity, right) * lateral_blend;

        const float yaw_blend =
            1.0f - std::exp(-std::max(tuning.service_brake_yaw_damping, 0.0f) *
                            service_brake_input * dt);
        body.angular_velocity -=
            up * glm::dot(body.angular_velocity, up) * yaw_blend;
    }

    // A low, wide car should not turn a kerb strike into an instant barrel
    // roll. Damp only the rotation around the car's nose, and only while the
    // suspension has a real pair of contacts to push against. This keeps
    // airborne spins honest and does not interfere with handbrake yaw.
    int grounded_wheels = 0;
    for (const WheelContact& contact : contacts) {
        if (contact.grounded) ++grounded_wheels;
    }
    if (grounded_wheels >= 2 || chassis_near_ground) {
        const float roll_rate = glm::dot(body.angular_velocity, forward);
        const float threshold =
            std::max(tuning.grounded_roll_damping_threshold, 0.0f);
        const float excess = std::max(std::fabs(roll_rate) - threshold, 0.0f);
        const float roll_blend =
            1.0f - std::exp(-std::max(tuning.grounded_roll_damping, 0.0f) * dt);
        if (excess > 0.0f) {
            body.angular_velocity -=
                forward * std::copysign(excess * roll_blend, roll_rate);
        }

        // A sharp terrain edge can deliver its whole angular impulse in one
        // fixed step, then leave the tyres airborne before exponential
        // damping gets a second chance. Cap only that roll component while
        // the car is still supported or grazing the ground. Yaw, pitch and
        // genuinely airborne rotation are unchanged.
        const float damped_roll_rate =
            glm::dot(body.angular_velocity, forward);
        const float roll_limit =
            std::max(tuning.grounded_roll_rate_limit, 0.0f);
        if (roll_limit > 0.0f && std::fabs(damped_roll_rate) > roll_limit) {
            body.angular_velocity -=
                forward * (damped_roll_rate -
                           std::copysign(roll_limit, damped_roll_rate));
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
        float strongest_impact_speed = 0.0f;
        glm::vec3 strongest_contact_local{0.0f, 0.0f,
                                          -tuning.chassis_half_length};
        glm::vec2 strongest_motion_local{0.0f, -1.0f};
        float strongest_contact_height = 0.45f;
        float strongest_contact_radius = 0.25f;
        float strongest_glancing = 0.0f;
        // The chassis' own vertical extent, not just its centre of mass. A wall
        // taller than the car has to block it, and a kerb shorter than the
        // floor pan has to be driven over — testing the CoM point alone gets
        // the first of those wrong, and a wall the car sails through is not a
        // wall.
        const float chassis_low = body.position.y + tuning.chassis_floor;
        const float chassis_high = body.position.y + tuning.chassis_roof;

        for (const StaticBox& b : collider.static_boxes()) {
            if (!b.enabled) continue;
            if (b.bounds.max.y <= chassis_low || b.bounds.min.y >= chassis_high) {
                continue;
            }
            // The enclosing AABB is only a broad bound. Test the chassis in
            // the prop's actual frame so rotated restaurant walls do not
            // create invisible solid wedges in the forecourt.
            const glm::vec3 local_body = b.local_point(body.position);
            const AABB& bounds = b.collision_bounds();
            const float nx = clampf(local_body.x, bounds.min.x, bounds.max.x);
            const float nz = clampf(local_body.z, bounds.min.z, bounds.max.z);
            float dx = local_body.x - nx;
            float dz = local_body.z - nz;
            const float d2 = dx * dx + dz * dz;
            BreakawayContact pole_contact;
            if (b.breakaway_speed > 0.0f) {
                pole_contact=breakaway_contact(body.position,body.orientation,
                    {tuning.car_collision_half_width,tuning.car_collision_half_length},
                    b.bounds.center(),std::max(b.bounds.extents().x,b.bounds.extents().z));
                if (!pole_contact.hit) continue;
            } else if (d2 >= r * r) continue;

            float push;
            if (pole_contact.hit) {
                const auto local=b.local_direction(pole_contact.normal);
                dx=local.x;dz=local.z;push=pole_contact.penetration;
            } else if (d2 > kEpsilon) {
                const float d = std::sqrt(d2);
                dx /= d;
                dz /= d;
                push = r - d;
            } else {
                // Centre is inside the footprint. Leave by the nearest face,
                // which is the only exit that does not shove the car through
                // the whole prop.
                const float to_min_x = local_body.x - bounds.min.x;
                const float to_max_x = bounds.max.x - local_body.x;
                const float to_min_z = local_body.z - bounds.min.z;
                const float to_max_z = bounds.max.z - local_body.z;
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

            const glm::vec3 local_normal{dx, 0.0f, dz};
            const glm::vec3 world_normal = b.world_direction(local_normal);
            dx = world_normal.x;
            dz = world_normal.z;
            const float closing = body.velocity.x * dx + body.velocity.z * dz;
            const bool release = b.breakaway_speed > 0.0f &&
                -closing >= b.breakaway_speed && b.breakaway_id != UINT32_MAX &&
                next.breakaway_id == UINT32_MAX;
            if (release) {
                next.breakaway_id = b.breakaway_id;
                next.breakaway_velocity = body.velocity;
            } else {
                body.position.x += dx * push;
                body.position.z += dz * push;
            }
            if (closing < 0.0f) {
                const float impact_speed = -closing;
                if (b.is_vehicle)
                    next.car_contact_speed=std::max(next.car_contact_speed,impact_speed);
                if (impact_speed > strongest_impact_speed) {
                    strongest_impact_speed = impact_speed;
                    const float contact_y = clampf(
                        body.position.y, b.bounds.min.y, b.bounds.max.y);
                    const glm::vec3 contact_offset_world = pole_contact.hit
                        ? glm::vec3{b.bounds.center().x-body.position.x,
                            contact_y-body.position.y,b.bounds.center().z-body.position.z}
                        : glm::vec3{-dx * r, contact_y - body.position.y, -dz * r};
                    strongest_contact_local =
                        glm::conjugate(body.orientation) * contact_offset_world;
                    const glm::vec3 local_motion3 =
                        glm::conjugate(body.orientation) * body.velocity;
                    strongest_motion_local =
                        glm::vec2{local_motion3.x, local_motion3.z};
                    strongest_contact_height = clampf(
                        (strongest_contact_local.y - tuning.chassis_floor) /
                            std::max(tuning.chassis_roof -
                                         tuning.chassis_floor,
                                     0.01f),
                        0.0f, 1.0f);
                    const float box_width =
                        std::max(bounds.max.x - bounds.min.x, 0.0f);
                    const float box_depth =
                        std::max(bounds.max.z - bounds.min.z, 0.0f);
                    const float tangent_span =
                        std::fabs(local_normal.z) * box_width +
                        std::fabs(local_normal.x) * box_depth;
                    strongest_contact_radius = clampf(
                        tangent_span /
                            std::max(2.0f * tuning.chassis_half_width, 0.01f),
                        0.05f, 1.0f);
                    const float horizontal = glm::length(
                        glm::vec2{body.velocity.x, body.velocity.z});
                    strongest_glancing = horizontal > kEpsilon
                        ? clampf(1.0f - impact_speed / horizontal, 0.0f, 1.0f)
                        : 0.0f;
                }

                // A sheared mounting takes a little momentum, not the whole
                // car. Keep the usual dent/impact event for crash feedback.
                const float response = release ? 0.18f :
                    1.0f + clampf(tuning.collision_restitution, 0.0f, 0.5f);
                body.velocity.x -= dx * closing * response;
                body.velocity.z -= dz * closing * response;
            }
        }

        // One event per step, even at a building corner where two authored
        // boxes overlap. Both faces still resolve velocity; health takes only
        // the strongest normal hit instead of charging twice for one crash.
        const float damage =
            std::max(strongest_impact_speed -
                         std::max(tuning.impact_safe_speed, 0.0f),
                     0.0f) *
            std::max(tuning.impact_damage_per_mps, 0.0f);
        if (damage > 0.0f) {
            next.health = clampf(next.health - damage, 0.0f, 100.0f);
            apply_vehicle_impact(next.body_damage, strongest_contact_local,
                                 damage, tuning.chassis_half_width,
                                 tuning.chassis_half_length,
                                 strongest_motion_local,
                                 strongest_contact_height,
                                 strongest_contact_radius,
                                 strongest_glancing, tuning.body_damage_gain);
            next.last_impact_speed = strongest_impact_speed;
            next.last_impact_damage = damage;
            ++next.impact_count;
        }
    }

    // --- ground guard -------------------------------------------------------
    // LAST RESORT, and it should almost never fire. The suspension is what
    // holds the car up; this only refuses to let a step END with the bodywork
    // below the TERRAIN — a landing that outruns the strut travel, a spawn
    // inside a hill, a car on its roof with the suspension switched off.
    // Correct overlap without changing orientation directly. The contact
    // impulse must act at the bodywork, however: cancelling CoM vertical speed
    // alone supports a nose-high car forever without letting it pivot down.
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
            // Average the deepest corners so a flat bumper/floor contact has
            // a centred support point and cannot invent a left/right kick.
            glm::vec3 support{0.0f};
            int support_count = 0;
            for (const glm::vec3& local : corners) {
                const glm::vec3 p = body.position + pose * local;
                const float depth =
                    collider.height(p.x, p.z) + tuning.ground_skin - p.y;
                if (depth > 0.0f &&
                    depth >= deepest - std::max(tuning.ground_skin, 0.0f)) {
                    support += p;
                    ++support_count;
                }
            }
            support /= static_cast<float>(support_count);
            const float closing_speed = glm::dot(body.point_velocity(support), world_up);
            if (closing_speed < 0.0f) {
                // The pose has advanced since the suspension force pass.
                body.inv_inertia_world = pose * inv_i_local * glm::transpose(pose);
                const glm::vec3 lever = glm::cross(support - body.position, world_up);
                const float inverse_mass = body.inv_mass +
                    glm::dot(lever, body.inv_inertia_world * lever);
                body.add_impulse_at(world_up * (-closing_speed / inverse_mass), support);
            }
            const float lift =
                std::min(deepest, std::max(tuning.ground_correction_rate, 0.0f) * dt);
            body.position.y += lift;
        }
    }

    // --- write back ---------------------------------------------------------
    next.position = body.position;
    next.orientation = body.orientation;
    next.velocity = body.velocity;
    next.angular_velocity = body.angular_velocity;
    next.engine_rpm = engine_running
        ? engine_rpm_for(tuning, next, active_front_drive_bias,
                         throttle, rpm, dt) : 0.0f;

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
