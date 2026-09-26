#include "app/app.h"
#include "app/vehicle_model_tuning.h"
#include "core/log.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace apricot {
void App::enable_trailer_collision(bool enabled) {
    for(auto slot:trailer_colliders_)collider_.set_kinematic_enabled(slot,enabled);
}
void App::sync_trailer_collision() {
    for(std::size_t i=0;i<trailer_colliders_.size();++i) {
        const auto box=kTrailerBoxes[i];
        auto half=box.half;
        half.y=box.half.y*std::cos(trailer_.pitch)+box.half.z*std::fabs(std::sin(trailer_.pitch));
        half.z=box.half.z*std::cos(trailer_.pitch)+box.half.y*std::fabs(std::sin(trailer_.pitch));
        const auto centre=trailer_point(trailer_,box.centre);
        auto& slot=trailer_colliders_[i];
        if(slot==static_cast<std::size_t>(-1))slot=collider_.add_kinematic_oriented_box(centre,half,trailer_.yaw);
        else collider_.set_kinematic_oriented_box(slot,centre,half,trailer_.yaw);
        collider_.set_kinematic_vehicle(slot,true);
    }
    std::vector<glm::vec3> obstacles;
    for(const auto& parked:parked_vehicles_)obstacles.push_back(parked.state.position);
    // Multiple traffic avoidance points cover the long box, including its tail.
    for(float z:{-4.f,-2.f,0.f,2.f,4.f})obstacles.push_back(trailer_point(trailer_,{0,.4f,z}));
    world_.set_parked_vehicle_poses(std::move(obstacles));
}
void App::reset_freight_yard(bool add_tractor) {
    enable_trailer_collision(false);
    trailer_=spawn_trailer(collider_,kFreightTrailerHome,kFreightTrailerYaw);
    prev_trailer_=trailer_;
    if(add_tractor)spawn_freight_tractor();
    sync_trailer_collision();trailer_visual_.sync(scene_,trailer_,trailer_,0);
}
void App::spawn_freight_tractor() {
    const auto yard=spawn_trailer(collider_,kFreightTrailerHome,kFreightTrailerYaw);
    ParkedVehicle parked;
    car_visual_.clone_parked(scene_,parked.visual);
    parked.tuning=player_model_tuning(driving_mechanics_style_,PlayerCarId::HarrowHauler);
    // Cab faces out of the loading yard. Two metres of backing to the pin.
    const auto pin=trailer_point(yard,kTrailerKingpin);
    const auto pos=pin+glm::angleAxis(yard.yaw,glm::vec3{0,1,0})*glm::vec3{0,0,-3.9f};
    parked.state=spawn_vehicle(parked.tuning,collider_,pos.x,pos.z,yard.yaw);
    parked.visual.select(scene_,parked.tuning,parked.state,PlayerCarId::HarrowHauler);
    const auto bounds=parked.visual.placed_body_bounds();
    parked.collider=collider_.add_kinematic_oriented_box(
        parked.state.position+parked.state.orientation*bounds.center(),bounds.extents(),yard.yaw);
    collider_.set_kinematic_vehicle(parked.collider,true);
    parked_vehicles_.push_back(std::move(parked));
}
void App::drop_trailer() {
    if(!trailer_.attached)return;
    trailer_.attached=false;trailer_.blocked=false;prev_trailer_=trailer_;
    sync_trailer_collision();
}
void App::toggle_trailer() {
    if(on_foot_ || in_boat_ || in_helicopter_ || in_aircraft_ || vehicle_transition_.active() ||
       car_visual_.active_car()!=PlayerCarId::HarrowHauler)return;
    const auto notice=[&](const char* message){vehicle_interaction_notice_=message;vehicle_notice_until_=step_index_+300;};
    if(!trailer_stopped(car_)) {notice("Stop the truck before using the hitch");return;}
    if(trailer_.attached) {
        if(std::fabs(trailer_.pitch)>.10f) {notice("Find level ground to drop the trailer");return;}
        drop_trailer();notice("Trailer dropped - landing gear down");
        AP_INFO("trailer dropped at %.2f, %.2f",double(trailer_.position.x),double(trailer_.position.z));return;
    }
    switch(trailer_coupling(car_,tuning_,trailer_)) {
        case TrailerCoupling::Moving:notice("Stop the truck before using the hitch");return;
        case TrailerCoupling::Misaligned:notice("Straighten the truck with the trailer");return;
        case TrailerCoupling::OutOfReach:notice("Back the fifth wheel under the trailer pin");return;
        case TrailerCoupling::Unsupported:notice("Find level ground to couple the trailer");return;
        case TrailerCoupling::Ready:break;
    }
    enable_trailer_collision(false);collider_.set_kinematic_enabled(current_vehicle_collider_,false);
    auto next=follow_trailer(trailer_,tractor_hitch(car_,tuning_),collider_);
    const bool clear=trailer_clear(trailer_,next,collider_);
    if(clear) {next.attached=true;trailer_=next;prev_trailer_=trailer_;}
    sync_trailer_collision();
    if(!clear) {notice("Hitch path blocked");return;}
    notice("Trailer coupled - landing gear up");AP_INFO("trailer coupled");
}
void App::step_trailer() {
    if(trailer_.attached) {
        enable_trailer_collision(false);
        if(!step_tractor_trailer(trailer_,car_,prev_car_,tuning_,collider_) && !on_foot_) {
            vehicle_interaction_notice_="Trailer blocked - pull forward or straighten up";
            vehicle_notice_until_=step_index_+90;
        }
    }
    // Traffic gets its normal car-contact response against the full trailer.
    // Its parked brakes hold the box in place when unhitched.
    VehicleState proxy;proxy.position=trailer_.position+glm::vec3{0,.7f,0};
    proxy.orientation=glm::angleAxis(trailer_.yaw,glm::vec3{0,1,0});
    proxy.velocity=trailer_.attached?car_.velocity:glm::vec3{0};
    world_.resolve_traffic_collision(proxy,1.25f,5.f,9000.f,0.f);
    if(trailer_.attached && proxy.car_contact_speed>1.f) {
        car_.position=prev_car_.position;car_.orientation=prev_car_.orientation;
        car_.velocity={};car_.angular_velocity={};trailer_=prev_trailer_;
    }
    sync_trailer_collision();
}
void App::run_trailer_check() {
    if(step_index_<20 || trailer_check_ran_)return;
    trailer_check_ran_=true;
    const auto fail=[](const char* why){AP_ERROR("trailer check: %s",why);};
    // Use the real cargo yard, loaded assets, host action, suspension, collision
    // and render state. Unit tests separately exercise hard obstacle sweeps.
    drop_trailer();
    for(auto& parked:parked_vehicles_) {collider_.set_kinematic_enabled(parked.collider,false);parked.visual.destroy(scene_);}
    parked_vehicles_.clear();reset_freight_yard(false);
    tuning_=player_model_tuning(driving_mechanics_style_,PlayerCarId::HarrowHauler);
    const auto pin=trailer_point(trailer_,kTrailerKingpin);
    const auto p=pin+glm::angleAxis(trailer_.yaw,glm::vec3{0,1,0})*glm::vec3{0,0,-3.9f};
    collider_.set_kinematic_enabled(current_vehicle_collider_,false);
    enable_trailer_collision(false);
    car_=spawn_vehicle(tuning_,collider_,p.x,p.z,trailer_.yaw);prev_car_=car_;
    car_visual_.select(scene_,tuning_,car_,PlayerCarId::HarrowHauler);
    on_foot_=false;in_boat_=false;in_aircraft_=false;in_helicopter_=false;vehicle_transition_={};
    dev_menu_.set_player_car(PlayerCarId::HarrowHauler);
    sync_trailer_collision();
    car_.velocity={0,0,2};toggle_trailer();if(trailer_.attached){fail("moving attach accepted");return;}
    car_.velocity={};
    InputFrame reverse;reverse.brake=.15f;
    bool reached=false;
    for(int i=0;i<1200;++i) {
        prev_car_=car_;prev_trailer_=trailer_;
        car_=step_vehicle(car_,tuning_,reverse,collider_,1.f/120.f);step_trailer();
        const auto d=tractor_hitch(car_,tuning_)-trailer_point(trailer_,kTrailerKingpin);
        if(glm::length(glm::vec2{d.x,d.z})<.45f) {reached=true;break;}
    }
    if(!reached) {fail("cannot back underneath trailer");return;}
    InputFrame park;park.brake=1;park.handbrake=1;auto braking=tuning_;braking.arcade_reverse=false;
    for(int i=0;i<90;++i){prev_car_=car_;car_=step_vehicle(car_,braking,park,collider_,1.f/120.f);step_trailer();}
    toggle_trailer();if(!trailer_.attached){fail(vehicle_interaction_notice_.c_str());return;}
    const auto start=car_.position;InputFrame go;go.throttle=.7f;
    for(int i=0;i<360;++i) {
        prev_car_=car_;prev_trailer_=trailer_;enable_trailer_collision(false);
        car_=step_vehicle(car_,tuning_,go,collider_,1.f/120.f);step_trailer();
    }
    if(glm::distance(start,car_.position)<2.f){fail("rig cannot leave bay");return;}
    if(glm::distance(tractor_hitch(car_,tuning_),trailer_point(trailer_,kTrailerKingpin))>.03f){fail("hitch separated");return;}
    car_.velocity={};car_.angular_velocity={};toggle_trailer();if(trailer_.attached){fail("drop failed");return;}
    const auto parked=trailer_.position;
    const auto dropped=car_.position;
    for(int i=0;i<120;++i) {prev_car_=car_;car_=step_vehicle(car_,tuning_,go,collider_,1.f/120.f);step_trailer();}
    if(glm::distance(dropped,car_.position)<.5f || glm::distance(parked,trailer_.position)>.01f) {fail("cannot drive away from dropped trailer");return;}
    // Reposition only the tractor to the recorded coupling spot, then run the
    // real action again. The trailer stays at the spot the driving path left it.
    car_.position=dropped;car_.velocity={};car_.angular_velocity={};
    toggle_trailer();if(!trailer_.attached){fail("recouple failed");return;}
    toggle_trailer();if(trailer_.attached || glm::distance(parked,trailer_.position)>.1f){fail("parked trailer moved");return;}
    toggle_trailer();
    // Exercise the actual host save/load wiring without touching the user's
    // checkpoint. The temp file is removed even if a check fails.
    // The OS temp folder rather than /tmp, which Windows does not have.
    std::error_code temp_error;
    const std::string pattern=(std::filesystem::temp_directory_path(temp_error)/"apricot-trailer-check-XXXXXX").string();
    std::vector<char> checkpoint(pattern.begin(),pattern.end());checkpoint.push_back('\0');
    const int fd=temp_error?-1:mkstemp(checkpoint.data());if(fd<0){fail("cannot create temporary checkpoint");return;}close(fd);
    const auto original_path=save_path_;save_path_=checkpoint.data();
    const auto saved_pose=trailer_.position;
    bool restored=save_game();
    if(restored) {drop_trailer();car_.position.x+=20;restored=load_game();}
    restored=restored && trailer_.attached && glm::distance(saved_pose,trailer_.position)<.001f;
    if(restored) {
        toggle_trailer();restored=!trailer_.attached && save_game();
        if(restored){trailer_.position.x+=20;restored=load_game();}
        restored=restored && !trailer_.attached && glm::distance(saved_pose,trailer_.position)<.001f;
    }
    save_path_=original_path;std::remove(checkpoint.data());
    if(!restored){fail("attached/dropped checkpoint restore failed");return;}
    toggle_trailer();
    prev_car_=car_;prev_trailer_=trailer_;world_.fill(scene_,renderer_,car_.position);
    trailer_check_passed_=true;
    AP_INFO("trailer check passed: moving attach denied, back under pin, couple, drive, hitch holds, drop, drive away, recouple, parked pose holds, attached/dropped saves");
}
}
