#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include "audio/mixer.h"
#include "audio/synth.h"
#include "core/rng.h"

namespace apricot {

// The player's own footfalls.
//
// CADENCE COMES FROM DISTANCE, NOT FROM TIME, and that is the one decision in
// here worth defending. A timer-driven footstep has to be re-tuned for walking
// and for sprinting, drifts against the animation whenever the character is
// slowed (aiming, drunk, a tornado pushing back), and gives a player who walks
// into a wall a full-speed march on the spot. PlayerCharacterState already
// accumulates distance_walked_m over ground contact only, so pacing off it is
// correct in all four cases for free, needs no dt, and cannot depend on the
// frame rate — there is no clock anywhere below this line.
//
// Recorded only. A family with no loaded takes is silent; nothing here
// synthesises a substitute, because a generated footstep is a click with a
// noise tail and the ear knows.

// What the character is doing this update. Plain data: the app fills it from
// PlayerCharacterState and the ground the collider already probed, so audio
// needs neither the physics nor the game headers.
struct FootstepWalk {
    bool on_foot = false;
    bool grounded = true;
    bool sprinting = false;

    // Monotonic ground distance from PlayerCharacterState. A DECREASE is read
    // as a respawn, a car exit or a teleport and re-arms rather than firing a
    // burst of steps.
    float distance_walked_m = 0.0f;

    float speed_mps = 0.0f;
    FootstepSurface surface = FootstepSurface::Concrete;

    // Raw pack under the foot, from GroundHit::snow_depth_m. Muffles the step.
    float snow_depth_m = 0.0f;

    // Where the foot landed, used only to decorrelate the take rotation so
    // pacing the same twenty metres does not replay the same sequence.
    glm::vec2 ground{0.0f};
};

struct FootstepTuning {
    // One footfall per stride of ground covered. Both are set from real gait
    // rather than picked: a person moving at the character's 2.35 m/s walk
    // takes a ~0.92 m stride, which is 2.55 footfalls a second, and a runner at
    // its 6.25 m/s sprint takes ~1.62 m, which is 3.9. Sprinting lengthens the
    // stride as well as speeding it up, and that is what keeps a sprint from
    // sounding like a sewing machine. Shortening either is the single easiest
    // way to make walking sound wrong while every test still passes.
    float walk_stride_m = 0.92f;
    float sprint_stride_m = 1.62f;

    // How far ahead the next footfall sits while the character is standing
    // still. Short, so the first step after you start walking lands under the
    // foot instead of a full stride later.
    float arm_distance_m = 0.24f;
    float idle_speed_mps = 0.30f;

    // Reference for the speed taper. Matches CharacterTuning::walk_speed_mps;
    // a creep is quieter than a walk, a walk and a sprint are not scaled down.
    float walk_speed_mps = 2.35f;
    float quiet_floor = 0.40f;

    float walk_gain = 0.50f;
    float sprint_gain = 0.78f;
    // A landing is one footfall with both feet in it.
    float land_gain = 0.92f;

    float pitch_jitter = 0.07f;

