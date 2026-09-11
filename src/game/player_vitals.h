#pragma once

#include <algorithm>
#include <cmath>

#include "city/body_damage.h"

namespace apricot {

// The player's half of city/body_damage.h: the same hundred points the crowd
// and the officers carry, plus the one thing they do not need — a way back.
//
// It is a separate little state machine rather than a bare float on App
// because the float on App was the bug. Health hit zero inside
// check_police_shots(), and that function respawned the player on the spot:
// full health, beside the car, still holding the pistol, while a WASTED banner
// played over a world the player was already driving around in. There was no
// moment of being dead, so there was nowhere for a second cause of death to
// hook in, and nothing to stop two bullets in the same step killing the player
// twice.
//
// Everything here is in SECONDS OF SIM TIME handed in as dt, like the rest of
// the simulation. No clock is read below App::run().
inline constexpr float kPlayerDeathSeconds = 3.0f;

struct PlayerVitals {
    float health = kBodyHealth;
    bool dead = false;
    // Counts UP from the moment of death, so a consumer can fade a banner on
    // it without knowing the total. Meaningless while alive.
    float dead_seconds = 0.0f;

    bool alive() const { return !dead; }

    float fraction() const {
        return std::clamp(health / kBodyHealth, 0.0f, 1.0f);
    }

    // Spend health. Returns true ONLY on the blow that killed, so the caller
    // can start the death once — the same one-shot contract every other damage
    // consumer in the tree uses. A blow landing on somebody already dead does
    // nothing and reports nothing.
    bool take_damage(float amount) {
        if (dead) return false;
        if (!apply_body_damage(health, amount)) return false;
        dead = true;
        dead_seconds = 0.0f;
        return true;
    }

    // Kill outright, whatever is left. Returns true only on the transition,
    // for the same reason.
    bool kill() {
        if (dead) return false;
        health = 0.0f;
        dead = true;
        dead_seconds = 0.0f;
        return true;
    }

    // One step of being dead. Returns true on the single step the respawn is
    // due; the caller does the respawning and then calls revive(). Advancing a
    // living player is a no-op, so this is safe to call unconditionally.
    bool step(float dt) {
        if (!dead || !std::isfinite(dt) || dt <= 0.0f) return false;
        const float before = dead_seconds;
        dead_seconds += dt;
        return before < kPlayerDeathSeconds && dead_seconds >= kPlayerDeathSeconds;
    }

    void revive() {
        health = kBodyHealth;
        dead = false;
        dead_seconds = 0.0f;
    }
};

}  // namespace apricot
