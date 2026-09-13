#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/rng.h"
#include "game/weapon.h"

namespace apricot {

// The molotov: a bottle of fuel with a lit rag, thrown, and whatever it lands
// on catches (game/fire.h).
//
// THIS IS NOT A SECOND PISTOL and the differences are the reason it is its own
// header rather than another branch inside WeaponUseState. A pistol is
// hitscan, instantaneous, and its whole state is a magazine; a molotov leaves
// the hand, flies for a second and a half under gravity, and the thing the
// player aims is an ARC rather than a line. Folding a projectile into a
// hitscan state machine would put two unrelated clocks in one struct and make
// every future weapon a third branch in both.
//
// What it DOES copy from WeaponUseState is that struct's input contract, to
// the letter, because the ways a weapon leaks input were already paid for
// there: presses are consumed even while holstered or blocked, so closing a
// menu cannot throw a bottle; a throw needs the weapon to have been ready on
// the PREVIOUS tick, so the frame the molotov finishes coming out is not also
// the frame it leaves; and losing focus cancels rather than latches.
//
// NO CLOCK, NO RNG STREAM. Timing is the caller's `dt`; the tumble is keyed on
// the throw's own id, so a replayed tape throws the same bottle the same way.

inline constexpr float kMolotovGravityMps2 = 16.0f;

struct MolotovUseInput {
    bool available = false;
    bool aim = false;
    bool throw_pressed = false;
};

struct MolotovUseState {
    // Five bottles. Enough to see the fire spread, set a second one going
    // beside it and still have one left; a molotov you can only throw once is
    // a cutscene, and an unlimited one stops the player ever looking for more.
    static constexpr int kInitialStock = 5;
    static constexpr float kDrawSeconds = 0.42f;   // slower than a pistol: it is a bottle
    static constexpr float kThrowInterval = 0.95f; // one arm, one bottle at a time
    // How long after the release the hand stays empty before the next bottle
    // appears in it. Purely presentation, but it is what makes the throw read
    // as a throw rather than as the bottle teleporting back.
    static constexpr float kRearmSeconds = 0.55f;

    int stock = kInitialStock;
    float aim_blend = 0.0f;
    float equip_blend = 0.0f;
    float throw_age = 60.0f;   // seconds since the last release
    float cooldown = 0.0f;

    bool armed() const { return stock > 0 && throw_age >= kRearmSeconds; }
    // 0 while the hand is empty after a throw, 1 once the next bottle is up.
    float rearm_progress() const {
        return std::clamp(throw_age / kRearmSeconds, 0.0f, 1.0f);
    }

    // Returns true ONLY on the tick the bottle leaves the hand, which is also
    // the tick the stock is spent. Same one-shot contract as everything else
    // that costs the player something.
    bool step(WeaponId selected, const MolotovUseInput& input, float dt) {
        const bool throw_edge = input.throw_pressed && !throw_was_pressed_;
        throw_was_pressed_ = input.throw_pressed;
        const bool active = input.available && selected == WeaponId::Molotov;
        if (!active) {
            // A paused menu owes no simulation time but still revokes the
            // weapon. Cancel here rather than below the dt guard, or holding
            // the throw button through a pause throws on the way out.
            available_last_ = false;
            equip_blend = aim_blend = 0.0f;
            cooldown = 0.0f;
            throw_age = 60.0f;
        }
        if (!std::isfinite(dt) || dt <= 0.0f) return false;

        const bool changed = selected != equipped_;
        const bool ready_before = active && available_last_ && !changed &&
                                  equip_blend >= 1.0f;
        if (changed || !active || !available_last_) {
            if (changed || (active && !available_last_)) equip_blend = 0.0f;
            throw_age = 60.0f;
        }
        equipped_ = selected;
        available_last_ = active;
        cooldown = std::max(0.0f, cooldown - dt);
        throw_age = std::min(60.0f, throw_age + dt);
        equip_blend = approach(equip_blend, active ? 1.0f : 0.0f, dt / kDrawSeconds);

        const bool thrown = ready_before && throw_edge && cooldown <= 0.0f &&
                            stock > 0 && throw_age >= kRearmSeconds;
        if (thrown) {
            --stock;
            cooldown = kThrowInterval;
            throw_age = 0.0f;
        }
        const bool aiming = active && input.aim;
        aim_blend = approach(aim_blend, aiming ? equip_blend : 0.0f,
                             dt / (aiming ? 0.18f : 0.14f));
        return thrown;
    }

private:
    static float approach(float value, float target, float rate) {
        if (!std::isfinite(rate) || rate <= 0.0f) return value;
        return value < target ? std::min(target, value + rate)
                              : std::max(target, value - rate);
    }
    WeaponId equipped_ = WeaponId::Unarmed;
    bool available_last_ = false;
    bool throw_was_pressed_ = false;
};

// Launch velocity for a throw down `yaw`/`pitch`, in world metres per second.
//
// The LOFT is the part worth defending. Thrown along the exact camera ray, a
// bottle aimed level goes flat and lands at the player's own feet, and there is
// no way for them to tell the game they meant "over there" rather than "here".
// A fixed upward bias, with the aim contributing the rest, gives a throw that
// clears a car bonnet from the hip and still goes where it is pointed when the
// player takes the time to aim up.
inline glm::vec3 molotov_throw_velocity(float yaw, float pitch, float aim_blend) {
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return glm::vec3{0.0f};
    const float blend = std::clamp(std::isfinite(aim_blend) ? aim_blend : 0.0f,
                                   0.0f, 1.0f);
    // Hip throws are lofted hard and land about seven metres out; an aimed
    // throw flattens a little and adds a lot of speed, and lands about eleven.
    //
    // BOTH HALVES MOVE, and getting that wrong is easy to do and hard to see.
    // Range is v^2 sin(2θ) / g, so flattening the arc on its own SHORTENS the
    // throw — the arc loses more flight time than the speed buys back. The
    // first version of these numbers dropped the loft from 20° to 9° and
    // raised the speed by a third, and taking aim made the bottle land a metre
    // nearer. tests/molotov_tests.cpp measures the ground each throw covers
    // rather than trusting either number on its own.
    const float loft = glm::radians(20.0f) * (1.0f - 0.35f * blend);
    const float launch = std::clamp(pitch + loft, glm::radians(-35.0f),
                                    glm::radians(62.0f));
    const float speed = 13.5f + 6.5f * blend;
    const float horizontal = std::cos(launch) * speed;
    return glm::vec3{-std::sin(yaw) * horizontal, std::sin(launch) * speed,
                     -std::cos(yaw) * horizontal};
}

// One bottle in the air. Ballistic only: no drag, no wind, no bounce. A
// molotov that bounced would need a fuel state ("lit but not broken") that
// nothing else in the game has, and the first thing it breaks on is what the
// player was aiming at anyway.
struct MolotovProjectile {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 spin_axis{1.0f, 0.0f, 0.0f};
    float spin_turns = 0.0f;     // accumulated revolutions, for the tumble
    float age = 0.0f;
    uint64_t throw_id = 0;
    bool live = false;
};

// A handful in the air at once, because the throw interval already limits how
// many the player can have out and a pool that can overflow at four is a pool
// whose overflow path never runs.
class MolotovProjectiles {
public:
    static constexpr std::size_t kCapacity = 4;
    // Nothing flies forever. A bottle thrown off a rooftop that never finds
    // ground has to end somewhere, and a projectile with no expiry is a
    // fire waiting to start under a player who has walked half a mile.
    static constexpr float kMaxFlightSeconds = 6.0f;