    // Snow over the boot. Full depth is where the muffling stops deepening,
    // not where the snow stops.
    float snow_full_depth_m = 0.14f;
    float snow_gain_floor = 0.42f;
    float snow_cutoff_hz = 1400.0f;
    float dry_cutoff_hz = 9000.0f;
};

// How loud, how bright and at what pitch one footfall plays. Pure and
// allocation-free, like every other *_mix() in this module: `roll` is the
// caller's deterministic hash, so this function has no state of its own and a
// test can assert the whole curve.
struct FootstepMix {
    float gain = 0.0f;
    float pitch = 1.0f;
    float lp_cutoff_hz = 0.0f;
};

inline FootstepMix footstep_mix(const FootstepWalk& walk,
                               const FootstepTuning& tuning, bool landing,
                               uint64_t roll) {
    FootstepMix mix;
    const float base = landing ? tuning.land_gain
                     : (walk.sprinting ? tuning.sprint_gain : tuning.walk_gain);
    const float reference = std::max(tuning.walk_speed_mps, 0.01f);
    const float taper = landing
        ? 1.0f
        : std::clamp(walk.speed_mps / reference, tuning.quiet_floor, 1.0f);

    const float snow = std::clamp(
        walk.snow_depth_m / std::max(tuning.snow_full_depth_m, 0.001f),
        0.0f, 1.0f);
    mix.gain = base * taper *
               (1.0f + snow * (tuning.snow_gain_floor - 1.0f));

    // Dry ground still gets a corner rather than a bypass, so crossing onto
    // snow moves one number instead of switching the filter on. The takes are
    // 48 kHz recordings of a boot; 9 kHz takes nothing off them that matters.
    mix.lp_cutoff_hz = tuning.dry_cutoff_hz +
                       snow * (tuning.snow_cutoff_hz - tuning.dry_cutoff_hz);

    // 16 bits of the hash mapped to [-1, 1]. Deterministic, so the same walk
    // replays with the same footsteps.
    const float unit = static_cast<float>(roll & 0xFFFFu) / 65535.0f;
    mix.pitch = 1.0f + (unit * 2.0f - 1.0f) * std::max(tuning.pitch_jitter, 0.0f);
    return mix;
}

// Salt for the take rotation. A fixed constant, not a run seed: a per-run salt
// would buy a little variety and cost the ability to reproduce a reported
// "this footstep sounds wrong" from the same inputs.
inline constexpr uint64_t kFootstepSalt = 0x5F007573'7465'7073ull;

// Pick a loaded take from a family. Rotates over the takes that ACTUALLY
// loaded, never over the six slots, so a family the pack supplies four of has
// a four-deep rotation instead of a one-in-three chance of silence. `previous`
// is the last index this family played and is avoided, which is what stops a
// long walk from doubling a take audibly.
inline const PcmClip* footstep_take(const SfxBank& bank, FootstepSurface surface,
                                    uint64_t roll, int previous) {
    const std::size_t row = static_cast<std::size_t>(surface);
    if (row >= kFootstepSurfaceCount) return nullptr;
    std::array<int, kFootstepVariantCount> loaded{};
    std::size_t count = 0;
    for (std::size_t i = 0; i < kFootstepVariantCount; ++i) {
        if (!bank.footsteps[row][i].empty())
            loaded[count++] = static_cast<int>(i);
    }
    if (count == 0) return nullptr;
    std::size_t pick = static_cast<std::size_t>(roll >> 32) % count;
    if (count > 1 && loaded[pick] == previous) pick = (pick + 1) % count;
    return &bank.footsteps[row][static_cast<std::size_t>(loaded[pick])];
}

// Assemble one update's input from a character's own state and the ground the
// caller has already probed under its feet.
//
// This exists so the game and a headless test go through the SAME assembly.
// A test that rebuilds this by hand passes happily while the real caller feeds
// a stale surface or the wrong speed, which is the failure this module is most
// exposed to — everything else in here is a number a test can check directly.
// Plain scalars only, so audio still needs neither the physics nor the game
// headers to say what a footfall sounds like.
inline FootstepWalk footstep_walk(glm::vec3 position, glm::vec3 velocity,
                                  float distance_walked_m, bool grounded,
                                  bool sprinting, AudioSurface material,
                                  bool road, bool prop, float snow_depth_m) {
    FootstepWalk walk;
    walk.on_foot = true;
    walk.grounded = grounded;
    walk.sprinting = sprinting;
    walk.distance_walked_m = distance_walked_m;
    // Horizontal only: a fall is not a walk, and counting the vertical
    // component would make a footstep loudest at the moment the feet leave the
    // ground.
    walk.speed_mps = glm::length(glm::vec2{velocity.x, velocity.z});
    walk.surface = footstep_surface(material, road, prop);
    walk.snow_depth_m = snow_depth_m;
    walk.ground = {position.x, position.z};
    return walk;
}

class FootstepAudio {
public:
    // Call once per simulation step while the world is live. Plays at most one
    // footfall per call: no sim step covers a 1.62 m sprint stride, and one
    // voice per update keeps footsteps off the mixer's one-shot slot budget.
    void update(VoiceMixer& mixer, const SfxBank& bank,
                const FootstepWalk& walk,
                const FootstepTuning& tuning = {}) {
        if (!walk.on_foot) { reset(); return; }

        // First update on foot, or the counter went backwards because the
        // character was respawned or put down somewhere else.
        if (!armed_ || walk.distance_walked_m < distance_m_) {
            armed_ = true;
            grounded_ = walk.grounded;
            distance_m_ = walk.distance_walked_m;
            next_step_m_ = walk.distance_walked_m + tuning.arm_distance_m;
            return;
        }
        distance_m_ = walk.distance_walked_m;

        const bool landed = walk.grounded && !grounded_;
        grounded_ = walk.grounded;
        const float stride = std::max(
            walk.sprinting ? tuning.sprint_stride_m : tuning.walk_stride_m,
            0.05f);

        if (landed) {
            play_(mixer, bank, walk, tuning, true);
            next_step_m_ = walk.distance_walked_m + stride;
            return;
        }
        // Airborne feet do not touch anything. distance_walked_m does not
        // advance off the ground either, so this is belt and braces.
        if (!walk.grounded) return;

        if (walk.speed_mps < tuning.idle_speed_mps) {
            next_step_m_ = walk.distance_walked_m + tuning.arm_distance_m;
            return;
        }
        if (walk.distance_walked_m < next_step_m_) return;

        play_(mixer, bank, walk, tuning, false);
        // Advance from the crossing, not from where the character is now, so
        // the cadence stays on a grid and a long step does not shorten the
        // next stride. A jump in distance resyncs instead of owing a burst.
        next_step_m_ += stride;
        if (next_step_m_ < walk.distance_walked_m)
            next_step_m_ = walk.distance_walked_m + stride;
    }

