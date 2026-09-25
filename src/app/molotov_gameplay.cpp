#include "app/app.h"

#include <algorithm>

#include "core/log.h"
#include "game/police_combat.h"
#include "terrain/heightmap.h"

namespace apricot {
namespace {

// How far a bottle may be pushed by the collision search in one step. The
// projectile moves 0.3 m at kSimDt on a hard throw, so this only bounds the
// pathological case of a very long dt after a stall.
constexpr float kMaxSweepM = 8.0f;

// How far back off the surface the burning fuel lands. Wide enough to clear a
// wall's collision face and the float error on it; short enough that a bottle
// thrown at somebody's feet still burns where they are standing.
constexpr float kSpillStepM = 0.35f;

// How far the fire throws light, and how hard. ONE light for the whole field,
// at the burning centroid: a spot light per cell would put sixty-four extra
// sources into the tiled grid for a glow the eye reads as a single fire.
constexpr float kFireLightRangeM = 11.0f;
constexpr float kFireLightDrawDistanceM = 140.0f;

}  // namespace

// The throw. Everything about WHEN a bottle may leave the hand is in
// game/molotov.h; this is the part that needs the world — where the bottle
// starts, what it hits, and what catches fire when it does.
void App::step_molotov(bool available, float dt) {
    MolotovUseInput controls;
    controls.available = available;
    controls.aim = weapon_aim_mouse_ || weapon_aim_pad_ || weapon_aim_toggle_;
    controls.throw_pressed = molotov_throw_pending_;
    const bool thrown = molotov_use_.step(weapon_wheel_.equipped, controls, dt);
    if (dt > 0.0f || !available) molotov_throw_pending_ = false;

    // Fly the bottles already out BEFORE launching a new one, so this step's
    // throw is not also this step's first metre of travel — a bottle that
    // moved before it was drawn would appear a stride in front of the hand.
    const auto previous = molotov_shots_.shots();
    molotov_shots_.step(dt);
    const auto& current = molotov_shots_.shots();
    for (std::size_t i = 0; i < current.size(); ++i) {
        if (!current[i].live || !previous[i].live) continue;
        const glm::vec3 travel = current[i].position - previous[i].position;
        const float distance = glm::length(travel);
        if (!(distance > 1e-4f) || distance > kMaxSweepM) continue;
        const glm::vec3 direction = travel / distance;
        // The world first, then people, clipped to the world hit: a bottle
        // cannot break on somebody standing behind a wall. Same ordering the
        // pistol uses, for the same reason.
        const auto wall = collider_.raycast(previous[i].position, direction, distance);
        const auto body = world_.traffic().raycast_ped(
            previous[i].position, direction, wall.hit ? wall.distance : distance);
        if (!wall.hit && !body.hit) continue;
        const glm::vec3 impact = body.hit ? body.point
            : previous[i].position + direction * wall.distance;
        // WHICH WAY THE FUEL SPLASHES, and it is not a detail. A bottle that
        // breaks against a shop front breaks on the OUTSIDE of the wall, and
        // the burning fuel runs down it into the street. Igniting at the
        // contact point instead put the fire on the far side of that surface:
        // a probe straight down from a point sitting on a building's collision
        // face lands on the building's ROOF, five metres up, where it cannot
        // reach any ground to spread to and simply burns alone in the sky.
        // Stepping back along the surface normal — or, for a person, back
        // along the throw — puts the fuel where it actually goes.
        const glm::vec3 away = body.hit || !wall.hit ? -direction : wall.normal;
        molotov_shots_.extinguish(i);
        break_molotov(impact + away * kSpillStepM, current[i].throw_id);
    }

    if (!thrown) return;
    ++molotov_throws_;
    // Out of the hand where the player can see it is, if the bottle is drawn;
    // out of the chest if the palm socket is unavailable this frame, which is
    // the same fallback the pistol's muzzle uses.
    glm::vec3 origin;
    if (!molotov_visual_.release_point(origin))
        origin = player_character_.position + glm::vec3{0.0f, 1.45f, 0.0f};
    const glm::vec3 velocity = molotov_throw_velocity(
        player_character_.view_yaw, player_character_.view_pitch,
        molotov_use_.aim_blend);
    // The bottle inherits the thrower. Running forward and lobbing one at your
    // own feet is the version without this line.
    const glm::vec3 carried = velocity + glm::vec3{
        player_character_.velocity.x, 0.0f, player_character_.velocity.z};
    const uint64_t throw_id = static_cast<uint64_t>(step_index_) * 131u +
                              molotov_throws_;
    if (!molotov_shots_.launch(origin, carried, throw_id)) {
        AP_WARN("molotov: no free projectile slot; throw dropped");
        return;
    }
    AP_INFO("molotov thrown: %d left", molotov_use_.stock);
}

// One bottle breaking. The glass, the fuel catching, the heat it earns, and
// the patch of ground that starts burning — in that order, because that is the
// order the player perceives them.
//
// `spill` is where the FUEL went, already stepped clear of whatever the bottle
// broke on; see the spill step in step_molotov() for why those are not the
// same point.
void App::break_molotov(glm::vec3 spill, uint64_t throw_id) {
    VoiceParams sound;
    sound.category = Category::Impacts;
    sound.spatial = true;
    sound.position = spill;
    sound.gain = 0.85f;
    audio_device_.mixer().play_oneshot(&molotov_glass_clip_, sound);
    // The fuel catches a beat after the glass, which is both what happens and
    // what the trimmed take is shaped for: the whoosh opens on an audible
    // swell and peaks a little under half a second in. See
    // tools/prepare_molotov_audio.py.
    sound.gain = 0.78f;
    audio_device_.mixer().play_oneshot(&molotov_whoosh_clip_, sound);

    // The fire burns on the GROUND under the spill, not at the height the
    // bottle happened to break. A bottle that bursts on a first-floor window
    // leaves a fire in the street below, which is where the fuel goes.
    const auto under = collider_.probe_down(spill + glm::vec3{0.0f, 0.5f, 0.0f},
                                            30.0f, TerrainCollider::ProbeVehicles::Exclude);
    // Nothing under it within thirty metres is a bottle thrown off something
    // very tall. The glass and the whoosh still play — they happened — but no
    // fire is lit, because a fire needs ground and guessing at one leaves
    // flames hanging in the air where the bottle happened to be.
    if (!under.hit) {
        AP_INFO("molotov broke with no ground beneath it; no fire lit");
        return;
    }
    if (!fire_.ignite(spill, under.point.y, throw_id)) return;
    ++molotov_fires_lit_;
    // Charged once per bottle that actually catches. A throw that lands
    // somewhere already burning costs nothing extra, because the street is
    // already on fire and the city can only notice that once.
    wanted_.add_heat(kArsonHeat, WantedSystem::Crime::Violent);
    AP_INFO("molotov lit a fire at %.1f, %.1f; wanted %d",
            static_cast<double>(spill.x), static_cast<double>(spill.z),
            wanted_.level());
}

// The fire itself: spread, burn out, burn whoever is standing in it, and hold
// the loop open underneath.
void App::step_fire(float dt) {
    fire_.step(dt, [this](float x, float z, float near_y) {
        FireGround out;
        // Probe from just above the burning cell rather than from a fixed
        // height, so the flames follow a kerb or a ramp instead of finding the
        // roof of whatever they are standing beside.
        //
        // VEHICLES ARE EXCLUDED ON PURPOSE. A fire cell is a fixed world
        // position; letting one catch on a car roof leaves the flames hanging
        // in the air the moment the car drives away.
        const auto hit = collider_.probe_down(
            glm::vec3{x, near_y + 1.6f, z}, 3.2f,
            TerrainCollider::ProbeVehicles::Exclude);
        if (!hit.hit) return out;
        // Nothing burns on water.
        if (hit.point.y <= kSeaLevelMetres + 0.15f) return out;
        out.supported = true;
        out.height_m = hit.point.y;
        return out;
    });

    // The loop. Opened when the first cell lights, moved and re-gained every
    // step, closed when the last cell goes out — so a fire that burns for ten
    // seconds is one voice for ten seconds and no voice afterwards.
    const bool burning = fire_.burning();
    // ONE description of the voice, used to open it and to move it. Two copies
    // of an attenuation is two copies that can drift, and the way that presents
    // is a fire that changes how loud it is the instant it stops growing.
    const auto bed = [this](float gain) {
        VoiceParams params;
        params.category = Category::World;
        params.looping = true;
        params.spatial = true;
        params.position = fire_.centre();
        params.gain = gain;
        params.attenuation.ref_distance = 5.0f;
        params.attenuation.max_distance = 70.0f;
        return params;
    };
    if (burning && !fire_voice_.valid() && !fire_loop_clip_.empty()) {
        // Opened silent and gained up on the same step, so a fire that starts
        // while the player is a street away fades in from where it is rather
        // than snapping to full for one block.
        fire_voice_ = audio_device_.mixer().open_loop(&fire_loop_clip_, bed(0.0f));
        if (!fire_voice_.valid()) AP_WARN("molotov: no free loop voice for the fire");
    }
    if (fire_voice_.valid()) {
        if (!burning) {
            audio_device_.mixer().close_loop(fire_voice_);
            fire_voice_ = VoiceHandle{};
        } else {
            audio_device_.mixer().set_loop(fire_voice_, bed(0.92f * fire_.intensity()));
        }
    }

    // Everybody else standing in it, on the shared beat in game/fire_harm.h.
    // Every fire in the game is one the player lit, so a death in it is
    // charged like any other death the player caused.
    if (fire_npc_bite_.due(dt, burning)) {
        const auto bitten = world_.burn_people(fire_, static_cast<int64_t>(step_index_));
        if (!bitten.empty()) {
            fire_npc_bites_ += static_cast<unsigned>(bitten.size());
            AP_INFO("fire bit %zu %s", bitten.size(), bitten.size() == 1 ? "person" : "people");
        }
        for (const PedShotHit& body : bitten) {
            charge_body_heat(body);
            if (body.killed)
                AP_INFO("fire killed %s %llu/%u; wanted %d",
                        body.officer ? "police officer" : "pedestrian",
                        static_cast<unsigned long long>(body.lane_key), body.slot,
                        wanted_.level());
        }
    }

    // Standing in it. On foot only: the car has its own shell, its own damage
    // model and no way yet to be on fire, and charging the driver for flames
    // the bodywork is ignoring would be the visible half of a system that does
    // not exist.
    fire_player_damage_timer_ = std::max(0.0f, fire_player_damage_timer_ - dt);
    if (!on_foot_ || !player_vitals_.alive()) return;
    const float heat = fire_.heat_at(player_character_.position);
    if (heat <= 0.05f) return;
    if (fire_player_damage_timer_ > 0.0f) return;
    fire_player_damage_timer_ = kFireBiteSeconds;
    damage_player(kFireBiteDamage * heat, "fire");
}

// The glow a fire puts on the street around it.
void App::append_fire_light(std::vector<TrafficSpotLight>& lights) const {
    if (!fire_.burning()) return;
    const glm::vec3 centre = fire_.centre();
    if (glm::distance(centre, camera_.position) >= kFireLightDrawDistanceM) return;
    const float power = 3.4f * fire_.intensity();
    if (power <= 0.01f) return;
    // Pointed down with a very wide cone: the flames themselves are emissive
    // geometry and supply the source the eye looks at, so this exists only to
    // put their colour onto the paving and the walls beside them.
    lights.push_back({glm::vec4{centre + glm::vec3{0.0f, 1.1f, 0.0f}, kFireLightRangeM},
                      glm::vec4{0.0f, -1.0f, 0.0f, power},
                      glm::vec4{1.0f, 0.46f, 0.14f, 0.05f}});
}

}  // namespace apricot
