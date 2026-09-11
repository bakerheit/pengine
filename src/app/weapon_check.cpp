#include "app/app.h"
#include "app/weapon_aim.h"
#include "core/log.h"

#include <SDL.h>
#include <limits>

namespace apricot {

void App::tick_weapon_hit_check() {
    if (frames_rendered_<600 || weapon_hit_check_failed_ || weapon_hit_check_done_) return;
    const auto key=[&](SDL_Keycode code,bool down) {
        SDL_Event event{};event.type=down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.keysym.sym=code;event.key.keysym.scancode=SDL_GetScancodeFromKey(code);
        SDL_PushEvent(&event);
    };
    const auto click=[&](bool down) {
        SDL_Event event{};event.type=down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.button=SDL_BUTTON_LEFT;SDL_PushEvent(&event);
    };
    const auto fail=[&](const char* reason) {
        weapon_hit_check_failed_=true;AP_ERROR("weapon hit check: %s",reason);
    };
    if (frames_rendered_==600) {
        // Use a real spawned pedestrian and an unobstructed, supported nearby
        // player position. The check never inserts or directly injures an NPC.
        float nearest=std::numeric_limits<float>::max();
        PlayerCharacterState setup;
        for (const auto& ped:world_.traffic().peds()) {
            if (ped_is_floored(ped.activity)) continue;
            const float proximity=glm::distance(ped.pos,player_character_.position);
            if (proximity>70.f || proximity>=nearest) continue;
            for (int side=0;side<8;++side) {
                const float angle=static_cast<float>(side)*glm::quarter_pi<float>();
                const glm::vec3 proposed=ped.pos+glm::vec3{std::cos(angle)*6.f,0,std::sin(angle)*6.f};
                auto placed=spawn_character(collider_,proposed.x,proposed.z);
                if (!character_position_clear(collider_,placed.position,character_tuning_)) continue;
                const auto aim=weapon_aim_toward(placed.position,ped.pos+glm::vec3{0,1.05f,0});
                if (!aim.reachable) continue;
                const auto camera=weapon_aim_camera(placed.position,aim.yaw,aim.pitch,1.f);
                const float distance=glm::distance(camera.eye,ped.pos+glm::vec3{0,1.05f,0});
                const auto wall=collider_.raycast(camera.eye,camera.direction,distance+.5f);
                const auto seen=world_.traffic().raycast_ped(camera.eye,camera.direction,
                    wall.hit ? wall.distance : distance+.5f);
                if (!seen.hit || seen.lane_key!=ped.lane_key || seen.slot!=ped.slot) continue;
                placed.view_yaw=placed.facing_yaw=aim.yaw;placed.view_pitch=aim.pitch;
                setup=placed;nearest=proximity;
                weapon_hit_check_lane_=ped.lane_key;weapon_hit_check_slot_=ped.slot;
                break;
            }
        }
        if (!std::isfinite(nearest) || nearest==std::numeric_limits<float>::max()) {
            fail("no visible supported pedestrian fixture");return;
        }
        player_character_=prev_player_character_=setup;
        camera_obstruction_distance_=-1.f;
        weapon_hit_check_start_hits_=weapon_body_hits_;
        AP_INFO("weapon hit check: using real pedestrian %llu/%u",
            static_cast<unsigned long long>(weapon_hit_check_lane_),weapon_hit_check_slot_);
    }
    if (frames_rendered_==601) key(SDLK_q,true);
    if (frames_rendered_==602) key(SDLK_q,false);
    if (frames_rendered_>=603 && frames_rendered_<=650) {
        bool found=false;
        for (const auto& ped:world_.traffic().peds()) {
            if (ped.lane_key!=weapon_hit_check_lane_ || ped.slot!=weapon_hit_check_slot_) continue;
            const auto aim=weapon_aim_toward(player_character_.position,ped.pos+glm::vec3{0,1.05f,0});
            player_character_.view_yaw=player_character_.facing_yaw=aim.yaw;
            player_character_.view_pitch=aim.pitch;
            prev_player_character_=player_character_;found=true;break;
        }
        if (!found) { fail("target streamed out before shot");return; }
    }
    if (frames_rendered_==650) {
        if (!weapon_aim_toggle_ || weapon_use_.aim_blend<.99f) {
            fail("Q did not engage aim");return;
        }
        click(true);
    }
    // The previous rendered view deliberately disagrees with the shot tick.
    // A flick-and-click must use current aim, not the prior frame's camera.
    if (frames_rendered_==649) {
        player_character_.view_yaw+=.6f;
        prev_player_character_=player_character_;
    }
    if (frames_rendered_==651) click(false);
    if (frames_rendered_==730) {
        if (weapon_visual_.blood_particle_count()!=0) { fail("blood did not expire");return; }
        // Aim at nearby ground for a world-only hit: no second blood burst.
        player_character_.view_pitch=-.9f;
        prev_player_character_=player_character_;update_camera(0.f);
    }
    if (frames_rendered_==732) click(true);
    if (frames_rendered_==733) click(false);
    if (frames_rendered_==750) key(SDLK_q,true);
    if (frames_rendered_==751) key(SDLK_q,false);
    if (frames_rendered_==800) {
        if (weapon_body_hits_!=weapon_hit_check_start_hits_+1 || weapon_shots_!=4 ||
            weapon_visual_.blood_particle_count()!=0 || weapon_aim_toggle_ || weapon_use_.aim_blend>.01f) {
            fail("body/world hit separation or Q aim release failed");return;
        }
        weapon_hit_check_done_=true;
        AP_INFO("weapon hit check: PASS; Q aim, real NPC wound/blood, expiry, ground hit, Q release");
    }
}

void App::capture_weapon_hit_check() {
    if (weapon_hit_check_failed_) return;
    if (frames_rendered_==645 && !save_screenshot(screenshot_path_+".aim-target.png"))
        weapon_hit_check_failed_=true;
    if (frames_rendered_==653) {
        // ONE ROUND IS A WOUND, not a knockdown: the check asks whether the
        // bullet SPENT HEALTH, which is what a body hit does now. It used to
        // ask whether the victim was on the floor, and a single round no
        // longer puts anybody there.
        bool wounded=false;
        for (const auto& ped:world_.traffic().peds())
            if (ped.lane_key==weapon_hit_check_lane_ && ped.slot==weapon_hit_check_slot_)
                wounded=ped.health<kBodyHealth;
        if (!wounded || weapon_body_hits_!=weapon_hit_check_start_hits_+1 ||
            weapon_visual_.blood_particle_count()==0 ||
            !save_screenshot(screenshot_path_+".body-hit.png")) {
            weapon_hit_check_failed_=true;
            AP_ERROR("weapon hit check: shot missed expected NPC or blood render was empty");
        }
    }
    if (frames_rendered_==662 && !save_screenshot(screenshot_path_+".blood-fall.png"))
        weapon_hit_check_failed_=true;
    if (frames_rendered_==710 && !save_screenshot(screenshot_path_+".body-fall.png"))
        weapon_hit_check_failed_=true;
}

} // namespace apricot
