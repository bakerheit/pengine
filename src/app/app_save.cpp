#include "app/app.h"
#include "app/traffic_paint_paths.h"
#include "app/vehicle_model_tuning.h"
#include "app/vehicle_paint_catalog.h"
#include "game/save_game.h"
#include "core/log.h"
#include <SDL.h>
#include <filesystem>

namespace apricot {

void App::init_save_game() {
    if (save_path_.empty()) {
        char* folder = SDL_GetPrefPath("Bakerheit", "Probable Cause");
        if (folder) { save_path_ = std::string(folder) + "checkpoint.save"; SDL_free(folder); }
        else save_notice_ = "Save folder is unavailable.";
    }
    GameSave saved; std::string error;
    const bool available=load_game_save(save_path_, saved, error);
    ui_.set_save_available(available);
    std::error_code status_error;
    if (!available && std::filesystem::exists(save_path_,status_error)) save_notice_=error;
}

bool App::save_game() {
    if (opening_cutscene_.active() || vehicle_transition_.active() || boat_transition_.active()) {
        save_notice_ = "Finish the current scene or movement before saving."; return false;
    }
    if (in_aircraft_ || in_helicopter_ || in_boat_) {
        save_notice_ = "Step onto land or into a car before saving."; return false;
    }
    GameSave saved;
    saved.session_seed=seed_; saved.sim_step=step_index_; saved.mission=mission_stage_;
    saved.on_foot=on_foot_; saved.character_position=player_character_.position;
    saved.character_yaw=player_character_.facing_yaw;
    saved.view_yaw=player_character_.view_yaw; saved.view_pitch=player_character_.view_pitch;
    saved.car_model=static_cast<int>(car_visual_.active_car());
    saved.driving_style=static_cast<int>(driving_mechanics_style_);
    saved.car_position=car_.position; saved.car_rotation=car_.orientation;
    saved.car_health=car_.health; saved.car_damage=car_.body_damage;
    saved.car_mechanical=car_.mechanical; saved.car_key=car_.mechanical_key;
    saved.car_registration=car_visual_.registration();
    saved.car_paint_base=car_visual_.paint_base();
    saved.car_has_paint=car_visual_.respray().has_value();
    saved.car_paint=car_visual_.respray().value_or(PaintColor{});
    saved.has_trailer=true;saved.trailer=trailer_;
    if (!store_game_save(save_path_,saved,save_notice_)) { AP_WARN("save: %s",save_notice_.c_str()); return false; }
    save_notice_="Game saved."; ui_.set_save_available(true);
    AP_INFO("game checkpoint saved"); return true;
}

bool App::load_game() {
    GameSave saved;
    if (!load_game_save(save_path_,saved,save_notice_)) { AP_WARN("load: %s",save_notice_.c_str()); return false; }
    // Complete decoding and validation before touching live gameplay. All
    // resources for catalog cars were loaded by PlayerCarVisual::init().
    const auto model=canonical_player_car_id(static_cast<PlayerCarId>(saved.car_model));
    const auto style=static_cast<DrivingMechanicsStyle>(saved.driving_style);
    const auto next_tuning=player_model_tuning(style,model);
    const glm::vec3 forward=saved.car_rotation * glm::vec3{0,0,-1};
    VehicleState next_car=spawn_vehicle(next_tuning,collider_,saved.car_position.x,
                                       saved.car_position.z,std::atan2(-forward.x,-forward.z));
    next_car.position=saved.car_position; next_car.orientation=saved.car_rotation;
    next_car.health=saved.car_health; next_car.body_damage=saved.car_damage;
    next_car.mechanical=saved.car_mechanical; next_car.mechanical_key=saved.car_key;
    // The save format checks a paint base's range on its own; only the app
    // knows how many liveries this model has.
    if (!paint_base_valid(model,saved.car_paint_base)) {
        save_notice_="Saved paint is unavailable. Game left unchanged."; return false;
    }
    if (!car_visual_.select(scene_,next_tuning,next_car,model)) {
        save_notice_="Saved vehicle is unavailable. Game left unchanged."; return false;
    }
    car_visual_.set_registration(scene_,saved.car_registration);
    cancel_vehicle_transition(); boat_transition_={}; transition_camera_release_=0;
    vehicle_audio_.exit_vehicle();
    in_aircraft_=false;in_helicopter_=false; in_boat_=false; on_foot_=saved.on_foot;
    tuning_=next_tuning; driving_mechanics_style_=style;
    dev_menu_.set_driving_mechanics(style); dev_menu_.set_player_car(model);
    car_=next_car; prev_car_=car_;
    player_character_={}; player_character_.position=saved.character_position;
    player_character_.facing_yaw=saved.character_yaw;
    player_character_.view_yaw=saved.view_yaw;player_character_.view_pitch=saved.view_pitch;
    prev_player_character_=player_character_; character_spawned_=true;
    mission_stage_=saved.mission; seed_=saved.session_seed; step_index_=saved.sim_step;
    snow_clearance_ = {};
    snowplow_service_.reset();
    game_ui_.clear_waypoint();
    wanted_.reset();
    police_escalation_.reset();
    police_stop_feedback_s_=0.0f;
    police_offenses_.reset();
    police_arrest_.reset();
    traffic_horn_audio_.reset();
    vehicle_audio_.stop_horn(); player_horn_pending_=false;
    arrested_feedback_s_=0.0f;
    world_.set_police_context(0, player_focus_position());
    seen_impact_count_=0; impact_feedback_seconds_=0; police_emergency_enabled_=false;
    vehicle_interaction_notice_.clear(); vehicle_notice_until_=0;
    character_look_dx_pending_=0; character_look_dy_pending_=0;
    repair_shop_feedback_s_=0; mission_success_feedback_s_=0;
    repair_shop_visit_={};
    // The checkpoint owns one car. Remove extra player-parked copies from the
    // current session so repeated loads cannot stack them at the save point.
    for (auto& parked : parked_vehicles_) {
        collider_.set_kinematic_enabled(parked.collider,false);
        parked.visual.destroy(scene_);
    }
    parked_vehicles_.clear();
    world_.set_parked_vehicle_poses({});
    // The paint, after the parked cars have gone: they no longer hold pool
    // slots, and select() above left the catalog paint on the body.
    reset_respray_state();
    if (saved.car_paint_base>0) {
        const TrafficVehicleKind kind=canonical_player_car_id(model)==PlayerCarId::LegacyCar8
            ? TrafficVehicleKind::BoxTruck : TrafficVehicleKind::Sedan;
        const MaterialId livery=traffic_visual_.paint_material(kind,saved.car_paint_base);
        if (livery!=kInvalidId) car_visual_.set_factory_paint(scene_,livery,saved.car_paint_base);
    }
    if (saved.car_has_paint) {
        const MaterialId owned=acquire_paint_material(saved.car_paint);
        if (owned!=kInvalidId) car_visual_.apply_respray(scene_,owned,saved.car_paint);
        else AP_WARN("load: the saved respray could not be applied; factory paint shown");
    }
    traffic_visual_.reset_signals(scene_,collider_);
    enable_trailer_collision(false);
    if(saved.has_trailer) {
        trailer_=saved.trailer;prev_trailer_=trailer_;
        if(model!=PlayerCarId::HarrowHauler)spawn_freight_tractor();
        sync_trailer_collision();
    }
    else reset_freight_yard(true);
    sync_current_vehicle_obstacle();
    update_weather();
    last_fill_steps_=world_.fill(scene_,renderer_,player_focus_position());
    vehicle_audio_.set_model(player_car_definition(model).mesh_path);
    if (!on_foot_) vehicle_audio_.enter_vehicle(false,car_.position);
    chase_camera_.reset(); camera_obstruction_distance_=-1;
    update_camera(0); clock_.reset(); input_.consume_edges();
    ui_.enter_game(); ui_.set_save_available(true);
    save_notice_="Game loaded.";
    AP_INFO("game checkpoint loaded: mission %d, player (%.1f, %.1f)",
            int(mission_stage_),double(player_focus_position().x),double(player_focus_position().z));
    return true;
}
} // namespace apricot
