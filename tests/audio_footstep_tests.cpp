// Footstep audio: cadence, surface choice, take rotation, and the recorded
// assets themselves.
//
// No test here can listen, so nothing below asserts "a sound played". Every
// claim is a count, a gain, a ratio or a measured waveform — see
// src/audio/README.md on why that distinction is the whole discipline.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

#include "audio/footstep_audio.h"
#include "audio/player_car_assets.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot_test;

namespace {

constexpr float kDt = 1.0f / 120.0f;

// A bank whose footstep rows hold distinguishable synthetic PCM. Deliberately
// NOT the shipped recordings: the cadence tests need to know exactly how many
// takes each family has, and tying them to the pack would make a future asset
// swap fail a test about arithmetic.
SfxBank fake_bank(std::size_t takes_per_family = kFootstepVariantCount) {
    SfxBank bank;
    for (std::size_t s = 0; s < kFootstepSurfaceCount; ++s) {
        for (std::size_t v = 0; v < takes_per_family; ++v) {
            PcmClip& clip = bank.footsteps[s][v];
            clip.channels = 1;
            clip.samples.assign(480, 0.0f);
            // One distinct constant per slot, so a rendered buffer identifies
            // which take was chosen.
            clip.samples[0] = 0.1f * static_cast<float>(s + 1) +
                              0.01f * static_cast<float>(v + 1);
        }
    }
    return bank;
}

// Drive one update and report whether it owed a footfall.
struct Walker {
    VoiceMixer mixer;
    SfxBank bank = fake_bank();
    FootstepAudio audio;
    FootstepTuning tuning;
    FootstepWalk walk;

    Walker() {
        // Nothing will ever drain the command ring in these cases, so say so:
        // otherwise the ring fills and dropped_commands() climbs forever.
        mixer.set_silent(true);
        walk.on_foot = true;
        walk.grounded = true;
    }

