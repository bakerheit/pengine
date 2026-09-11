#include "app/app.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace apricot {

// Everything that can hurt the player, and the one door all of it goes
// through.
//
// The door is the point. Before this file there was a single `player_health_`
// float subtracted from in one place — inside the police shooting check — and
// the respawn was three statements further down the same loop. Adding a second
// cause of death meant copying the respawn, and a copied respawn is a respawn
// that forgets to clear the wanted level, or holster the pistol, or move the
// body. Every source below calls damage_player() and stops there; what being
// dead MEANS is written once, in kill_player() and step_player_vitals().

bool App::damage_player(float amount, const char* cause) {
    if (!player_vitals_.take_damage(amount)) {
        if (player_vitals_.alive() && std::isfinite(amount) && amount > 0.0f) {
            player_hit_feedback_s_ = 0.45f;
            AP_INFO("player hurt by %s: %.0f damage, health %.0f", cause,
                    static_cast<double>(amount),
                    static_cast<double>(player_vitals_.health));
        }
        return false;
    }
    begin_player_death(cause);
    return true;
}

void App::begin_player_death(const char* cause) {
    // Dying ends the chase. Everything the pursuit is holding — the heat, the
    // witnessed offences in flight, the arrest hold an officer had on you — is
    // dropped together, because leaving any one of them armed means the
    // respawned player is immediately re-arrested by a cop still walking
    // toward where the body was.
    wanted_.reset();
    police_offenses_.reset();
    police_arrest_.reset();
    world_.set_police_context(0, player_focus_position());
    weapon_wheel_.equipped = WeaponId::Unarmed;
    weapon_use_ = WeaponUseState{};
    player_hit_feedback_s_ = 0.0f;
    ++player_death_reports_;
    AP_INFO("player killed by %s; wasted", cause);
}

void App::step_player_vitals(float dt) {
    player_hit_feedback_s_ = std::max(0.0f, player_hit_feedback_s_ - dt);
    if (!player_vitals_.step(dt)) return;

    // The respawn, on the one step the timer says so. Placed beside the
    // current vehicle rather than at a hospital: the hospital campus exists in
    // city/ but nothing routes to it yet, and a respawn that teleports you
    // across the island is a bigger decision than this change is making.
    place_character_next_to_car();
    prev_player_character_ = player_character_;
    player_character_.velocity = glm::vec3{0.0f};
    player_landing_speed_mps_ = 0.0f;
    player_vitals_.revive();
    AP_INFO("player respawned beside current vehicle at full health");
}

// Being run over. The crowd reports the contact and the closing speed; the
// curve that turns that into health is the SAME one that decides whether a
// pedestrian survives the player's bumper, which is the property worth having:
// the speed at which you can kill somebody is the speed at which somebody can
// kill you.
void App::check_on_foot_traffic_hits() {
    if (!on_foot_ || !player_vitals_.alive()) return;
    for (const auto& hit : world_.traffic().on_foot_player_hits()) {
        const float damage = vehicle_impact_damage(
            hit.closing_speed_mps, world_.traffic_tuning().ped_life.knockdown_speed_mps);
        if (damage <= 0.0f) continue;
        // Thrown the way the car was going, like anybody else it hits. The
        // character solver owns the position, so this goes in as velocity
        // rather than as a teleport.
        player_character_.velocity += glm::vec3{
            hit.travel_xz.x * hit.closing_speed_mps * 0.45f,
            hit.closing_speed_mps * 0.22f,
            hit.travel_xz.y * hit.closing_speed_mps * 0.45f};
        player_character_.grounded = false;
        // Airborne from a shove is not a fall the player chose, and charging
        // them landing damage on top of the impact double-bills one event.
        player_landing_speed_mps_ = 0.0f;
        ++player_run_down_reports_;
        AP_INFO("player hit by %s traffic at %.1f mph",
                hit.police_unit ? "police" : "civilian",
                static_cast<double>(hit.closing_speed_mps * 2.2369363f));
        if (damage_player(damage, "a car")) break;
    }
}

// Falling. The character controller owns `grounded`, so the landing speed is
// simply the downward speed on the last airborne step — sampled BEFORE the
// step that grounds them, because by then the solver has already zeroed it.
//
// Sampling after was the first version, and it read every landing as zero.
void App::check_player_fall_damage(bool was_grounded) {
    if (!on_foot_) {
        player_landing_speed_mps_ = 0.0f;
        return;
    }
    if (!player_character_.grounded) {
        player_landing_speed_mps_ =
            std::max(0.0f, -player_character_.velocity.y);
        return;
    }
    if (was_grounded) return;
    const float landed = player_landing_speed_mps_;
    player_landing_speed_mps_ = 0.0f;
    const float damage = fall_impact_damage(landed);
    if (damage <= 0.0f) return;
    AP_INFO("player landed at %.1f m/s", static_cast<double>(landed));
    damage_player(damage, "a fall");
}

// Crashing your own car. city/body_damage.h owns the curve and says why it is
// a different one from the pedestrian's: the driver is inside a car, and the
// car is already losing its own health to the same impact.
void App::check_player_crash_damage(float impact_speed_mps) {
    if (on_foot_ || in_aircraft_ || in_boat_) return;
    const float damage = crash_driver_damage(impact_speed_mps);
    if (damage <= 0.0f) return;
    damage_player(damage, "a crash");
}

// The jab, at the moment the arm is out.
//
// Reach and height are the fist's, not the pistol's: a punch is thrown from
// the shoulder at somebody standing in front of you, so it starts at chest
// height and stops a bit over an arm's length away. Using the eye ray and the
// weapon's range — which was the tempting reuse — lets a player deck somebody
// on the far side of the street by looking at them.
void App::throw_player_punch() {
    if (!on_foot_ || !player_vitals_.alive()) return;
    constexpr float kPunchReachM = 1.15f;
    const glm::vec3 chest = player_character_.position + glm::vec3{0.0f, 1.28f, 0.0f};
    const glm::vec3 facing = character_forward(player_character_.facing_yaw);
    const float length = glm::length(facing);
    if (!(length > 1e-4f)) return;
    const glm::vec3 direction = facing / length;

    // Cover still counts. A fist that reaches through a wall is the same bug
    // as a bullet that does, and the weapon path already refuses it.
    const auto cover = collider_.raycast(chest, direction, kPunchReachM);
    const float reach = cover.hit ? cover.distance : kPunchReachM;
    const auto body = world_.punch_ped(chest, direction, reach,
                                       static_cast<int64_t>(step_index_));
    if (!body.hit) return;

    ++weapon_body_hits_;
    weapon_hit_feedback_ = 0.25f;
    if (body.killed) ++weapon_kills_;
    if (body.officer) {
        wanted_.add_heat(body.killed ? kOfficerKilledHeat : kOfficerWoundedHeat,
                         WantedSystem::Crime::OfficerAssault);
        AP_INFO("punch %s police officer %llu/%u; wanted %d",
                body.killed ? "killed" : "connected with",
                static_cast<unsigned long long>(body.lane_key), body.slot,
                wanted_.level());
        return;
    }
    wanted_.add_heat(body.killed ? kCivilianKilledHeat : kCivilianWoundedHeat,
                     body.killed ? WantedSystem::Crime::Violent
                                 : WantedSystem::Crime::Assault);
    AP_INFO("punch %s pedestrian %llu/%u; wanted %d",
            body.killed ? "killed" : "connected with",
            static_cast<unsigned long long>(body.lane_key), body.slot,
            wanted_.level());
}

}  // namespace apricot
