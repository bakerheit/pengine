#include "game/vehicle.h"

#include <algorithm>
#include <cmath>

#include "physics/world_collision.h"
#include "world/heightmap.h"

namespace pengine {

namespace {

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

// Project a world-space direction onto the ground plane defined by `n`,
// then renormalise. Returns zero if `d` is parallel to `n`.
glm::vec3 project_on_plane(const glm::vec3& d, const glm::vec3& n) {
    glm::vec3 p = d - n * glm::dot(d, n);
    float len = glm::length(p);
    if (len < 1e-5f) return glm::vec3{0.f};
    return p / len;
}

} // namespace

void Vehicle::spawn(const glm::vec3& spawn_pos, float yaw_deg) {
    body_ = RigidBody{};
    body_.set_box_inertia(chassis_mass, chassis_full_extents);
    body_.position    = spawn_pos;
    body_.orientation = glm::angleAxis(glm::radians(yaw_deg + 90.f),
                                        glm::vec3{0.f, 1.f, 0.f});
    body_.linear_vel  = {0.f, 0.f, 0.f};
    body_.angular_vel = {0.f, 0.f, 0.f};

    // Mounts sit one CoM-height below the body origin (= a real car's CoM
    // sits just above the floor / drivetrain). Inset slightly along X/Z.
    float hx = chassis_full_extents.x * 0.5f - 0.15f;
    float hy = -com_height_above_mount;
    float hz = chassis_full_extents.z * 0.5f - 0.5f;

    wheels_[0] = Wheel{}; wheels_[0].mount_local = {-hx, hy, -hz}; wheels_[0].is_steering = true;  wheels_[0].is_driven = false; // FL
    wheels_[1] = Wheel{}; wheels_[1].mount_local = { hx, hy, -hz}; wheels_[1].is_steering = true;  wheels_[1].is_driven = false; // FR
    wheels_[2] = Wheel{}; wheels_[2].mount_local = {-hx, hy,  hz}; wheels_[2].is_steering = false; wheels_[2].is_driven = true;  // RL
    wheels_[3] = Wheel{}; wheels_[3].mount_local = { hx, hy,  hz}; wheels_[3].is_steering = false; wheels_[3].is_driven = true;  // RR

    // Default visual AABB = the chassis box, asymmetric about origin to
    // account for the CoM sitting low. traffic.cpp overrides this with
    // the true mesh bounds for cars that have a body model.
    float vex = chassis_full_extents.x * 0.5f;
    float vez = chassis_full_extents.z * 0.5f;
    visual_aabb_min_ = {-vex, -com_height_above_mount, -vez};
    visual_aabb_max_ = {+vex,
                         chassis_full_extents.y - com_height_above_mount,
                         +vez};

    throttle_ = brake_ = steer_in_ = steer_rad_ = 0.f;
    handbrake_ = false;
}

void Vehicle::set_inputs(float throttle, float brake, float steer, bool handbrake) {
    throttle_  = clampf(throttle, -1.f, 1.f);
    brake_     = clampf(brake,     0.f, 1.f);
    steer_in_  = clampf(steer,    -1.f, 1.f);
    handbrake_ = handbrake;
}

bool Vehicle::airborne() const {
    for (const Wheel& w : wheels_) if (w.grounded) return false;
    return true;
}

void Vehicle::substep(float dt, const WorldCollision& world) {
    // ---- Steering: lerp toward input ---------------------------------------
    float target_steer = steer_in_ * max_steer_rad;
    steer_rad_ += (target_steer - steer_rad_) * std::min(1.f, steer_lerp * dt);

    // ---- Wheel raycasts + suspension forces --------------------------------
    glm::vec3 chassis_down = body_.to_world_dir({0.f, -1.f, 0.f});
    glm::vec3 chassis_up   = -chassis_down;

    // Suspension only makes sense when the chassis is roughly upright. When
    // tipped past ~73°, raycasts in chassis-down can hit ground at near-
    // horizontal angles and apply spring/drive/lateral forces that push the
    // car sideways across the ground. Disable wheel forces in that case;
    // chassis-on-ground friction below takes over.
    const bool chassis_upright = glm::dot(chassis_up, glm::vec3{0.f, 1.f, 0.f}) > 0.3f;

    for (Wheel& w : wheels_) {
        if (!chassis_upright) {
            w.grounded         = false;
            w.compression      = 0.f;
            w.normal_force     = 0.f;
            w.visual_drop      = suspension_rest;
            w.prev_compression = 0.f;
            continue;
        }

        glm::vec3 mount_w = body_.to_world_point(w.mount_local);
        float ray_len = suspension_max + wheel_radius;
        RayHit hit = world.raycast(mount_w, chassis_down, ray_len);

        if (hit.hit && hit.t <= ray_len) {
            // Compression saturates at suspension_rest (the bumpstop), but
            // the *visual* drop should track the actual hit so the wheel
            // sits at the terrain. Without the signed form below the wheel
            // snaps up to the mount whenever hit.t < wheel_radius, which
            // looks like the tire is sucked into the body.
            float susp_signed = hit.t - wheel_radius;
            float susp_len    = std::max(0.f, susp_signed);
            w.compression     = clampf(suspension_rest - susp_len, 0.f, suspension_rest);
            w.grounded        = true;
            w.contact_world   = mount_w + chassis_down * hit.t;
            w.contact_normal  = hit.normal;
            w.visual_drop     = susp_signed;

            // Spring + damper. The damper input is the body's velocity at
            // the mount projected onto chassis-down (i.e. how fast the sprung
            // mass is moving toward the ground at this corner). That's the
            // physically correct relative motion across the shock, AND it's
            // smooth — the previous formulation took a finite difference of
            // the raycasted compression, which spiked to tens of m/s on
            // sharp terrain features (the V-valley launch problem) even
            // though the body itself barely moved.
            constexpr float MAX_DCOMPRESS_RATE = 8.f;     // m/s — sanity cap
            constexpr float MAX_WHEEL_FORCE    = 80000.f; // N — ~12× rest weight
            float dcompress  = glm::dot(body_.point_velocity(mount_w), chassis_down);
            dcompress        = clampf(dcompress, -MAX_DCOMPRESS_RATE, MAX_DCOMPRESS_RATE);
            float force_mag  = w.compression * spring_k + dcompress * damper_k;
            force_mag        = clampf(force_mag, 0.f, MAX_WHEEL_FORCE);
            w.normal_force   = force_mag;
            body_.apply_force_at(chassis_up * force_mag, mount_w);
        } else {
            // Airborne: hang the wheel at the max suspension travel, not at
            // rest length. This is continuous with the grounded branch (a
            // ray that just barely fails returns visual_drop ≈ suspension_max
            // either way), so wheels skirting the edge of contact don't
            // flicker between two visual heights.
            w.grounded     = false;
            w.compression  = 0.f;
            w.normal_force = 0.f;
            w.visual_drop  = suspension_max;
        }
        w.prev_compression = w.compression;
    }

    // ---- Gravity (central) -------------------------------------------------
    body_.apply_central_force({0.f, gravity * body_.mass, 0.f});

    // ---- Drive / brake -----------------------------------------------------
    int  driven_grounded = 0;
    for (const Wheel& w : wheels_) if (w.is_driven && w.grounded) ++driven_grounded;
    bool any_driven_grounded = (driven_grounded > 0);
    if (driven_grounded == 0) driven_grounded = 1; // avoid /0

    glm::vec3 fwd_world = forward();
    float speed_signed  = glm::dot(body_.linear_vel, fwd_world);
    float drive_factor_fwd = clampf(1.f - std::max(0.f, speed_signed) / max_speed, 0.f, 1.f);
    float drive_factor_rev = clampf(1.f - std::max(0.f, -speed_signed) / max_reverse, 0.f, 1.f);
    // Drag compensation. Without this, the (1 − v/max) torque curve drops to
    // zero exactly where air drag is still pulling, so terminal velocity
    // settles at engine_force / (m·linear_drag + engine_force/max_speed) —
    // well below max_speed. Adding m·k·v of thrust at full throttle cancels
    // the longitudinal drag, leaving net accel = engine·(1 − v/max)/m, which
    // actually reaches max_speed. Applied as a central force (below) rather
    // than through the wheel mounts: drag itself is a central body force, so
    // its compensation has zero pitch arm. Routing it through the mounts
    // would compound the engine's mount-level pitch torque and visibly
    // wheelie the chassis at high speed. Capped at max_speed so we don't
    // keep boosting past the rated top end.
    float drag_comp_v_fwd  = std::min(std::max(0.f, speed_signed),  max_speed);
    float drag_comp_v_rev  = std::min(std::max(0.f, -speed_signed), max_reverse);
    float drag_comp_fwd    = chassis_mass * linear_drag * drag_comp_v_fwd;
    float drag_comp_rev    = chassis_mass * linear_drag * drag_comp_v_rev;

    for (Wheel& w : wheels_) {
        if (!w.grounded) continue;

        // Per-wheel ground-plane axes (steering for fronts).
        glm::vec3 wheel_fwd = body_.to_world_dir({0.f, 0.f, -1.f});
        if (w.is_steering) {
            float c = std::cos(steer_rad_), s = std::sin(steer_rad_);
            // Rotate around chassis up by steer_rad_.
            glm::vec3 wheel_right = body_.to_world_dir({1.f, 0.f, 0.f});
            wheel_fwd = wheel_fwd * c + wheel_right * s;
        }
        glm::vec3 fwd_g = project_on_plane(wheel_fwd, w.contact_normal);
        glm::vec3 lat_g = glm::normalize(glm::cross(w.contact_normal, fwd_g));
        if (glm::length(fwd_g) < 1e-4f) continue;

        // Throttle (only on driven wheels). Signed: + = forward, - = reverse.
        // Drive force is applied at the wheel mount (chassis-bottom level)
        // rather than at the contact patch. The longer lever arm from contact
        // to COM produces unrealistic pitch torque (visible wheelies); real
        // drivetrains transmit longitudinal load through the suspension link
        // at mount level — this is the "anti-squat" geometry trick. Lateral
        // grip and brake still go through the contact, where they belong.
        glm::vec3 mount_w = body_.to_world_point(w.mount_local);
        if (w.is_driven && throttle_ > 0.f) {
            float per_wheel = (engine_force * drive_factor_fwd)
                              / static_cast<float>(driven_grounded);
            body_.apply_force_at(fwd_g * per_wheel * throttle_, mount_w);
        } else if (w.is_driven && throttle_ < 0.f) {
            float per_wheel = (reverse_force * drive_factor_rev)
                              / static_cast<float>(driven_grounded);
            body_.apply_force_at(fwd_g * per_wheel * throttle_, mount_w);
        }

        // Brake (all wheels). Opposes current contact-point velocity along fwd_g.
        glm::vec3 v_contact = body_.point_velocity(w.contact_world);
        float v_long = glm::dot(v_contact, fwd_g);
        if (brake_ > 0.f) {
            float bf = (brake_force / 4.f) * brake_;
            // Brake force opposes longitudinal motion; clamp so we don't reverse.
            float impulse_cap = std::abs(v_long) * (body_.mass / 4.f) / std::max(dt, 1e-5f);
            float bmag = std::min(bf, impulse_cap);
            if (v_long > 0.f) bmag = -bmag;
            body_.apply_force_at(fwd_g * bmag, w.contact_world);
        } else if (throttle_ == 0.f) {
            // Engine braking + rolling resistance on coast. Strong enough to
            // hold the car on a gentle slope, weak enough not to stop it
            // unrealistically fast at speed.
            float coast_force = 250.f + std::abs(v_long) * 80.f;
            float impulse_cap = std::abs(v_long) * (body_.mass / 4.f) / std::max(dt, 1e-5f);
            float cmag = std::min(coast_force, impulse_cap);
            if (v_long > 0.f)      cmag = -cmag;
            else if (v_long < 0.f) {/* already +ve */}
            else                   cmag = 0.f;
            body_.apply_force_at(fwd_g * cmag, w.contact_world);
        }

        // Lateral grip: kill sideways velocity at the contact point. Apply as
        // an impulse this substep (capped by friction circle vs normal force).
        // Like drive force above, the impulse is applied at the wheel MOUNT
        // rather than the contact patch — the "anti-roll" analogue of the
        // anti-squat trick. Routing lateral load through the contact gives a
        // moment arm of (chassis_height/2 + suspension_rest + wheel_radius),
        // which drops the static tip threshold below the friction cap (~μg)
        // and rolls cars on ordinary corners. Real suspensions transmit
        // lateral load to the body at the roll centre, which sits near mount
        // level; doing the same here restores the intended thresholds (sedan
        // ~25 m/s², truck ~11 m/s²) so sedans slide and only trucks tip.
        // v_lat is still measured at the contact (where the tire actually
        // grips the ground); only the application point moves.
        float v_lat = glm::dot(v_contact, lat_g);
        float grip  = lateral_grip;
        if (handbrake_ && !w.is_steering) grip = handbrake_grip; // rear loose
        // Required impulse to kill v_lat over this substep (per wheel, mass/4).
        float wanted_impulse = -v_lat * (body_.mass / 4.f) * std::min(1.f, grip * dt);
        // Friction circle cap: total horizontal friction <= mu * N.
        float mu = 0.9f;
        float max_friction_impulse = mu * w.normal_force * dt;
        wanted_impulse = clampf(wanted_impulse, -max_friction_impulse, max_friction_impulse);
        body_.apply_impulse_at(lat_g * wanted_impulse, mount_w);
    }

    // Drag compensation, applied centrally so it doesn't add pitch torque
    // (see the drag_comp comment block above). Only fires when at least one
    // driven wheel is on the ground — a wheels-up car shouldn't get a free
    // forward shove.
    if (any_driven_grounded) {
        if (throttle_ > 0.f) {
            body_.apply_central_force(fwd_world * (drag_comp_fwd * throttle_));
        } else if (throttle_ < 0.f) {
            body_.apply_central_force(fwd_world * (drag_comp_rev * throttle_));
        }
    }

    // ---- Integrate ---------------------------------------------------------
    body_.integrate(dt);

    // ---- Drag --------------------------------------------------------------
    {
        // Linear drag on horizontal motion only (don't fight gravity).
        glm::vec3 lv = body_.linear_vel;
        glm::vec3 lv_h{lv.x, 0.f, lv.z};
        lv_h *= std::exp(-linear_drag * dt);
        body_.linear_vel = {lv_h.x, lv.y, lv_h.z};

        body_.angular_vel *= std::exp(-angular_drag * dt);
    }

    // ---- Cylinder vs buildings (XZ) ----------------------------------------
    {
        glm::vec2 xz{body_.position.x, body_.position.z};
        float feet_y = body_.position.y - com_height_above_mount - suspension_rest;
        glm::vec2 fixed = world.resolve_cylinder_xz(xz, feet_y,
                                                     chassis_full_extents.y + suspension_rest,
                                                     chassis_collision_radius);
        if (fixed != xz) {
            // Position correction: also kill velocity along the push direction.
            glm::vec3 push{fixed.x - xz.x, 0.f, fixed.y - xz.y};
            body_.position.x = fixed.x;
            body_.position.z = fixed.y;
            float plen = glm::length(push);
            if (plen > 1e-4f) {
                glm::vec3 n = push / plen;
                float vn = glm::dot(body_.linear_vel, n);
                if (vn < 0.f) body_.linear_vel -= n * vn;
            }
        }
    }

    // ---- Chassis-corner contact springs ------------------------------------
    // Each of the 8 chassis corners that penetrates terrain applies its own
    // upward spring/damper plus a Coulomb friction force, *at that corner*.
    // Forces applied off-centre naturally generate torque, so a chassis
    // balanced on its rear face is unstable: any small lean makes one corner
    // penetrate more than the others, asymmetric support tips it over, and
    // friction at the moving contact dissipates the slide. This replaces the
    // earlier rigid hard-floor clamp (which lifted the centre of mass and so
    // never produced a toppling moment) and the bespoke linear/angular drag
    // code (subsumed by per-corner friction).
    {
        // Sample the eight corners of the visual mesh's body-local AABB,
        // not the hand-tuned chassis box: the visual extends well below
        // the chassis on truck-like models (front bumper, frame rails),
        // and we want what the player sees to be what the ground stops.
        const glm::vec3& vmin = visual_aabb_min_;
        const glm::vec3& vmax = visual_aabb_max_;
        const glm::vec3 N{0.f, 1.f, 0.f};
        constexpr float K_NORMAL    = 400000.f;  // N/m
        constexpr float C_NORMAL    =  18000.f;  // N·s/m
        constexpr float V_DAMP_CAP  =      6.f;  // m/s — clamp damper input
        constexpr float CHASSIS_MU  =     0.9f;

        // Wheel suspension already supports body weight when wheels are
        // grounded. If we let chassis-corner springs fire at full strength on
        // top of that, narrow features (a V-shaped valley, where two opposite
        // chassis corners penetrate the V walls while the wheels are happily
        // on the slopes) can launch the car. Cap the corner force tightly
        // while any wheel is grounded; the cap relaxes the moment the car is
        // genuinely unsupported (mid-air, tipped over, etc.).
        bool any_wheel_grounded = false;
        for (const Wheel& w : wheels_) if (w.grounded) { any_wheel_grounded = true; break; }
        const float fn_per_corn = any_wheel_grounded ? 4000.f : 60000.f;

        for (int sx = 0; sx <= 1; ++sx)
        for (int sy = 0; sy <= 1; ++sy)
        for (int sz = 0; sz <= 1; ++sz) {
            float cx = (sx == 0) ? vmin.x : vmax.x;
            float cy = (sy == 0) ? vmin.y : vmax.y;
            float cz = (sz == 0) ? vmin.z : vmax.z;
            glm::vec3 c_world = body_.to_world_point({cx, cy, cz});
            float ground = Heightmap::sample(c_world.x, c_world.z);
            float pen    = ground - c_world.y;
            if (pen <= 0.f) continue;

            glm::vec3 v_corner = body_.point_velocity(c_world);
            float     v_normal = glm::dot(v_corner, N);

            // Spring + damper. Damper only resists incoming motion (don't
            // pull the corner back down once it lifts off). The damper
            // velocity is capped so a high-speed impact doesn't fire a
            // single-substep impulse big enough to launch the car.
            float v_n_in = std::min(std::max(0.f, -v_normal), V_DAMP_CAP);
            float fn     = pen * K_NORMAL + v_n_in * C_NORMAL;
            fn           = std::min(fn, fn_per_corn);
            body_.apply_force_at(N * fn, c_world);

            // Coulomb friction at the contact point: opposes tangential
            // velocity, capped by μ * normal force. Capped a second time so
            // we can't overshoot and reverse the tangential motion within a
            // single substep.
            glm::vec3 v_tan     = v_corner - N * v_normal;
            float     v_tan_mag = glm::length(v_tan);
            if (v_tan_mag > 1e-4f) {
                float fric_max  = CHASSIS_MU * fn;
                float fric_stop = v_tan_mag * (body_.mass / 8.f)
                                  / std::max(dt, 1e-5f);
                float fric_mag  = std::min(fric_max, fric_stop);
                body_.apply_force_at(-(v_tan / v_tan_mag) * fric_mag, c_world);
            }
        }

        // Position-correction pass: the per-corner spring above caps grounded
        // force at fn_per_corn (4 kN), which is intentionally low so a V-shape
        // valley can't launch the car — but it's also too weak to stop a
        // hard-cornering chassis from sinking its outside corner below the
        // terrain. Find the deepest penetrating corner after this substep
        // and translate the body straight up so that corner sits flush on
        // the ground. Translation only: roll angle is preserved so the
        // corner stays in contact across frames while the cornering force
        // continues to push it down. (Wheels above the corrected position
        // simply lose a bit of suspension compression for one substep,
        // which the next raycast resolves cleanly.)
        float deepest_pen = 0.f;
        for (int sx = 0; sx <= 1; ++sx)
        for (int sy = 0; sy <= 1; ++sy)
        for (int sz = 0; sz <= 1; ++sz) {
            float cx = (sx == 0) ? vmin.x : vmax.x;
            float cy = (sy == 0) ? vmin.y : vmax.y;
            float cz = (sz == 0) ? vmin.z : vmax.z;
            glm::vec3 c_world = body_.to_world_point({cx, cy, cz});
            float ground = Heightmap::sample(c_world.x, c_world.z);
            float pen = ground - c_world.y;
            if (pen > deepest_pen) deepest_pen = pen;
        }
        if (deepest_pen > 0.f) {
            body_.position.y += deepest_pen;
            // Cancel any remaining downward CoM velocity — a corner sitting
            // on the ground shouldn't keep accelerating into it. We don't
            // touch upward velocity (a hard impact still bounces).
            if (body_.linear_vel.y < 0.f) body_.linear_vel.y = 0.f;
        }
    }
}

} // namespace pengine