    // Advance `metres` at `speed`, in `slices` updates. Returns footfalls owed.
    uint32_t travel(float metres, float speed, int slices, bool sprinting = false) {
        const uint32_t before = audio.steps_played();
        walk.sprinting = sprinting;
        walk.speed_mps = speed;
        for (int i = 0; i < slices; ++i) {
            walk.distance_walked_m += metres / static_cast<float>(slices);
            walk.ground.x += metres / static_cast<float>(slices);
            audio.update(mixer, bank, walk, tuning);
        }
        return audio.steps_played() - before;
    }
};

// --- cadence ---------------------------------------------------------------

// THE headline claim. A timer-driven footstep count changes when the update
// rate changes; a distance-driven one cannot.
void cadence_is_distance_not_time() {
    Walker coarse, fine;
    // Arm both: the first update on foot never fires.
    coarse.audio.update(coarse.mixer, coarse.bank, coarse.walk, coarse.tuning);
    fine.audio.update(fine.mixer, fine.bank, fine.walk, fine.tuning);

    const uint32_t coarse_steps = coarse.travel(24.0f, 2.35f, 60);
    const uint32_t fine_steps = fine.travel(24.0f, 2.35f, 2400);
    REQUIRE(coarse_steps == fine_steps);
    const double expected =
        (24.0 - coarse.tuning.arm_distance_m) / coarse.tuning.walk_stride_m;
    REQUIRE_MSG(std::abs(coarse_steps - expected) <= 1.0,
                "24 m of walking owes one footfall per stride", "walk cadence");
    pass("footfall count is set by distance covered, not by the update rate");
}

// The cadence is the perceivable claim, and it is a RATE, not a count. Both
// figures come from real gait — a person at 2.35 m/s takes about 2.5 steps a
// second, a runner at 6.25 m/s about 3.9 — and both were measured out of the
// real mixer before being written down here. A stride shortened by a tenth of a
// metre still passes every other test in this file and sounds wrong instantly,
// which is why the rate is pinned rather than left to arithmetic.
void the_cadence_matches_a_real_gait() {
    const auto rate = [](float speed, bool sprinting) {
        Walker w;
        w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
        constexpr float kSeconds = 20.0f;
        const int slices = static_cast<int>(kSeconds / kDt);
        const uint32_t steps = w.travel(speed * kSeconds, speed, slices, sprinting);
        return static_cast<double>(steps) / kSeconds;
    };
    const double walking = rate(2.35f, false);
    const double sprinting = rate(6.25f, true);
    std::printf("      walk %.2f footfalls/s   sprint %.2f footfalls/s\n",
                walking, sprinting);
    REQUIRE_MSG(walking > 2.40 && walking < 2.70,
                "a walk paces like a walk", "walk rate");
    REQUIRE_MSG(sprinting > 3.70 && sprinting < 4.05,
                "a sprint paces like a run", "sprint rate");
    pass("the walking and sprinting cadences match a real gait");
}

void sprinting_lengthens_the_stride_and_still_steps_faster() {
    Walker walk, sprint;
    walk.audio.update(walk.mixer, walk.bank, walk.walk, walk.tuning);
    sprint.audio.update(sprint.mixer, sprint.bank, sprint.walk, sprint.tuning);

    const uint32_t walked = walk.travel(40.0f, 2.35f, 400);
    const uint32_t sprinted = sprint.travel(40.0f, 6.25f, 400, true);
    // Fewer footfalls over the same GROUND...
    REQUIRE(sprinted < walked);
    // ...and more per second, because the ground goes by 2.66x faster.
    const double walk_rate = walked / (40.0 / 2.35);
    const double sprint_rate = sprinted / (40.0 / 6.25);
    REQUIRE_MSG(sprint_rate > walk_rate * 1.3,
                "a sprint is audibly quicker despite the longer stride",
                "sprint cadence");
    pass("a sprint lengthens the stride and still steps more often per second");
}

void standing_still_is_silent() {
    Walker w;
    for (int i = 0; i < 600; ++i) {
        w.walk.speed_mps = 0.0f;
        w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    }
    REQUIRE(w.audio.steps_played() == 0u);
    pass("standing still owes no footfalls");
}

// The bug this guards is specific: a speed-driven cadence marches on the spot
// when the character is pressed into a wall and going nowhere.
void walking_into_a_wall_is_silent() {
    Walker w;
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    for (int i = 0; i < 600; ++i) {
        w.walk.speed_mps = 2.35f;          // the stick is held
        // ...and distance_walked_m does not move, because nothing moved.
        w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    }
    REQUIRE(w.audio.steps_played() == 0u);
    pass("a character held against a wall does not march on the spot");
}

void the_first_step_lands_soon_after_setting_off() {
    Walker w;
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    // Stand for a while, then walk exactly one arming distance.
    for (int i = 0; i < 100; ++i) {
        w.walk.speed_mps = 0.0f;
        w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    }
    REQUIRE(w.travel(w.tuning.arm_distance_m + 0.01f, 2.35f, 8) == 1u);
    pass("the first footfall after setting off lands within the arming distance");
}

void a_landing_fires_at_once_and_is_the_loudest_step() {
    Walker w;
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    w.travel(2.0f, 2.35f, 20);
    const float walking_gain = w.audio.last_mix().gain;

    w.walk.grounded = false;
    w.walk.speed_mps = 2.35f;
    for (int i = 0; i < 30; ++i) w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    const uint32_t before = w.audio.steps_played();
    w.walk.grounded = true;
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    REQUIRE(w.audio.steps_played() == before + 1u);
    REQUIRE(w.audio.last_mix().gain > walking_gain);
    pass("touching down plays one louder footfall on the frame it happens");
}

void airborne_feet_are_silent() {
    Walker w;
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    w.walk.grounded = false;
    // Distance would normally freeze in the air; force it forward anyway to
    // prove grounded is checked rather than merely implied.
    const uint32_t steps = w.travel(30.0f, 6.0f, 300);
    REQUIRE(steps == 0u);
    pass("no footfall while the feet are off the ground");
}

// Getting into a car, respawning and the dev teleport all hand back a character
// whose distance counter has restarted. That must re-arm, not owe a burst.
void a_reset_distance_counter_rearms_instead_of_bursting() {
    Walker w;
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    w.travel(50.0f, 2.35f, 500);
    const uint32_t before = w.audio.steps_played();

    w.walk.distance_walked_m = 0.0f;   // spawn_character()'s fresh state
    w.walk.speed_mps = 0.0f;
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    REQUIRE(w.audio.steps_played() == before);
    // And it still works afterwards.
    REQUIRE(w.travel(10.0f, 2.35f, 100) > 8u);
    pass("a restarted distance counter re-arms rather than firing a burst");
}

void leaving_on_foot_stops_and_rearms() {
    Walker w;
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    w.travel(10.0f, 2.35f, 100);
    const uint32_t before = w.audio.steps_played();
    w.walk.on_foot = false;
    for (int i = 0; i < 100; ++i) {
        w.walk.distance_walked_m += 1.0f;
        w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    }
    REQUIRE(w.audio.steps_played() == before);
    pass("no footfalls while the player is not on foot");
}

// --- gain, pitch and snow ---------------------------------------------------

void a_creep_is_quieter_than_a_walk_and_a_walk_than_a_sprint() {
    FootstepTuning tuning;
    FootstepWalk creep, walk, sprint;
    creep.speed_mps = 0.45f;
    walk.speed_mps = 2.35f;
    sprint.speed_mps = 6.25f;
    sprint.sprinting = true;
    const float a = footstep_mix(creep, tuning, false, 0).gain;
    const float b = footstep_mix(walk, tuning, false, 0).gain;
    const float c = footstep_mix(sprint, tuning, false, 0).gain;
    REQUIRE(a < b);
    REQUIRE(b < c);
    // The taper has a floor: a creep is quiet, never inaudible.
    REQUIRE(a > tuning.walk_gain * tuning.quiet_floor * 0.99f);
    pass("footfall gain rises from a creep through a walk to a sprint");
}

void snow_both_quietens_and_dulls_a_footfall() {
    FootstepTuning tuning;
    FootstepWalk dry, deep;
    dry.speed_mps = deep.speed_mps = 2.35f;
    deep.snow_depth_m = 0.30f;   // past full depth
    const FootstepMix a = footstep_mix(dry, tuning, false, 0);
    const FootstepMix b = footstep_mix(deep, tuning, false, 0);
    REQUIRE(b.gain < a.gain * 0.6f);
    REQUIRE(b.lp_cutoff_hz < a.lp_cutoff_hz * 0.25f);
    // Past full depth the muffling stops deepening rather than reaching zero.
    FootstepWalk absurd = deep;
    absurd.snow_depth_m = 40.0f;
    REQUIRE_NEAR(footstep_mix(absurd, tuning, false, 0).gain, b.gain, 1e-6);
    REQUIRE(b.gain > 0.0f);
    pass("snow under the boot quietens and dulls the step, and bottoms out");
}

void pitch_jitters_around_native_and_never_inverts() {
    FootstepTuning tuning;
    FootstepWalk walk;
    walk.speed_mps = 2.35f;
    float low = 2.0f, high = 0.0f;
    double sum = 0.0;
    constexpr int kRolls = 4096;
    for (int i = 0; i < kRolls; ++i) {
        const float pitch = footstep_mix(
            walk, tuning, false, hash_coord(kFootstepSalt, i, 0)).pitch;
        low = std::min(low, pitch);
        high = std::max(high, pitch);
        sum += pitch;
    }
    REQUIRE(low >= 1.0f - tuning.pitch_jitter - 1e-6f);
    REQUIRE(high <= 1.0f + tuning.pitch_jitter + 1e-6f);
    REQUIRE(low < 1.0f - tuning.pitch_jitter * 0.8f);   // the range is used
    REQUIRE(high > 1.0f + tuning.pitch_jitter * 0.8f);
    REQUIRE_NEAR(sum / kRolls, 1.0, 0.005);             // and centred
    pass("pitch jitter spans its range, stays inside it, and centres on native");
}

// --- surface choice --------------------------------------------------------

void the_road_bake_beats_the_terrain_under_it() {
    // A sidewalk over a grass basin must not sound like a lawn.
    REQUIRE(footstep_surface(AudioSurface::Grass, true, false) ==
            FootstepSurface::Concrete);
    REQUIRE(footstep_surface(AudioSurface::Sand, true, false) ==
            FootstepSurface::Concrete);
    // ...and it beats a prop flag too, since the road bake IS the surface.
    REQUIRE(footstep_surface(AudioSurface::Grass, true, true) ==
            FootstepSurface::Concrete);
    // But an UNPAVED road is Gravel in the bake's own material, and must keep
    // it: a dirt track is a road and does not sound like a pavement.
    REQUIRE(footstep_surface(AudioSurface::Gravel, true, false) ==
            FootstepSurface::Gravel);
    REQUIRE(footstep_surface(AudioSurface::Gravel, true, true) ==
            FootstepSurface::Gravel);
    pass("paved ground sounds paved, and an unpaved road still sounds loose");
}

void every_terrain_material_maps_to_a_family() {
    REQUIRE(footstep_surface(AudioSurface::Rock, false, false) ==
            FootstepSurface::Stone);
    REQUIRE(footstep_surface(AudioSurface::Gravel, false, false) ==
            FootstepSurface::Gravel);
    REQUIRE(footstep_surface(AudioSurface::Grass, false, false) ==
            FootstepSurface::Grass);
    REQUIRE(footstep_surface(AudioSurface::Sand, false, false) ==
            FootstepSurface::Dirt);
    // An authored prop top is a built slab until props carry their own
    // material; see the comment on footstep_surface().
    REQUIRE(footstep_surface(AudioSurface::Grass, false, true) ==
            FootstepSurface::Stone);
    // Every family has a name, and no two share one.
    std::set<std::string> names;
    for (std::size_t i = 0; i < kFootstepSurfaceCount; ++i) {
        const char* name = footstep_surface_name(static_cast<FootstepSurface>(i));
        REQUIRE(name[0] != '?');
        names.insert(name);
    }
    REQUIRE(names.size() == kFootstepSurfaceCount);
    pass("every ground material maps to exactly one named footstep family");
}

// --- take rotation ---------------------------------------------------------

void the_rotation_uses_every_loaded_take_and_never_doubles() {
    Walker w;
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    w.walk.surface = FootstepSurface::Gravel;
    const std::size_t row = static_cast<std::size_t>(FootstepSurface::Gravel);

    std::vector<int> sequence;
    for (int i = 0; i < 400; ++i) {
        const uint32_t before = w.audio.steps_played();
        w.walk.speed_mps = 2.35f;
        w.walk.distance_walked_m += 0.2f;
        w.walk.ground.x += 0.2f;
        w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
        if (w.audio.steps_played() == before) continue;
        REQUIRE(w.audio.played_a_take());
        // Recover which take by matching the clip the mixer was handed. The
        // fake bank's first sample is unique per slot.
        sequence.push_back(-1);
        for (std::size_t v = 0; v < kFootstepVariantCount; ++v) {
            if (&w.bank.footsteps[row][v] ==
                footstep_take(w.bank, FootstepSurface::Gravel,
                              hash_coord3(kFootstepSalt,
                                          static_cast<int32_t>(w.walk.ground.x),
                                          static_cast<int32_t>(w.walk.ground.y),
                                          before),
                              sequence.size() >= 2
                                  ? sequence[sequence.size() - 2] : -1)) {
                sequence.back() = static_cast<int>(v);
            }
        }
    }
    REQUIRE(sequence.size() > 80u);
    std::set<int> used(sequence.begin(), sequence.end());
    REQUIRE_MSG(used.size() == kFootstepVariantCount,
                "a long walk reaches every take in the family", "rotation");
    pass("the take rotation reaches every loaded take");
}

void no_take_plays_twice_running() {
    // Straight at footstep_take(), which is where the rule lives: whatever the
    // hash says, the previous take is not repeated while an alternative exists.
    SfxBank bank = fake_bank();
    for (std::size_t s = 0; s < kFootstepSurfaceCount; ++s) {
        const auto surface = static_cast<FootstepSurface>(s);
        int previous = -1;
        for (int i = 0; i < 2000; ++i) {
            const PcmClip* take = footstep_take(
                bank, surface, hash_coord(kFootstepSalt, i, static_cast<int>(s)),
                previous);
            REQUIRE(take != nullptr);
            const int index = static_cast<int>(take - &bank.footsteps[s][0]);
            REQUIRE(index != previous);
            previous = index;
        }
    }
    pass("no footstep take is chosen twice in a row");
}

// A family the pack supplies four takes of must rotate over four, not roll a
// one-in-six chance of silence.
void a_short_family_rotates_over_what_loaded() {
    SfxBank bank = fake_bank(4);
    std::set<int> used;
    int previous = -1;
    for (int i = 0; i < 500; ++i) {
        const PcmClip* take = footstep_take(
            bank, FootstepSurface::Stone, hash_coord(kFootstepSalt, i, 7),
            previous);
        REQUIRE_MSG(take != nullptr, "a short family never yields silence",
                    "short family");
        REQUIRE(!take->empty());
        previous = static_cast<int>(
            take - &bank.footsteps[static_cast<std::size_t>(
                       FootstepSurface::Stone)][0]);
        used.insert(previous);
    }
    REQUIRE(used.size() == 4u);
    REQUIRE(*used.rbegin() == 3);
    pass("a family with four takes rotates over four, never over six slots");
}

void an_empty_family_is_silent_without_faking_a_footstep() {
    Walker w;
    w.bank = SfxBank{};   // nothing loaded at all
    w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
    const uint32_t steps = w.travel(20.0f, 2.35f, 200);
    REQUIRE(steps > 0u);                      // the cadence still runs...
    REQUIRE(!w.audio.played_a_take());        // ...and plays nothing
    REQUIRE(footstep_take(w.bank, FootstepSurface::Grass, 0, -1) == nullptr);
    // Out-of-range is silence too, not a read past the row array.
    REQUIRE(footstep_take(fake_bank(), FootstepSurface::kCount, 0, -1) == nullptr);
    pass("a family with no recordings is silent, never a synthesised stand-in");
}

void the_same_walk_replays_the_same_footsteps() {
    const auto run = [] {
        Walker w;
        w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
        std::vector<float> gains;
        std::vector<float> pitches;
        for (int i = 0; i < 600; ++i) {
            const uint32_t before = w.audio.steps_played();
            w.walk.speed_mps = (i % 7 == 0) ? 1.1f : 2.35f;
            w.walk.sprinting = (i % 11 == 0);
            w.walk.distance_walked_m += 0.05f;
            w.walk.ground.x += 0.05f;
            w.audio.update(w.mixer, w.bank, w.walk, w.tuning);
            if (w.audio.steps_played() != before) {
                gains.push_back(w.audio.last_mix().gain);
                pitches.push_back(w.audio.last_mix().pitch);
            }
        }
        return std::make_pair(gains, pitches);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.first.size() > 20u);
    REQUIRE(a.first == b.first);
    REQUIRE(a.second == b.second);
    pass("an identical walk produces an identical footstep sequence");
}

// --- the real producer ------------------------------------------------------

// A hand-fed FootstepWalk proves the arithmetic and nothing about whether the
// game feeds it correctly. This drives the REAL character step over the REAL
// collider and assembles the input the way App does, through the same
// footstep_walk() the app calls.
void a_real_walk_over_real_ground_paces_itself() {
    TerrainCollider collider{0xC0FFEEu};
    CharacterTuning character;
    PlayerCharacterState person = spawn_character(collider, 40.0f, -18.0f);

    VoiceMixer mixer;
    mixer.set_silent(true);
    const SfxBank bank = fake_bank();
    FootstepAudio audio;
    const FootstepTuning tuning;

    InputFrame input;
    input.throttle = 1.0f;

    std::set<FootstepSurface> surfaces;
    const float start_x = person.position.x;
    const float start_z = person.position.z;
    constexpr int kSteps = 1200;   // ten seconds at 120 Hz
    for (int i = 0; i < kSteps; ++i) {
        person = step_character(person, character, input, collider, kDt);
        const TerrainCollider::GroundHit ground = collider.probe_down(
            {person.position.x, person.position.y + 0.25f, person.position.z},
            0.60f);
        const Surface material = ground.hit
            ? ground.material
            : collider.material(person.position.x, person.position.z);
        const AudioSurface audio_material =
            static_cast<AudioSurface>(surface_index(material));
        audio.update(mixer, bank,
                     footstep_walk(person.position, person.velocity,
                                   person.distance_walked_m, person.grounded,
                                   person.sprinting, audio_material,
                                   ground.hit && ground.road,
                                   ground.hit && ground.prop,
                                   ground.hit ? ground.snow_depth_m : 0.0f),
                     tuning);
        if (audio.steps_played() > 0) surfaces.insert(audio.last_surface());
    }

    const float travelled = std::hypot(person.position.x - start_x,
                                       person.position.z - start_z);
    REQUIRE_MSG(travelled > 15.0f, "the real character actually walked",
                "real walk");
    // The pace must match the ground covered, within the arming step and one
    // stride of slack at each end.
    const double expected = person.distance_walked_m / tuning.walk_stride_m;
    REQUIRE_MSG(audio.steps_played() >= expected - 3.0 &&
                audio.steps_played() <= expected + 1.0,
                "footfalls track the distance the real step produced",
                "real cadence");
    // And a real walk on real ground lands on a real family, not the default
    // that a zeroed struct would give.
    REQUIRE(!surfaces.empty());
    REQUIRE(audio.played_a_take());
    pass("a real character walking real ground paces its own footsteps");
}

// --- the shipped recordings ------------------------------------------------

void the_shipped_footstep_assets_load_and_are_usable() {
    SfxBank bank;
    SfxOverridePaths paths;
    paths.footsteps = player_car_audio_overrides().footsteps;
    const std::size_t loaded = override_bank_from_wavs(bank, paths);
    // 5 + 4 + 5 + 5 + 6 across concrete, stone, gravel, dirt and grass.
    REQUIRE(shipped_footstep_take_count() == 25u);
    REQUIRE_MSG(loaded == shipped_footstep_take_count(),
                "every cooked footstep take loads", "assets");

    for (std::size_t s = 0; s < kFootstepSurfaceCount; ++s) {
        const auto surface = static_cast<FootstepSurface>(s);
        std::size_t takes = 0;
        for (std::size_t v = 0; v < kFootstepVariantCount; ++v) {
            const PcmClip& clip = bank.footsteps[s][v];
            if (clip.empty()) continue;
            ++takes;
            REQUIRE(clip.channels == 1u);
            REQUIRE(clip.sample_rate == 48000u);
            // A footfall is a transient: long enough to have a body, far too
            // short to be a loop. A take outside this is the wrong file.
            REQUIRE_MSG(clip.duration_seconds() > 0.08f &&
                        clip.duration_seconds() < 0.40f,
                        "a footstep take is a tenth-of-a-second transient",
                        footstep_surface_name(surface));
            float peak = 0.0f;
            double energy = 0.0;
            for (const float sample : clip.samples) {
                peak = std::max(peak, std::abs(sample));
                energy += static_cast<double>(sample) * sample;
            }
            // Headroom under the engine and the city bed, and not silence.
            REQUIRE_MSG(peak > 0.05f && peak < 0.95f,
                        "a take is audible and leaves headroom",
                        footstep_surface_name(surface));
            const double rms = std::sqrt(energy / clip.samples.size());
            REQUIRE(rms > 0.005);
            // No click at either end: a footstep is triggered ON the footfall,
            // so a non-zero first sample is an audible tick before the step.
            REQUIRE(std::abs(clip.samples.front()) < 0.02f);
            REQUIRE(std::abs(clip.samples.back()) < 0.02f);
        }
        REQUIRE_MSG(takes >= 4u, "every shipped family has at least four takes",
                    footstep_surface_name(surface));
        // ...and the runtime can therefore always find one.
        REQUIRE(footstep_take(bank, surface, 0, -1) != nullptr);
    }

    // The families must be within reach of each other in loudness, or crossing
    // a kerb reads as a volume bug rather than as a change of surface. The
    // cook normalises per family for exactly this; the check is that it did.
    double loudest = 0.0, quietest = 1.0;
    for (std::size_t s = 0; s < kFootstepSurfaceCount; ++s) {
        double energy = 0.0;
        std::size_t frames = 0;
        for (std::size_t v = 0; v < kFootstepVariantCount; ++v) {
            for (const float sample : bank.footsteps[s][v].samples) {
                energy += static_cast<double>(sample) * sample;
                ++frames;
            }
        }
        const double rms = std::sqrt(energy / static_cast<double>(frames));
        loudest = std::max(loudest, rms);
        quietest = std::min(quietest, rms);
    }
    REQUIRE_MSG(loudest < quietest * 2.0,
                "no family is twice as loud as another", "family balance");
    pass("the shipped footstep recordings load, are clean, and are balanced");
}

void a_missing_footstep_file_changes_nothing() {
    SfxBank bank;
    SfxOverridePaths paths;
    paths.footsteps[0][0] = "/missing/apricot-footstep.wav";
    REQUIRE(override_bank_from_wavs(bank, paths) == 0u);
    REQUIRE(bank.footsteps[0][0].empty());
    pass("a footstep path that does not resolve loads nothing and fails nothing");
}

}  // namespace

