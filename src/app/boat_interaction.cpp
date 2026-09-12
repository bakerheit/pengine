#include "app/app.h"
#include "city/marina.h"
#include "core/log.h"

namespace apricot {
void App::reset_boat() {
    boat_transition_={};
    boat_={};boat_.position={city::kMarlinMooring.x,0,city::kMarlinMooring.z};
    boat_.yaw=city::kMarlinYaw;prev_boat_=boat_;
    world_.sync_boat(scene_,collider_,boat_);
}
bool App::nearby_boat() const {
    if(!on_foot_ || !boat_in_boarding_range(boat_,player_character_.position)) return false;
    const auto local=glm::inverse(boat_rotation(boat_))*(player_character_.position-boat_.position);
    const auto from=player_character_.position+glm::vec3{0,.85f,0};
    auto edge=boat_point(boat_,{local.x<0?-1.22f:1.22f,0,-1.55f});
    edge.y=from.y;
    const auto d=edge-from;const float length=glm::length(d);
    const auto hit=length>.01f ? collider_.raycast(from,d/length,length):TerrainCollider::GroundHit{};
    return !hit.hit || hit.distance>=length-.05f;
}
bool App::toggle_boat() {
    if(boat_transition_.active()) return true;
    if(in_boat_) {
        if(!boat_stopped(boat_)) {
            vehicle_interaction_notice_="Stop beside a dock or shore to get out";
            vehicle_notice_until_=step_index_+240;return true;
        }
        bool placed=false;
        world_.enable_boat_collision(collider_,false);
        for(float side:{1.f,-1.f}) {
            for(float distance:{2.f,2.6f,3.1f}) {
                const auto p=boat_point(boat_,{side*distance,0,-1.55f});
                const auto support=boat_landing_support(collider_,p);
                if(!support.hit) continue;
                PlayerCharacterState trial{};trial.position=support.point;
                trial.view_yaw=trial.facing_yaw=-boat_.yaw;
                if(!character_position_clear(collider_,trial.position,character_tuning_)) continue;
                const auto from=boat_point(boat_,{side*1.22f,1.45f,-1.55f});
                const auto d=trial.position+glm::vec3{0,.85f,0}-from;
                const float length=glm::length(d);
                const auto hit=collider_.raycast(from,d/length,length);
                if(hit.hit && hit.distance<length-.05f) continue;
                boat_shore_pose_=trial;placed=true;break;
            }
            if(placed) break;
        }
        world_.enable_boat_collision(collider_,true);
        if(!placed) {
            vehicle_interaction_notice_="Pull alongside a clear dock or shore";
            vehicle_notice_until_=step_index_+240;return true;
        }
        boat_.speed=0;boat_.velocity={};boat_.throttle=0;prev_boat_=boat_;
        boat_transition_={VehicleTransitionDirection::Exit,0};
        character_visual_.prepare_boat_transition(boat_shore_pose_);
        boat_transition_camera_=camera_;
        character_look_dx_pending_=character_look_dy_pending_=0;camera_obstruction_distance_=-1;
        AP_INFO("player climbing out of Marlin");return true;
    }
    if(!nearby_boat()) return false;
    boat_.throttle=0;boat_.speed=0;boat_.velocity={};prev_boat_=boat_;
    boat_shore_pose_=player_character_;boat_shore_pose_.velocity={};
    player_character_=boat_shore_pose_;prev_player_character_=player_character_;
    boat_transition_={VehicleTransitionDirection::Enter,0};
    character_visual_.prepare_boat_transition(boat_shore_pose_);
    boat_transition_camera_=camera_;
    sync_current_vehicle_obstacle();
    character_look_dx_pending_=character_look_dy_pending_=0;camera_obstruction_distance_=-1;
    AP_INFO("player climbing aboard Marlin Sprint 22");return true;
}
void App::step_boat_transition() {
    if(!advance_boat_transition(boat_transition_)) return;
    const bool entering=boat_transition_.direction==VehicleTransitionDirection::Enter;
    if(!entering && !character_position_clear(collider_,boat_shore_pose_.position,character_tuning_)) {
        boat_transition_={};vehicle_interaction_notice_="Dock landing blocked; try again";
        vehicle_notice_until_=step_index_+240;return;
    }
    in_boat_=entering;on_foot_=!entering;
    if(entering) {
        vehicle_audio_.exit_vehicle();vehicle_audio_.set_model(city::kMarlinBody);
        vehicle_audio_.enter_vehicle(false,boat_.position);
    } else {
        player_character_=boat_shore_pose_;prev_player_character_=player_character_;
        vehicle_audio_.exit_vehicle();
    }
    boat_transition_={};
    transition_camera_=camera_;transition_camera_release_=.45f;
    sync_current_vehicle_obstacle();
    AP_INFO("Marlin animation complete: %s",entering?"seated at helm":"back on dock");
}
void App::run_boat_check() {
    if(boat_check_ran_ || step_index_<20) return;
    boat_check_ran_=true;
    const auto fail=[](const char* reason){AP_ERROR("boat check: %s",reason);};
    const auto finish_animation=[&](){
        for(uint32_t i=0;i<kBoatTransitionTicks && boat_transition_.active();++i) step_boat_transition();
    };
    reset_boat();on_foot_=true;in_aircraft_=false;in_helicopter_=false;in_boat_=false;
    player_character_={};player_character_.position=boat_point(boat_,{2.6f,city::kMarinaDeckTop,-1.55f});
    prev_player_character_=player_character_;
    if(!nearby_boat()) {fail("dock boarding unreachable");return;}
    toggle_player_mode();
    if(in_boat_ || !boat_transition_.active()) {fail("boarding skipped animation");return;}
    finish_animation();if(!in_boat_) {fail("boarding failed");return;}
    toggle_player_mode();finish_animation();if(in_boat_ || !on_foot_) {fail("dock exit failed");return;}
    toggle_player_mode();finish_animation();if(!in_boat_) {fail("reboarding failed");return;}
    const auto home=boat_;
    InputFrame go{};go.throttle=1;
    world_.enable_boat_collision(collider_,false);
    for(int i=0;i<900;++i) boat_=step_boat(boat_,go,collider_,1.f/120.f);
    world_.sync_boat(scene_,collider_,boat_);world_.enable_boat_collision(collider_,true);
    if(glm::distance(home.position,boat_.position)<25.f || boat_.blocked) {fail("cannot leave berth");return;}
    toggle_player_mode();if(!in_boat_) {fail("moving exit allowed");return;}
    const float yaw=boat_.yaw;go.steer=.6f;
    world_.enable_boat_collision(collider_,false);
    for(int i=0;i<90;++i) boat_=step_boat(boat_,go,collider_,1.f/120.f);
    if(std::fabs(boat_.yaw-yaw)<.1f) {world_.enable_boat_collision(collider_,true);fail("no steering");return;}
    InputFrame stop{};stop.handbrake=1;
    for(int i=0;i<600;++i) boat_=step_boat(boat_,stop,collider_,1.f/120.f);
    world_.sync_boat(scene_,collider_,boat_);world_.enable_boat_collision(collider_,true);
    if(!boat_stopped(boat_)) {fail("cannot stop");return;}
    toggle_player_mode();if(!in_boat_) {fail("open-water exit allowed");return;}
    const auto offshore=boat_;
    reset_boat();toggle_player_mode();finish_animation();
    if(in_boat_ || !on_foot_) {fail("recovered boat cannot exit onto dock");return;}
    toggle_player_mode();finish_animation();if(!in_boat_) {fail("recovered boat cannot reboard");return;}
    boat_=offshore;
    // Leave the bounded preview underway after checking the safe exit gates.
    world_.enable_boat_collision(collider_,false);go.steer=0;
    for(int i=0;i<180;++i) boat_=step_boat(boat_,go,collider_,1.f/120.f);
    world_.sync_boat(scene_,collider_,boat_);world_.enable_boat_collision(collider_,true);
    prev_boat_=boat_;world_.fill(scene_,renderer_,boat_.position);
    boat_check_passed_=true;
    AP_INFO("boat check passed: dock boarding/exit/re-entry, depart, steer, stop, recovery, unsafe exits denied");
}
} // namespace apricot
