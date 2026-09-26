#include "app/app.h"
#include "app/weapon_aim.h"
#include "core/log.h"

#include <SDL.h>
#include <limits>

namespace apricot {

// --damage-check: the whole of city/body_damage.h, in the real game.
//
// The suites prove the arithmetic and the state machines headlessly. They
// cannot tell you that a corpse is lying on the pavement in the fall pose
// rather than standing back up in a walk cycle, or that WASTED is up for the
// three seconds the player is actually dead rather than over a world they are
// already driving around in. Both of those are things a player SEES, and both
// of them were broken at some point in this change while every test passed.
//
// Stages, from a real spawned pedestrian and a real police round:
//   1. three rounds into a civilian: wound, wound, kill
//   2. the body is still on the ground several seconds later
//   3. the player is killed: WASTED, control frozen, then a respawn
void App::tick_damage_check() {
    if (frames_rendered_ < 600 || damage_check_failed_ || damage_check_done_)
        return;
    const auto fail = [&](const char* reason) {
        damage_check_failed_ = true;
        AP_ERROR("damage check: %s (stage %d)", reason, damage_check_stage_);
    };

    // Put the player somewhere a clear shot at `ped` exists, five metres out.
    // Used to pick the fixture AND to re-stage mid-check, because the victim
    // RUNS once wounded and will otherwise put a wall between you and them —
    // which is the behaviour under test, not a reason for the check to give up.
    // The check never inserts an NPC and never injures one directly.
    const auto stand_where_you_can_shoot = [&](const PedAgent& ped) -> bool {
        for (int side = 0; side < 8; ++side) {
            const float angle =
                static_cast<float>(side) * glm::quarter_pi<float>();
            const glm::vec3 proposed =
                ped.pos + glm::vec3{std::cos(angle) * 5.f, 0, std::sin(angle) * 5.f};
            auto placed = spawn_character(collider_, proposed.x, proposed.z);
            if (!character_position_clear(collider_, placed.position,
                                          character_tuning_))
                continue;
            const auto aim = weapon_aim_toward(placed.position,
                                               ped.pos + glm::vec3{0, 1.05f, 0});
            if (!aim.reachable) continue;
            const auto camera =
                weapon_aim_camera(placed.position, aim.yaw, aim.pitch, 1.f);
            const float distance =
                glm::distance(camera.eye, ped.pos + glm::vec3{0, 1.05f, 0});
            const auto wall =
                collider_.raycast(camera.eye, camera.direction, distance + .5f);
            const auto seen = world_.traffic().raycast_ped(
                camera.eye, camera.direction,
                wall.hit ? wall.distance : distance + .5f);
            if (!seen.hit || seen.lane_key != ped.lane_key ||
                seen.slot != ped.slot)
                continue;
            placed.view_yaw = placed.facing_yaw = aim.yaw;
            placed.view_pitch = aim.pitch;
            player_character_ = prev_player_character_ = placed;
            camera_obstruction_distance_ = -1.f;
            return true;
        }
        return false;
    };

    // Stage 0: pick the nearest untouched person we can get a clear shot at.
    if (damage_check_stage_ == 0) {
        const PedAgent* chosen = nullptr;
        float nearest = std::numeric_limits<float>::max();
        for (const auto& ped : world_.traffic().peds()) {
            if (ped_is_floored(ped.activity) || ped.health < kBodyHealth) continue;
            const float proximity =
                glm::distance(ped.pos, player_character_.position);
            if (proximity > 70.f || proximity >= nearest) continue;
            nearest = proximity;
            chosen = &ped;
        }
        if (chosen == nullptr || !stand_where_you_can_shoot(*chosen)) {
            // Not a failure yet: the streamer may simply have nobody reachable
            // in front of the player on this frame. Keep looking.
            if (frames_rendered_ > 1500) fail("no visible supported pedestrian fixture");
            return;
        }
        damage_check_lane_ = chosen->lane_key;
        damage_check_slot_ = chosen->slot;
        // QA hands itself the pistol; buying one is --gun-store-check's job.
        economy_.owned_weapons |= weapon_bit(WeaponId::Pistol);
        weapon_wheel_.equipped = WeaponId::Pistol;
        weapon_aim_toggle_ = true;
        damage_check_stage_ = 1;
        damage_check_stage_frame_ = frames_rendered_;
        AP_INFO("damage check: using real pedestrian %llu/%u at %.1f m",
                static_cast<unsigned long long>(damage_check_lane_),
                damage_check_slot_, static_cast<double>(nearest));
        return;
    }

    const auto victim = [&]() -> const PedAgent* {
        for (const auto& ped : world_.traffic().peds())
            if (ped.lane_key == damage_check_lane_ && ped.slot == damage_check_slot_)
                return &ped;
        return nullptr;
    };
    const int held = frames_rendered_ - damage_check_stage_frame_;

    // Stage 1: let the pistol finish being drawn, then land three rounds on ONE
    // person.
    //
    // Re-aimed every frame and gated on the shot actually reaching the intended
    // body, because a wounded person RUNS — that is the entire point of the
    // wound path — and the first version of this check simply fired on a timer
    // at the place the victim used to be. It put round one into the target,
    // round two into a bystander who had walked into the line, and round three
    // into a wall, and then reported that three rounds do not kill anybody.
    if (damage_check_stage_ == 1) {
        if (held < 45) return;
        const PedAgent* target = victim();
        if (target == nullptr) { fail("the pedestrian under test was retired"); return; }
        if (held > 1500) { fail("could not land three rounds on one person"); return; }

        // ROUNDS THAT LANDED, counted off the victim's health rather than off
        // the clicks pushed. A click arriving while the pistol is on its fire
        // interval is consumed and spends nothing, so counting presses reported
        // three rounds when two had hit — and the check then blamed the damage
        // model for a body that was still standing.
        if (target->health < damage_check_last_health_) {
            ++damage_check_rounds_;
            damage_check_last_health_ = target->health;
        }
        if (target->activity == PedActivity::Dead) {
            damage_check_stage_ = 2;
            damage_check_stage_frame_ = frames_rendered_;
            return;
        }

        const glm::vec3 torso = target->pos + glm::vec3{0, 1.05f, 0};
        const auto aim = weapon_aim_toward(player_character_.position, torso);
        if (!aim.reachable) return;
        player_character_.view_yaw = player_character_.facing_yaw = aim.yaw;
        player_character_.view_pitch = aim.pitch;
        prev_player_character_ = player_character_;
        update_camera(0.f);

        // Would this round reach the person it is meant for? Same two queries
        // the real fire path makes, in the same order: world cover first, then
        // the nearest body on what is left of the ray.
        const auto camera = weapon_aim_camera(player_character_.position,
                                              aim.yaw, aim.pitch, 1.f);
        const float distance = glm::distance(camera.eye, torso);
        const auto wall = collider_.raycast(camera.eye, camera.direction,
                                            distance + .5f);
        const auto seen = world_.traffic().raycast_ped(
            camera.eye, camera.direction,
            wall.hit ? wall.distance : distance + .5f);
        if (!seen.hit || seen.lane_key != damage_check_lane_ ||
            seen.slot != damage_check_slot_) {
            // Blocked. Follow them: re-stage no more than once every half
            // second so the check does not thrash the player around the city.
            if (frames_rendered_ - damage_check_last_restage_frame_ >= 30) {
                damage_check_last_restage_frame_ = frames_rendered_;
                stand_where_you_can_shoot(*target);
            }
            return;
        }
        // Clear of WeaponUseState::kFireInterval since the last round.
        if (frames_rendered_ - damage_check_last_shot_frame_ < 30) return;
        damage_check_last_shot_frame_ = frames_rendered_;
        SDL_Event down{};
        down.type = SDL_MOUSEBUTTONDOWN;
        down.button.button = SDL_BUTTON_LEFT;
        SDL_PushEvent(&down);
        SDL_Event up{};
        up.type = SDL_MOUSEBUTTONUP;
        up.button.button = SDL_BUTTON_LEFT;
        SDL_PushEvent(&up);
        return;
    }

    // Stage 2: the person is dead, and it took exactly three rounds to do it.
    if (damage_check_stage_ == 2) {
        const PedAgent* target = victim();
        if (target == nullptr) { fail("the pedestrian under test was retired"); return; }
        if (target->activity != PedActivity::Dead) {
            fail("a dead pedestrian stopped being dead");
            return;
        }
        if (damage_check_rounds_ != 3) {
            fail("a civilian did not take exactly three pistol rounds to kill");
            return;
        }
        if (held < 30) return;
        damage_check_body_ = target->pos;
        damage_check_stage_ = 3;
        damage_check_stage_frame_ = frames_rendered_;
        AP_INFO("damage check: pedestrian killed by %d rounds, wanted %d",
                damage_check_rounds_, wanted_.level());
        return;
    }

    // Stage 3: EIGHT SECONDS LATER the body is still there. The old knockdown
    // expired after at most seven and a half and the victim stood up, so this
    // window is chosen to be past it.
    if (damage_check_stage_ == 3) {
        const PedAgent* target = victim();
        if (target == nullptr) { fail("the body was retired while the player stood over it"); return; }
        if (target->activity != PedActivity::Dead) {
            fail("a body got up off the pavement");
            return;
        }
        if (held < 480) return;
        if (glm::distance(target->pos, damage_check_body_) > 0.05f) {
            fail("a settled body crept along the pavement");
            return;
        }
        damage_check_stage_ = 4;
        damage_check_stage_frame_ = frames_rendered_;
        AP_INFO("damage check: body still down after 8 s at %.1f %.1f",
                static_cast<double>(target->pos.x),
                static_cast<double>(target->pos.z));
        return;
    }

    // Stage 4: kill the PLAYER, through the same door every other cause uses,
    // and watch what being dead looks like.
    if (damage_check_stage_ == 4) {
        damage_check_death_position_ = player_character_.position;
        damage_player(kBodyHealth * 2.0f, "the damage check");
        if (player_vitals_.alive()) { fail("the player survived a lethal blow"); return; }
        damage_check_stage_ = 5;
        damage_check_stage_frame_ = frames_rendered_;
        return;
    }

    // Stage 5: dead means dead. Wanted cleared, pistol holstered, and — the
    // part that was broken — the player does not move while the banner is up.
    if (damage_check_stage_ == 5) {
        if (!player_vitals_.alive()) {
            if (wanted_.level() != 0) { fail("dying did not clear the pursuit"); return; }
            if (weapon_wheel_.equipped != WeaponId::Unarmed) {
                fail("a dead player is still holding the pistol");
                return;
            }
            if (glm::distance(player_character_.position,
                              damage_check_death_position_) > 1.0f) {
                fail("a dead player moved");
                return;
            }
            if (held > 600) { fail("the death never ended"); return; }
            return;
        }
        // Revived. Whole, and back in the world.
        if (player_vitals_.health != kBodyHealth) {
            fail("the respawned player is not at full health");
            return;
        }
        if (held < static_cast<int>(kPlayerDeathSeconds * 55.0f)) {
            fail("the respawn came before the death finished");
            return;
        }
        damage_check_stage_ = 6;
        damage_check_stage_frame_ = frames_rendered_;
        damage_check_done_ = true;
        AP_INFO("damage check: PASS; three rounds killed a civilian, the body "
                "stayed down 8 s, the player died, froze and respawned whole");
        return;
    }
}

void App::capture_damage_check() {
    if (damage_check_failed_) return;
    const int held = frames_rendered_ - damage_check_stage_frame_;
    const auto shot = [&](const char* suffix) {
        if (!save_screenshot(screenshot_path_ + suffix)) damage_check_failed_ = true;
    };
    if (damage_check_stage_ == 2 && held == 20) shot(".killed.png");
    if (damage_check_stage_ == 3 && held == 470) shot(".body-8s.png");
    if (damage_check_stage_ == 5 && held == 90) shot(".wasted.png");
    if (damage_check_stage_ == 6 && held == 30) shot(".respawned.png");
}

}  // namespace apricot
