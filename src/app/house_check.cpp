#include "app/app.h"
#include "city/residential_neighborhood.h"
#include "core/log.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace apricot {
namespace {
// Deliberately drives the normal character-input/door/collision path. Only
// initial placement is a warp; every subsequent doorway is crossed on foot.
constexpr std::array<glm::vec2,29> kHouseCheckRoute{{
    {0,5.1f},{0,1},{-3,1},{-3,-2.5f},{-3,-3.5f},{-3,1},
    {0,1},{0,-2.7f},{0,1},{3,1},{3,-1.7f},{2.3f,-3.8f},
    // Return around the swung leaf's free end, not through its 95-degree stop.
    {2.8f,-2.7f},{2.8f,-1.7f},{3,1},{0,1},{0,7},{0,10},{0,7},{0,1},
    {-3,0},{-8,0},{-12,0},{-12,-10},{-12,0},{-8,0},{-3,0},
    {0,1},{0,8.7f}
}};
}

InputFrame App::house_check_input() {
    const auto& site=city::kResidentialHouses[city::kResidentialTargetHouse].site;
    if(!house_check_started_) {
        const auto at=city::residential_world(site,{0,8.7f});
        player_character_=spawn_character(collider_,at.x,at.y,
            std::atan2(-site.sin_yaw,site.cos_yaw));
        prev_player_character_=player_character_;
        house_check_started_=true;
        camera_obstruction_distance_=-1;
        AP_INFO("house check: placed at front porch; all travel after this is normal walking");
    }
    InputFrame input;
    if(house_check_failed_ || house_check_complete_)return input;
    // Show the closed front door without triggering a proximity opener.
    if(house_check_stage_==0 && house_check_ticks_<120)return input;
    const auto goal=city::residential_world(site,kHouseCheckRoute[house_check_stage_]);
    const glm::vec2 delta{goal.x-player_character_.position.x,goal.y-player_character_.position.z};
    input.look_dx=std::atan2(delta.x,-delta.y)-player_character_.view_yaw;
    input.throttle=std::min(1.f,glm::length(delta)/(character_tuning_.walk_speed_mps*static_cast<float>(kSimDt)));
    return input;
}

void App::update_house_check() {
    if(!house_check_started_ || house_check_failed_ || house_check_complete_)return;
    ++house_check_ticks_;
    const auto& states=world_.house_door_states();
    if(states.size()!=5u) {
        AP_ERROR("house check: expected five live door leaves");house_check_failed_=true;return;
    }
    if(house_check_stage_==0 && house_check_ticks_==100) {
        for(const auto& state:states)if(std::abs(state.angle)>.001f) {
            AP_ERROR("house check: standing nearby opened a door");house_check_failed_=true;return;
        }
        house_check_capture_="front-closed";
    }
    for(std::size_t i=0;i<states.size();++i) {
        const unsigned bit=1u<<static_cast<unsigned>(i);
        if(std::abs(states[i].angle)>.25f && !(house_check_opened_&bit)) {
            house_check_opened_|=bit;
            house_check_capture_="door-"+std::to_string(i)+"-pushed";
        }
    }
    if(!character_position_clear(collider_,player_character_.position,character_tuning_)) {
        AP_ERROR("house check: actor overlaps collision at stage %zu",house_check_stage_);
        house_check_failed_=true;return;
    }
    const auto& site=city::kResidentialHouses[city::kResidentialTargetHouse].site;
    const auto goal=city::residential_world(site,kHouseCheckRoute[house_check_stage_]);
    const auto delta=glm::vec2{player_character_.position.x-goal.x,player_character_.position.z-goal.y};
    if(glm::length(delta)<.06f) {
        AP_INFO("house check: walked stage %zu to local %.2f %.2f",house_check_stage_,
            kHouseCheckRoute[house_check_stage_].x,kHouseCheckRoute[house_check_stage_].y);
        if(house_check_stage_==0)house_check_capture_="front-entered";
        if(house_check_stage_==3)house_check_capture_="study-entered";
        if(house_check_stage_==7)house_check_capture_="bath-entered";
        if(house_check_stage_==10)house_check_capture_="bedroom-entered";
        if(house_check_stage_==21)house_check_capture_="garden-exited";
        if(house_check_stage_==26)house_check_capture_="garden-reentered";
        ++house_check_stage_;house_check_ticks_=0;
        if(house_check_stage_==kHouseCheckRoute.size()) {
            if(house_check_opened_!=31u) {
                AP_ERROR("house check: route finished without pushing every leaf");
                house_check_failed_=true;return;
            }
            house_check_complete_=true;
            house_check_capture_="front-exited";
            AP_INFO("house check: PASS front/interior/garden routes and both exits");
        }
    } else if(house_check_ticks_>2400) {
        const auto local=city::access_local(site,{player_character_.position.x,player_character_.position.z});
        AP_ERROR("house check: blocked at stage %zu, local %.3f %.3f",house_check_stage_,local.x,local.y);
        for(std::size_t i=0;i<states.size();++i)
            AP_ERROR("house check door %zu angle %.2f degrees",i,glm::degrees(states[i].angle));
        house_check_capture_="blocked";
        house_check_failed_=true;
    }
}

void App::capture_house_check() {
    if(house_check_capture_.empty())return;
    if(!screenshot_path_.empty() && !save_screenshot(screenshot_path_+"."+house_check_capture_+".bmp"))
        house_check_failed_=true;
    house_check_capture_.clear();
}
} // namespace apricot