    // Returns false when every slot is busy, which the caller should treat as
    // the throw not happening rather than as a silent loss of a bottle.
    bool launch(glm::vec3 position, glm::vec3 velocity, uint64_t throw_id) {
        if (!finite(position) || !finite(velocity)) return false;
        for (auto& shot : shots_) {
            if (shot.live) continue;
            shot.position = position;
            shot.velocity = velocity;
            shot.age = 0.0f;
            shot.spin_turns = 0.0f;
            shot.throw_id = throw_id;
            // A bottle leaves the hand end over end, around an axis across the
            // throw. Keyed on the throw so a replay tumbles the same way.
            const uint64_t roll = hash_coord3(throw_id ^ 0xB07715E5ull, 0, 0, 0);
            const float lean = (static_cast<float>(roll >> 40) / 16777215.0f - 0.5f) * 0.7f;
            const glm::vec3 flat{velocity.x, 0.0f, velocity.z};
            const float reach = glm::length(flat);
            const glm::vec3 across = reach > 1e-3f
                ? glm::vec3{-flat.z / reach, 0.0f, flat.x / reach}
                : glm::vec3{1.0f, 0.0f, 0.0f};
            shot.spin_axis = glm::normalize(across + glm::vec3{0.0f, lean, 0.0f});
            shot.live = true;
            return true;
        }
        return false;
    }

    void step(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f) return;
        for (auto& shot : shots_) {
            if (!shot.live) continue;
            shot.age += dt;
            if (shot.age >= kMaxFlightSeconds) { shot.live = false; continue; }
            shot.velocity.y -= kMolotovGravityMps2 * dt;
            shot.position += shot.velocity * dt;
            // Tumble rate follows speed, so a lobbed bottle turns lazily and a
            // hard throw spins. 0.55 turns per metre of travel reads as glass
            // going end over end without strobing at frame rate.
            shot.spin_turns += glm::length(shot.velocity) * dt * 0.55f;
        }
    }

    // Put one out. The caller does the world query and decides where the
    // bottle stopped, because collision belongs to whoever owns the collider.
    void extinguish(std::size_t index) {
        if (index < kCapacity) shots_[index].live = false;
    }

    void clear() { shots_ = {}; }
    const std::array<MolotovProjectile, kCapacity>& shots() const { return shots_; }
    std::size_t live_count() const {
        return static_cast<std::size_t>(std::count_if(shots_.begin(), shots_.end(),
            [](const MolotovProjectile& s) { return s.live; }));
    }

private:
    static bool finite(glm::vec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    std::array<MolotovProjectile, kCapacity> shots_{};
};

}  // namespace apricot