int main() {
    cadence_is_distance_not_time();
    the_cadence_matches_a_real_gait();
    sprinting_lengthens_the_stride_and_still_steps_faster();
    standing_still_is_silent();
    walking_into_a_wall_is_silent();
    the_first_step_lands_soon_after_setting_off();
    a_landing_fires_at_once_and_is_the_loudest_step();
    airborne_feet_are_silent();
    a_reset_distance_counter_rearms_instead_of_bursting();
    leaving_on_foot_stops_and_rearms();
    a_creep_is_quieter_than_a_walk_and_a_walk_than_a_sprint();
    snow_both_quietens_and_dulls_a_footfall();
    pitch_jitters_around_native_and_never_inverts();
    the_road_bake_beats_the_terrain_under_it();
    every_terrain_material_maps_to_a_family();
    the_rotation_uses_every_loaded_take_and_never_doubles();
    no_take_plays_twice_running();
    a_short_family_rotates_over_what_loaded();
    an_empty_family_is_silent_without_faking_a_footstep();
    the_same_walk_replays_the_same_footsteps();
    a_real_walk_over_real_ground_paces_itself();
    the_shipped_footstep_assets_load_and_are_usable();
    a_missing_footstep_file_changes_nothing();
    return done("audio_footstep_tests");
}
