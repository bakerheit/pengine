#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "core/input_frame.h"
#include "physics/terrain_collider.h"

namespace apricot {

// A forgiving fixed-step arcade helicopter. Deliberately separate from both
// cars and game/aircraft.h, because the thing that makes a helicopter a
// helicopter is exactly what the fixed-wing model cannot express: its thrust
// points along its OWN up axis, so tilting the airframe is how it translates,
// and it has no stall speed to fall out of. A fixed-wing model with a hover
// bolted on is a plane that cheats; this is the other shape.
//
// Origin is the bottom of the landing gear; authored nose is local +Z, which
// is the same frame game/aircraft.h uses and the frame the source mesh is
// already in.
//
// Controls, and why they are mapped this way:
//   throttle / brake     cyclic fore/aft -- nose down to fly forward. W is
//                        "forward" in every game a player has ever touched,
//                        and on a helicopter forward IS nose-down.
//   ShiftUp / ShiftDown  collective -- climb / descend.
//   steer                pedals -- yaw on the spot, which is the manoeuvre a
//                        car and a plane both refuse to do.
// Bank is not a control. It is rolled INTO the turn as a consequence of yaw,
// which is what stops a pedal turn from reading like a rotating statue.
struct HelicopterState {
    glm::vec3 position{};
    glm::vec3 velocity{};
    float yaw = 0, pitch = 0, roll = 0;
    float collective = 0;    // commanded thrust, in units of hover weight
    float rotor = 0;         // spool, 0..1. Thrust authority scales with it.
    float rotor_angle = 0;   // radians; the visual disc's angle, derived here
    bool grounded = true;
    bool crashed = false;
    // Wreck state. A crashed helicopter is not a finished helicopter: it falls,
    // and these are what carry it down. `tumble` is rad/s on pitch, yaw and
    // roll; `impacts` bumps once per collision so the host can fire an effect
    // off the edge without keeping its own copy of last frame's crash flag.
    glm::vec3 tumble{};
    uint32_t impacts = 0;
};

// Tuning. Stated as named constants rather than inline magic because the
// takeoff/hover/landing tests below assert against these same numbers.
inline constexpr float kHeliGravity = 9.81f;
inline constexpr float kHeliMaxTilt = 0.5585f;      // 32 degrees of cyclic
inline constexpr float kHeliMaxThrust = 2.6f;       // g, at full collective
inline constexpr float kHeliDrag = 0.0015f;         // caps cruise near 64 m/s
inline constexpr float kHeliYawRate = 1.2f;         // rad/s at full pedal
inline constexpr float kHeliSpoolRate = 0.45f;      // 0..1 in ~2.2 s
inline constexpr float kHeliRotorSpeed = 31.4f;     // rad/s -- ~300 rpm
inline constexpr float kHeliLiftoffRotor = 0.55f;   // no lift below this spool
// A wreck keeps most of the speed it hit with, minus whatever went into the
// wall, and it is draggier than a flying machine but nothing like a brick.
inline constexpr float kHeliWreckDrag = 0.0009f;
inline constexpr float kHeliWreckKeep = 0.55f;      // momentum surviving a hit
inline constexpr float kHeliWreckRestTilt = 0.25f;  // ~14 deg, how it lies
// Collective authority and vertical damping are one decision, not two. Rate
// settles at authority*g/damping and the coast after letting go is that again
// over damping, so the pair is chosen together: ~9.8 m/s of climb, and ~6.5 m
// of float before it holds the altitude it stopped at. Raising one alone
// either makes a machine that drifts up for twenty metres after the player has
// stopped asking, or one that cannot climb.
inline constexpr float kHeliCollectiveAuthority = 1.5f;
inline constexpr float kHeliVerticalDamping = 1.5f;
// DOWN is deliberately weaker than up, and this is the single decision that
// makes the machine landable. Neutral collective auto-trims to hold altitude,
// so the only way down is to hold the descend button -- and if holding it all
// the way in exceeded the gear's limit, then every approach would end in a
// wreck unless the player feathered a DIGITAL button. At 0.85 the terminal
// descent is ~5.6 m/s, inside the 7 m/s limit below, so holding it down lands
// you firmly and safely. What still ends badly is arriving with the speed
// on: the horizontal limit is what a landing is actually a skill about.
inline constexpr float kHeliDescentAuthority = 0.85f;

// Hull and rotor-disc rim. The disc is 14 m across and is the first thing to
// touch anything, which is the whole hazard of flying one down a street.
inline constexpr glm::vec3 kHeliProbes[] = {
    {0,2.4f,6.6f},{0,2.4f,1.5f},{0,1.8f,-3.5f},{0,3.4f,-7.2f},
    {-2.8f,2.2f,1.0f},{2.8f,2.2f,1.0f},
    {-7.0f,4.6f,2.2f},{7.0f,4.6f,2.2f},{0,4.6f,9.2f},{0,4.6f,-4.8f}};

inline glm::quat helicopter_rotation(const HelicopterState& s) {
    return glm::angleAxis(s.yaw, glm::vec3{0,1,0}) *
           glm::angleAxis(-s.pitch, glm::vec3{1,0,0}) *
           glm::angleAxis(s.roll, glm::vec3{0,0,1});
}
inline glm::vec3 helicopter_forward(const HelicopterState& s) {
    return helicopter_rotation(s) * glm::vec3{0,0,1};
}
inline glm::vec3 helicopter_up(const HelicopterState& s) {
    return helicopter_rotation(s) * glm::vec3{0,1,0};
}
inline glm::vec3 helicopter_point(const HelicopterState& s, glm::vec3 local) {
    return s.position + helicopter_rotation(s) * local;
}
inline bool helicopter_can_exit(const HelicopterState& s) {
    return s.grounded && glm::length(s.velocity) < 1.0f;
}

// Ground-level access at the port cockpit door. The zone is measured off the
// mesh, not guessed: the stub wings end at z 2.41 and the fuselage is only
// 1.23 m half-width from z 3 forward, so a band at z 2.6..5.4 starting 1.2 m
// out is beside the cockpit and clear of the wing you would otherwise walk
// through. Never board through the starboard side, the nose or the tail boom.
inline bool helicopter_in_boarding_range(const HelicopterState& s,
                                         glm::vec3 person, float ground) {
    if (!helicopter_can_exit(s) || s.crashed ||
        !std::isfinite(person.x + person.y + person.z + ground)) return false;
    const auto p = glm::inverse(helicopter_rotation(s)) * (person - s.position);
    return p.x < -1.2f && p.x > -3.4f && std::fabs(p.z - 4.0f) < 1.4f &&
           person.y >= ground - .45f && person.y <= ground + .65f;
}

// Sweep every probe from one pose to the next. Returns the probe's local
// offset and the surface it found, or false. The host layer disables this
// helicopter's own colliders around the query.
inline bool helicopter_sweep(const HelicopterState& from, const HelicopterState& to,
                             const TerrainCollider& ground, glm::vec3& arm_out,
                             TerrainCollider::GroundHit& hit_out) {
    for (const auto local : kHeliProbes) {
        const glm::vec3 start = helicopter_point(from, local);
        const glm::vec3 travel = helicopter_point(to, local) - start;
        const float distance = glm::length(travel);
        if (distance < .00001f) continue;
        const auto hit = ground.raycast(start, travel / distance, distance);
        if (hit.hit && hit.distance <= distance) {
            arm_out = local; hit_out = hit; return true;
        }
    }
    return false;
}

// What an impact does to the machine. Momentum into the surface is spent on
// the surface; what is left carries the wreck away from it, and the torque
// about the part that actually hit is what sets it tumbling -- a rotor tip
// into a wall rolls it, a nose-first hit pitches it. Deriving the tumble from
// the collision rather than from a random number keeps it deterministic AND
// makes it look like the thing that just happened.
inline void helicopter_wreck(HelicopterState& s, const HelicopterState& previous,
                             glm::vec3 arm, glm::vec3 normal) {
    const glm::vec3 v = previous.velocity;
    const float speed = glm::length(v);
    const float into = std::min(0.0f, glm::dot(v, normal));
    s.velocity = (v - normal * into * 1.35f) * kHeliWreckKeep;
    const glm::vec3 local_force =
        glm::inverse(helicopter_rotation(previous)) * (-v);
    const glm::vec3 torque = glm::cross(arm, local_force);
    const float magnitude = glm::length(torque);
    const float rate = std::clamp(speed / 9.0f, .9f, 3.0f);
    s.tumble = magnitude > 1e-4f ? torque / magnitude * rate
                                 : glm::vec3{1.2f, 0, .8f} * rate;
    s.crashed = true;
    s.collective = 0;
    ++s.impacts;
}

// Put a falling wreck on the ground and leave it lying there. It keeps a bit
// of the attitude it arrived with, because a helicopter that has just fallen
// out of the sky does not come to rest perfectly level -- but only a bit, or
// the skids float and the tail buries itself.
inline void helicopter_settle(HelicopterState& s, float floor) {
    s.position.y = floor;
    s.velocity = {};
    s.tumble = {};
    s.grounded = true;
    s.pitch = std::clamp(std::remainder(s.pitch, glm::two_pi<float>()),
                         -kHeliWreckRestTilt, kHeliWreckRestTilt);
    s.roll = std::clamp(std::remainder(s.roll, glm::two_pi<float>()),
                        -kHeliWreckRestTilt, kHeliWreckRestTilt);
    ++s.impacts;
}

inline HelicopterState step_helicopter(const HelicopterState& previous,
                                       const InputFrame& input,
                                       const TerrainCollider& ground, float dt) {
    HelicopterState s = previous;
    if (!(dt > 0) || !std::isfinite(dt)) return s;
    dt = std::min(dt, 1.0f / 30.0f);
    // A WRECK IS STILL SUBJECT TO GRAVITY. This branch used to return here,
    // which froze the machine at the point of impact: fly into a tower block
    // and it hangs in the air with the blades winding down, which is the one
    // outcome nobody expects. It takes no input and makes no thrust, but it
    // falls, it tumbles, and it arrives.
    if (s.crashed) {
        s.rotor = std::max(0.0f, s.rotor - kHeliSpoolRate * dt);
        s.rotor_angle = std::remainder(
            s.rotor_angle + s.rotor * kHeliRotorSpeed * dt, glm::two_pi<float>());
        if (s.grounded) return s;
        s.velocity.y -= kHeliGravity * dt;
        s.velocity -= s.velocity * glm::length(s.velocity) * kHeliWreckDrag * dt;
        s.pitch = std::remainder(s.pitch + s.tumble.x * dt, glm::two_pi<float>());
        s.yaw = std::remainder(s.yaw + s.tumble.y * dt, glm::two_pi<float>());
        s.roll = std::remainder(s.roll + s.tumble.z * dt, glm::two_pi<float>());
        s.position += s.velocity * dt;
        const auto support = ground.probe_down(s.position + glm::vec3{0,5,0}, 10000);
        const float floor = support.hit ? support.point.y
                                        : ground.height(s.position.x, s.position.z);
        if (s.position.y <= floor) { helicopter_settle(s, floor); return s; }
        // Still in the air, and it can clip a wall on the way down. GRAZE off
        // it and keep falling -- a wreck that stops dead against a wall is the
        // same bug in a smaller costume, just harder to see. Only the
        // horizontal move is taken back, so the thing always keeps descending
        // and cannot wedge itself against a face forever; what it does instead
        // is slide down the building, which is what it should look like.
        // Landing ON something is probe_down's job above, not this one's.
        glm::vec3 arm; TerrainCollider::GroundHit hit;
        if (helicopter_sweep(previous, s, ground, arm, hit)) {
            s.position.x = previous.position.x;
            s.position.z = previous.position.z;
            const float into = std::min(0.0f, glm::dot(s.velocity, hit.normal));
            glm::vec3 grazed = s.velocity - hit.normal * into * 1.2f;
            // Scrub sideways speed on the wall, but NEVER the fall. Damping
            // all three axes here reaches an equilibrium against gravity at a
            // quarter of a metre per second, and a wreck sliding down a tower
            // for two minutes is not visibly different from one that is stuck.
            grazed.x *= .7f;
            grazed.z *= .7f;
            grazed.y = std::min(s.velocity.y, 0.0f);
            s.velocity = grazed;
        }
        return s;
    }
    s.rotor = std::min(1.0f, s.rotor + kHeliSpoolRate * dt);
    s.rotor_angle = std::remainder(
        s.rotor_angle + s.rotor * kHeliRotorSpeed * dt, glm::two_pi<float>());

    const float cyclic = std::clamp(input.throttle, 0.0f, 1.0f) -
                         std::clamp(input.brake, 0.0f, 1.0f);
    const float lift_input = (is_held(input, kBtnShiftUp) ? 1.0f : 0.0f) -
                             (is_held(input, kBtnShiftDown) ? 1.0f : 0.0f);
    const float pedal = std::clamp(input.steer, -1.0f, 1.0f);
    const bool powered = s.rotor >= kHeliLiftoffRotor;

    // Attitude. On the ground the skids hold it level: a helicopter sitting on
    // its gear does not tilt because the stick moved, it just does not lift.
    const float pitch_target = powered ? -cyclic * kHeliMaxTilt : 0.0f;
    s.pitch += (pitch_target - s.pitch) * std::min(1.0f, dt * 2.2f);
    const float speed = glm::length(glm::vec2{s.velocity.x, s.velocity.z});
    const float bank_target = s.grounded ? 0.0f
        : -pedal * .38f * std::clamp(speed / 22.0f, .25f, 1.0f);
    s.roll += (bank_target - s.roll) * std::min(1.0f, dt * 2.0f);
    if (powered) s.yaw -= pedal * kHeliYawRate * (s.grounded ? .35f : 1.0f) * dt;
    s.yaw = std::remainder(s.yaw, glm::two_pi<float>());
    if (s.grounded) { s.pitch *= std::max(0.0f, 1.0f - dt * 6.0f); s.roll = 0; }

    // Collective. Neutral stick auto-trims to hold altitude at the CURRENT
    // tilt: thrust along a tilted up axis only lifts by its vertical share, so
    // the trim is 1/up.y, in units of hover weight. This is the forgiving
    // part, and it is forgiving on purpose -- across the whole 32 degrees of
    // cyclic the trim costs at most 1.18 g, well inside the rotor's 2.6, so
    // a player learning the machine can point it where they like and not sink
    // into a hangar roof for it. The .55 floor is only a guard for the attitudes a
    // collision can leave behind; normal flight never reaches it.
    const glm::vec3 up = helicopter_up(s);
    const float authority = lift_input > 0 ? kHeliCollectiveAuthority
                                          : kHeliDescentAuthority;
    s.collective = std::clamp(1.0f / std::max(up.y, .55f) + lift_input * authority,
                              0.0f, kHeliMaxThrust);
    const float thrust = powered ? s.collective * kHeliGravity *
        std::clamp((s.rotor - kHeliLiftoffRotor) / (1.0f - kHeliLiftoffRotor),
                   0.0f, 1.0f) : 0.0f;

    glm::vec3 acceleration = up * thrust - glm::vec3{0, kHeliGravity, 0};
    acceleration -= s.velocity * glm::length(s.velocity) * kHeliDrag;
    // Vertical damping is separate and far stronger than the cruise drag,
    // because they are sizing different things. The quadratic term is tuned so
    // cruise tops out near 64 m/s; left to cap the climb as well it would let
    // full collective run away to a 55 m/s ascent. This linear term puts climb
    // and descent near 10 m/s, which is a helicopter, and it is what makes the
    // touchdown limit below a skill rather than a coin toss: terminal descent
    // is past the gear's limit, so every landing needs the collective eased
    // off on the way in.
    acceleration.y -= s.velocity.y * kHeliVerticalDamping;
    // Bleed sideways drift harder than forward speed. Without this the machine
    // skates like an air-hockey puck and never settles into a hover.
    const glm::vec3 forward = helicopter_forward(s);
    const glm::vec3 flat = glm::normalize(
        glm::vec3{forward.x, 0, forward.z} + glm::vec3{1e-6f, 0, 0});
    const glm::vec3 side{-flat.z, 0, flat.x};
    acceleration -= side * glm::dot(s.velocity, side) * .55f;

    // Leaving the ground is an explicit ask, not a float comparison. Trimmed
    // thrust on a level pad is exactly one weight, so `thrust > gravity` is a
    // coin toss decided by the last bit of up.y -- and the coin landing the
    // wrong way is a helicopter that drifts off its stand on its own.
    if (s.grounded && !(lift_input > 0 && thrust > kHeliGravity)) {
        s.velocity = {};
    } else {
        s.grounded = false;
        s.velocity += acceleration * dt;
    }
    s.position += s.velocity * dt;

    const auto support = ground.probe_down(s.position + glm::vec3{0,5,0}, 10000);
    const float floor = support.hit ? support.point.y
                                    : ground.height(s.position.x, s.position.z);
    // A SURFACE UNDER THE MACHINE IS NOT ALWAYS A PLACE TO LAND. probe_down()
    // reports the top of whatever box is beneath the origin, so flying at the
    // side of a five-metre blast wall finds that wall's ROOF -- above the
    // machine, not below it -- and the block below would happily snap a
    // fifty-knot impact up onto it and call it parked. It did, too: the first
    // in-game run of this hit a revetment and quietly landed on top of it.
    // A floor that is well ABOVE where the machine just was, reached with the
    // speed still on, is a wall. Leave it to the sweep further down.
    const float step_up = floor - previous.position.y;
    const bool flew_into_it = step_up > .6f &&
        glm::length(glm::vec2{s.velocity.x, s.velocity.z}) > 6.0f;
    if (s.position.y <= floor && !flew_into_it) {
        // A helicopter lands on its gear, and the gear is what decides whether
        // that was a landing or an accident: descent rate first, attitude
        // second. Both limits are generous -- this is arcade, and a player
        // setting one down in a revetment should get away with a thump.
        const bool hard = !s.grounded &&
            (s.velocity.y < -7.0f || std::fabs(s.roll) > glm::radians(28.0f) ||
             std::fabs(s.pitch) > glm::radians(30.0f) ||
             glm::length(glm::vec2{s.velocity.x, s.velocity.z}) > 14.0f);
        s.position.y = floor;
        s.velocity = {};
        s.grounded = true;
        s.pitch = 0; s.roll = 0;
        const bool wrecked = hard || floor < .2f;  // .2 m: the sea is no helipad
        // Count the arrival only when it was an accident. `impacts` is what the
        // host fires an explosion off, and a tidy landing is not an explosion.
        if (wrecked && !s.crashed) ++s.impacts;
        s.crashed = s.crashed || wrecked;
    }

    // Anything the hull or the disc swept into ends the flight. Back the
    // machine out of whatever it found, then let helicopter_wreck() decide how
    // it leaves -- which is with momentum and a tumble, not at a dead stop.
    {
        glm::vec3 arm; TerrainCollider::GroundHit hit;
        if (helicopter_sweep(previous, s, ground, arm, hit)) {
            s.position = previous.position;
            s.yaw = previous.yaw; s.pitch = previous.pitch; s.roll = previous.roll;
            helicopter_wreck(s, previous, arm, hit.normal);
            // Hitting the ground itself is already an arrival, not a fall.
            if (s.grounded) helicopter_settle(s, s.position.y);
        } else if (flew_into_it) {
            // The obstacle is real but sits below every probe -- a low wall, a
            // bank of earth rising into the skids. Still a wall at speed.
            const glm::vec2 track{s.velocity.x, s.velocity.z};
            const float along = glm::length(track);
            const glm::vec3 face = along > 1e-4f
                ? glm::vec3{-track.x / along, 0, -track.y / along}
                : glm::vec3{0, 1, 0};
            s.position = previous.position;
            s.yaw = previous.yaw; s.pitch = previous.pitch; s.roll = previous.roll;
            helicopter_wreck(s, previous, glm::vec3{0, 1.8f, 4.f}, face);
        }
    }
    // A hard landing is a crash that is already on the ground: no fall left.
    if (s.crashed && s.grounded) { s.velocity = {}; s.tumble = {}; s.collective = 0; }
    return s;
}

} // namespace apricot