    // Re-arms the cadence. The lifetime footfall COUNT is deliberately not
    // reset: it salts the take rotation, so restarting it would replay the
    // same sequence every time the player steps out of a car.
    void reset() {
        armed_ = false;
        grounded_ = true;
        distance_m_ = 0.0f;
        next_step_m_ = 0.0f;
        previous_.fill(-1);
    }

    // Inspection, for tests and a debug overlay. Never a decision input.
    uint32_t steps_played() const { return steps_; }
    FootstepSurface last_surface() const { return last_surface_; }
    const FootstepMix& last_mix() const { return last_mix_; }
    bool played_a_take() const { return played_a_take_; }

private:
    void play_(VoiceMixer& mixer, const SfxBank& bank,
               const FootstepWalk& walk, const FootstepTuning& tuning,
               bool landing) {
        // Keyed on the step count AND the metre the foot landed on: the count
        // alone repeats a pattern when you pace back and forth, the position
        // alone repeats the same take on the same paving stone.
        const uint64_t roll = hash_coord3(
            kFootstepSalt, static_cast<int32_t>(walk.ground.x),
            static_cast<int32_t>(walk.ground.y), steps_);
        const FootstepMix mix = footstep_mix(walk, tuning, landing, roll);
        const std::size_t row = static_cast<std::size_t>(walk.surface);
        const int previous = row < previous_.size() ? previous_[row] : -1;
        const PcmClip* take = footstep_take(bank, walk.surface, roll, previous);

        ++steps_;
        last_surface_ = walk.surface;
        last_mix_ = mix;
        played_a_take_ = take != nullptr;
        if (!take) return;
        if (row < previous_.size())
            previous_[row] = static_cast<int>(take - &bank.footsteps[row][0]);

        VoiceParams p;
        p.category = Category::Footsteps;
        p.gain = mix.gain;
        p.pitch = mix.pitch;
        p.lp_cutoff_hz = mix.lp_cutoff_hz;
        // The player's own feet are at the listener. A spatial emitter here
        // would pan them off centre as the camera swings behind the character.
        p.spatial = false;
        p.pan = 0.0f;
        mixer.play_oneshot(take, p);
    }

    bool armed_ = false;
    bool grounded_ = true;
    float distance_m_ = 0.0f;
    float next_step_m_ = 0.0f;

    std::array<int, kFootstepSurfaceCount> previous_{-1, -1, -1, -1, -1};
    uint32_t steps_ = 0;
    FootstepSurface last_surface_ = FootstepSurface::Concrete;
    FootstepMix last_mix_{};
    bool played_a_take_ = false;
};

}  // namespace apricot
