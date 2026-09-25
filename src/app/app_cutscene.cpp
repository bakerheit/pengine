#include "app/app.h"
#include "app/vehicle_model_tuning.h"
#include "core/log.h"
#include "city/start_area.h"
#include "game/ui_canvas.h"
#include "game/delivery_mission.h"
#include "gfx/gl_state.h"
#include "terrain/heightmap.h"
#include <glad/gl.h>
#include <string_view>

namespace apricot {
void App::begin_new_game() {
    snow_clearance_ = {};
    snowplow_service_.reset();
    // Load the complete production scene before changing session state.
    std::string error;
    if (!opening_cutscene_.start(scene_,renderer_,error)) {
        save_notice_="Could not start the opening: "+error;
        AP_ERROR("%s",save_notice_.c_str());ui_.show_title();return;
    }
    traffic_visual_.clear_vehicles(scene_);
    delivery_cutscene_=false;
    mission_stage_=MissionStage::Opening;save_notice_.clear();
    game_ui_.clear_waypoint();
    bank_vault_={};world_.reset_session_objects(scene_,collider_);
    wanted_.reset();
    police_escalation_.reset();
    police_stop_feedback_s_=0.0f;
    traffic_visual_.reset_signals(scene_,collider_);
    weapon_wheel_={};dev_menu_={};step_index_=0;seed_=new_game_seed_;
    // A new game starts in a city that is not on fire. The fire is world
    // state and deliberately survives a death (see player_damage.cpp), so it
    // has to be put out HERE, where the world itself is being replaced —
    // together with the loop voice holding its crackle open, which would
    // otherwise go on playing at a spot on a map that no longer exists.
    molotov_use_={};molotov_shots_.clear();fire_.clear();
    fire_player_damage_timer_=0.f;molotov_throws_=0;molotov_fires_lit_=0;
    if (fire_voice_.valid()) {
        audio_device_.mixer().close_loop(fire_voice_);
        fire_voice_={};
    }
    for (auto& parked:parked_vehicles_) {
        parked.visual.destroy(scene_);collider_.set_kinematic_enabled(parked.collider,false);
    }
    parked_vehicles_.clear();world_.set_parked_vehicle_poses({});
    repair_shop_visit_={};repair_shop_feedback_s_=0;
    reset_respray_state();
    mission_success_feedback_s_=0;
    vehicle_interaction_notice_.clear();vehicle_notice_until_=0;
    driving_mechanics_style_=DrivingMechanicsStyle::ClassicGta;
    tuning_=player_model_tuning(driving_mechanics_style_,start_car_);
    car_visual_.select(scene_,tuning_,car_,start_car_);
    dev_menu_.set_player_car(start_car_);
    on_foot_=true;in_boat_=false;in_aircraft_=false;in_helicopter_=false;
    reset_boat();reset_aircraft();
    reset_freight_yard(true);
    teleport({start_position_.x,0,start_position_.y},start_heading_radians_);
    sync_current_vehicle_obstacle();
    update_weather();
    intro_audio_.stop(audio_device_.mixer());vehicle_audio_.stop();
    police_siren_.stop();traffic_idle_audio_.stop();traffic_horn_audio_.stop();city_audio_.stop();
    vehicle_leak_warning_.stop(audio_device_.mixer());
    camera_=opening_cutscene_.camera(window_.aspect());
    world_.fill(scene_,renderer_,camera_.position);
    opening_cutscene_.sync();scene_.update();
    ui_.enter_game();input_.set_ui_mode(true);input_.consume_edges();clock_.reset();
    AP_INFO("opening started: real scene, 19 voice cues, dialogue gestures and delivery handoff");
}
bool App::begin_delivery_cutscene() {
    if (mission_stage_!=MissionStage::DeliveryActive ||
        !delivery_contact(player_character_.position,on_foot_)) return false;
    std::string error;
    if (!opening_cutscene_.start(scene_,renderer_,error,"cutscenes/mission1-delivery.cutscene")) {
        vehicle_interaction_notice_="Could not start delivery: "+error;
        vehicle_notice_until_=step_index_+600;
        AP_ERROR("%s",vehicle_interaction_notice_.c_str());return false;
    }
    traffic_visual_.clear_vehicles(scene_);
    delivery_cutscene_=true;
    vehicle_interaction_notice_.clear();mission_success_feedback_s_=0;
    intro_audio_.stop(audio_device_.mixer());vehicle_audio_.stop();
    police_siren_.stop();traffic_idle_audio_.stop();traffic_horn_audio_.stop();city_audio_.stop();
    vehicle_leak_warning_.stop(audio_device_.mixer());
    camera_=opening_cutscene_.camera(window_.aspect());
    world_.fill(scene_,renderer_,camera_.position);
    opening_cutscene_.sync();scene_.update();
    input_.set_ui_mode(true);input_.consume_edges();clock_.reset();
    AP_INFO("Mission 1 delivery cutscene started; package completion pending");
    return true;
}
void App::finish_opening() {
    const auto position=opening_cutscene_.handoff_position();
    const float yaw=opening_cutscene_.handoff_yaw();
    opening_cutscene_.stop();on_foot_=true;in_boat_=false;in_aircraft_=false;in_helicopter_=false;
    player_character_=spawn_character(collider_,position.x,position.z,yaw);
    player_character_.view_yaw=yaw;prev_player_character_=player_character_;
    character_look_dx_pending_=character_look_dy_pending_=0;
    if (delivery_cutscene_) {
        // Natural completion and skip commit the same checkpoint, after assets loaded.
        complete_delivery(mission_stage_,player_character_.position,on_foot_);
        vehicle_interaction_notice_="PACKAGE DELIVERED TO DEVON";
        vehicle_notice_until_=step_index_+static_cast<uint64_t>(std::ceil(5.0/kSimDt));
        VoiceParams success;success.category=Category::Music;
        success.gain=.72f;success.spatial=false;
        audio_device_.mixer().play_oneshot(&audio_device_.bank().mission_success,success);
        mission_success_feedback_s_=6.25f;
    } else mission_stage_=MissionStage::DeliveryNeedsCar;
    city_audio_.start(audio_device_.mixer(),audio_device_.bank());
    vehicle_audio_.start(audio_device_.mixer(),audio_device_.bank());
    vehicle_audio_.set_model(player_car_definition(car_visual_.active_car()).mesh_path);
    traffic_idle_audio_.start(audio_device_.mixer(),audio_device_.bank());
    traffic_horn_audio_.start(audio_device_.mixer(),audio_device_.bank());
    police_siren_.start(audio_device_.mixer());
    chase_camera_.reset();camera_obstruction_distance_=-1;
    input_.consume_edges();input_.set_ui_mode(false);clock_.reset();
    sync_current_vehicle_obstacle();world_.fill(scene_,renderer_,player_character_.position);
    update_camera(0);
    // Automated captures must never replace the player's real checkpoint.
    if (frame_limit_==0) save_game();
    if (delivery_cutscene_) {
        AP_INFO("Mission 1 delivery complete: package received, control restored");
        delivery_cutscene_=false;return;
    }
    AP_INFO("opening complete: control returned to Johnny at %.2f, %.2f, %.2f; enter-car objective active",
        static_cast<double>(player_character_.position.x),static_cast<double>(player_character_.position.y),
        static_cast<double>(player_character_.position.z));
}
// Exercise the actual SDL input and gameplay routing with a disposable bounded run.
void App::tick_delivery_check() {
    const auto key=[](SDL_Keycode code,bool down) {
        SDL_Event event{};event.type=down?SDL_KEYDOWN:SDL_KEYUP;
        event.key.keysym.sym=code;event.key.keysym.scancode=SDL_GetScancodeFromKey(code);
        SDL_PushEvent(&event);
    };
    const auto check=[&](bool ok,const char* name) {
        if(!ok) {delivery_check_failed_=true;AP_ERROR("delivery check failed: %s",name);}
        else AP_INFO("delivery check passed: %s",name);
    };
    const int frame=frames_rendered_;
    if(frame==10 || frame==170) key(SDLK_e,true);
    if(frame==11 || frame==171) key(SDLK_e,false);
    if(frame==20) {
        check(opening_cutscene_.active()&&mission_stage_==MissionStage::DeliveryActive,
            "interaction enters scene without completing mission");
        check(traffic_visual_.car_count()==0,
            "cutscene clears live traffic rigs");
    }
    if(frame==30 || frame==60) key(SDLK_p,true);
    if(frame==31 || frame==61) key(SDLK_p,false);
    if(frame==40) {check(opening_cutscene_.paused(),"pause");delivery_check_pause_time_=opening_cutscene_.time();}
    if(frame==55) check(opening_cutscene_.time()==delivery_check_pause_time_,"paused timeline remains fixed");
    if(frame==70) check(!opening_cutscene_.paused()&&opening_cutscene_.time()>delivery_check_pause_time_,"resume advances timeline");
    if(frame==80) key(SDLK_ESCAPE,true);
    if(frame==81) key(SDLK_ESCAPE,false);
    if(frame==90) {
        check(!opening_cutscene_.active()&&mission_stage_==MissionStage::DeliveryComplete&&on_foot_,"skip returns control and completes delivery");
        check(traffic_visual_.car_count()>0,
            "traffic rigs repopulate after skip");
        delivery_check_position_=player_character_.position;
        key(SDLK_s,true);
    }
    if(frame==145) key(SDLK_s,false);
    if(frame==155) check(glm::distance(delivery_check_position_,player_character_.position)>.03f,"player moves after skip");
    if(frame==160) {
        const auto devon=city::devon_position();
        player_character_=spawn_character(collider_,devon.x,devon.z-.9f,3.14159265f);
        prev_player_character_=player_character_;mission_stage_=MissionStage::DeliveryActive;
        mission_success_feedback_s_=0;
    }
    if(frame==180) {
        check(opening_cutscene_.active()&&mission_stage_==MissionStage::DeliveryActive,
            "second interaction restarts scene");
        check(traffic_visual_.car_count()==0,
            "second cutscene clears repopulated traffic rigs");
    }
    if(frame==850) {
        check(!opening_cutscene_.active()&&mission_stage_==MissionStage::DeliveryComplete&&on_foot_,"natural completion returns control");
        check(traffic_visual_.car_count()>0,
            "traffic rigs repopulate after natural completion");
        delivery_check_passed_=!delivery_check_failed_;
    }
}
void App::render_opening() {
    camera_=opening_cutscene_.camera(window_.aspect());
    const auto env=compute_sky_env(opening_cutscene_.daylight());
    glClearColor(env.fog_color.r,env.fog_color.g,env.fog_color.b,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    std::vector<TrafficSpotLight> interior;
    const auto& site=city::kGasStationSite;
    for (const auto& part:city::kGasStationParts) {
        if (std::string_view(part.name)!="store interior ceiling light lens") continue;
        const glm::vec3 at{site.origin.x+site.cos_yaw*part.centre.x+site.sin_yaw*part.centre.z,
            site.ground_m+part.bottom_m-.04f,
            site.origin.z-site.sin_yaw*part.centre.x+site.cos_yaw*part.centre.z};
        interior.push_back({glm::vec4{at,6.6f},{0,-1,0,2.3f},{1,.88f,.70f,.58f}});
    }
    HeadlightRig lights;
    if (!tiled_lighting_.upload(interior,camera_.view_projection(),camera_.view(),window_.width(),window_.height()))
        ++gl_errors_;
    lights.traffic=tiled_lighting_.view();
    const auto& canopy=world_.canopy_lights();
    sky_.render(camera_,env,opening_cutscene_.time());
    const auto& visible=scene_.cull(camera_.frustum(),camera_.position,1800);
    renderer_.render(scene_,visible.visible,camera_,env,lights,canopy,{});
    // Normal player/staff rendering is suppressed for the authored cast.
    opening_cutscene_.draw(camera_,env,lights,canopy);
    ocean_.render(camera_,env,lights,canopy,opening_cutscene_.time(),
                  kSeaLevelMetres);
    renderer_.render_glass(scene_,visible.visible,camera_,env,lights,canopy);
    render_stats_=renderer_.stats();
    const glm::vec2 vp=UiCanvas::from_drawable({window_.width(),window_.height()}).size;
    hud_.begin(vp);opening_cutscene_.draw_hud(hud_,vp);hud_.end();
    if (!screenshot_path_.empty()&&frame_limit_>0&&frames_rendered_+1>=frame_limit_) {
        save_screenshot(screenshot_path_);screenshot_path_.clear();
    }
    gl_errors_+=drain_gl_errors("opening cutscene");
    window_.swap();++frames_rendered_;
}
}
