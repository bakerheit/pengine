#include "app/app.h"
#include "core/log.h"
#include "core/rng.h"
#include "city/map.h"
#include "game/vehicle_interaction.h"
#include "game/delivery_mission.h"
#include <algorithm>
#include <cmath>

namespace apricot {
namespace {
PlayerCarId traffic_model(const VehicleAgent& v) {
    switch (traffic_vehicle_kind(v)) {
        case TrafficVehicleKind::Bwc360:return PlayerCarId::Bwc360;
        case TrafficVehicleKind::Sedan:return PlayerCarId::LegacyCar5;
        case TrafficVehicleKind::BoxTruck:
        case TrafficVehicleKind::Snowplow:return PlayerCarId::LegacyCar8;
        case TrafficVehicleKind::Ambulance:return PlayerCarId::MunicipalAmbulance;
        case TrafficVehicleKind::Firetruck:return PlayerCarId::MunicipalFiretruck;
        case TrafficVehicleKind::HalcyonSix:return PlayerCarId::HalcyonSix;
        case TrafficVehicleKind::MontroseRegentEight:return PlayerCarId::MontroseRegentEight;
        case TrafficVehicleKind::VesperVx91:return PlayerCarId::VesperVx91;
        case TrafficVehicleKind::Police:return PlayerCarId::MunicipalCruiser91C;
    }
    return PlayerCarId::LegacyCar5;
}
glm::quat traffic_rotation(const VehicleAgent& v) {
    return glm::angleAxis(std::atan2(-v.fwd.x,-v.fwd.z),glm::vec3{0,1,0});
}
}

App::VehicleEntryTarget App::nearby_vehicle() const {
    VehicleEntryTarget result;
    float best=kVehicleEntryReach;
    const auto consider=[&](VehicleEntryTarget target,glm::vec3 pos,glm::quat rotation,
                            float width,float length,float ground,float speed) {
        const float distance=vehicle_entry_distance(player_character_.position,pos,rotation,width,length,ground,speed);
        if (!(distance<best)) return;
        // Test the route to the nearest door at chest height. A thin wall
        // between the player and a car must block entry too.
        const auto local=glm::inverse(rotation)*(player_character_.position-pos);
        glm::vec3 door=pos+rotation*glm::vec3{(local.x<0?-1.f:1.f)*(width+.4f),0,-length*.22f};
        const glm::vec3 from=player_character_.position+glm::vec3{0,.85f,0};
        door.y=from.y;
        const auto delta=door-from;
        const float reach=glm::length(delta);
        if (reach>.05f) {
            const auto hit=collider_.raycast(from,delta/reach,reach);
            if (hit.hit && hit.distance<reach-.08f) return;
        }
        result=target;best=distance;
    };
    consider({VehicleEntryTarget::Kind::Current,0,0,0,car_visual_.active_car()},car_.position,car_.orientation,
        tuning_.car_collision_half_width,tuning_.car_collision_half_length,
        car_.position.y-tuning_.wheel_radius-static_suspension_length(tuning_)-tuning_.com_height_above_mount,
        glm::length(car_.velocity));
    for (std::size_t i=0;i<parked_vehicles_.size();++i) {
        const auto& p=parked_vehicles_[i];
        consider({VehicleEntryTarget::Kind::Parked,i,0,0,p.visual.active_car()},p.state.position,p.state.orientation,
            p.tuning.car_collision_half_width,p.tuning.car_collision_half_length,
            p.state.position.y-p.tuning.wheel_radius-static_suspension_length(p.tuning)-p.tuning.com_height_above_mount,0);
    }
    for (const auto& v:world_.traffic().vehicles()) {
        if (v.snowplow_unit) continue;
        const auto footprint=traffic_vehicle_footprint(traffic_vehicle_kind(v));
        consider({VehicleEntryTarget::Kind::Traffic,0,v.lane_key,v.slot,traffic_model(v),v.police_unit},v.pos,traffic_rotation(v),
            footprint.half_width_m,footprint.half_length_m,v.pos.y,
            std::fabs(v.speed_mps)+glm::length(v.collision_velocity_xz));
    }
    return result;
}

void App::sync_current_vehicle_obstacle() {
    if (!on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_) {
        collider_.set_kinematic_enabled(current_vehicle_collider_,false);
        return;
    }
    const auto& bounds=car_visual_.placed_body_bounds();
    const auto centre=car_.position+car_.orientation*bounds.center();
    const float yaw=std::atan2((car_.orientation*glm::vec3{0,0,1}).x,(car_.orientation*glm::vec3{0,0,1}).z);
    if (current_vehicle_collider_==static_cast<std::size_t>(-1))
        current_vehicle_collider_=collider_.add_kinematic_oriented_box(centre,bounds.extents(),yaw);
    else collider_.set_kinematic_oriented_box(current_vehicle_collider_,centre,bounds.extents(),yaw);
    collider_.set_kinematic_vehicle(current_vehicle_collider_,true);
}

void App::park_current_vehicle() {
    drop_trailer();
    ParkedVehicle parked;
    car_visual_.clone_parked(scene_,parked.visual);
    parked.state=car_;parked.state.velocity=glm::vec3{0};parked.state.angular_velocity=glm::vec3{0};
    parked.tuning=tuning_;
    parked.visual.sync(scene_,parked.tuning,parked.state,parked.state,0,0,0);
    const auto& bounds=parked.visual.placed_body_bounds();
    const auto forward=parked.state.orientation*glm::vec3{0,0,1};
    const auto centre=parked.state.position+parked.state.orientation*bounds.center();
    if (current_vehicle_collider_!=static_cast<std::size_t>(-1)) {
        parked.collider=current_vehicle_collider_;
        collider_.set_kinematic_oriented_box(parked.collider,centre,bounds.extents(),std::atan2(forward.x,forward.z));
        current_vehicle_collider_=static_cast<std::size_t>(-1);
    } else parked.collider=collider_.add_kinematic_oriented_box(centre,bounds.extents(),std::atan2(forward.x,forward.z));
    collider_.set_kinematic_vehicle(parked.collider,true);
    parked_vehicles_.push_back(std::move(parked));
}

bool App::take_nearby_vehicle(const VehicleEntryTarget& target) {
    if (target.kind==VehicleEntryTarget::Kind::None) return false;
    if (target.kind==VehicleEntryTarget::Kind::Current) return true;
    if (target.kind==VehicleEntryTarget::Kind::Parked) {
        if (target.parked_index>=parked_vehicles_.size()) return false;
        park_current_vehicle();
        auto parked=std::move(parked_vehicles_[target.parked_index]);
        parked_vehicles_.erase(parked_vehicles_.begin()+static_cast<std::ptrdiff_t>(target.parked_index));
        collider_.set_kinematic_enabled(parked.collider,false);
        current_vehicle_collider_=parked.collider;
        car_visual_.destroy(scene_);
        car_visual_=std::move(parked.visual);car_=parked.state;tuning_=parked.tuning;
    } else {
        const auto& vehicles=world_.traffic().vehicles();
        const auto it=std::find_if(vehicles.begin(),vehicles.end(),[&](const VehicleAgent& v) {
            return v.lane_key==target.lane_key && v.slot==target.slot;
        });
        if (it==vehicles.end()) return false;
        const VehicleAgent candidate=*it;
        const auto layout=traffic_visual_.vehicle_layout(candidate);
        const MaterialId paint=traffic_visual_.vehicle_paint(candidate);
        VehicleAgent taken;
        if (!world_.take_traffic_vehicle(candidate.lane_key,candidate.slot,taken)) return false;
        park_current_vehicle();
        // Match the traffic car's existing scale and axle spacing, rather than
        // turning a stolen van into a stretched version of the previous car.
        const auto& definition=player_car_definition(target.model);
        tuning_=player_vehicle_tuning(driving_mechanics_style_);
        const float scale=layout.body.scale.z;
        tuning_.half_track=definition.wheel_x*scale;
        tuning_.half_wheelbase=(definition.wheel_front_z+definition.wheel_rear_z)*scale*.5f;
        tuning_.wheel_radius=layout.wheel_radius;
        tuning_.car_collision_half_width=layout.placed_body_bounds.extents().x;
        tuning_.car_collision_half_length=layout.placed_body_bounds.extents().z;
        const auto rotation=traffic_rotation(taken);
        const auto axle_mid=(layout.wheel_centres[0]+layout.wheel_centres[2])*.5f;
        const auto pos=taken.pos+rotation*glm::vec3{0,0,axle_mid.z};
        car_=spawn_vehicle(tuning_,collider_,pos.x,pos.z,std::atan2(-taken.fwd.x,-taken.fwd.z));
        car_.velocity=taken.fwd*taken.speed_mps;
        car_.body_damage=taken.body_damage;
        car_.mechanical=taken.mechanical;
        car_.mechanical_key=splitmix64_mix(city::kMapSeed ^ taken.lane_key ^
            (static_cast<uint64_t>(taken.slot)<<32));
        float damage=0;for (float zone:taken.body_damage.zones) damage=std::max(damage,zone);
        car_.health=std::max(5.f,100.f-90.f*damage);
        car_visual_.select(scene_,tuning_,car_,target.model);
        car_visual_.set_paint(scene_,paint);
        car_visual_.set_registration(scene_,traffic_visual_.vehicle_registration(taken));
        vehicle_interaction_notice_="Vehicle taken";
        vehicle_notice_until_=step_index_+360;
        if (!vehicle_entry_check_ && !driver_transition_check_)
            wanted_.add_heat(2.5f, WantedSystem::Crime::VehicleTheft);
        AP_INFO("vehicle taken: key=%llu slot=%u model=%s %s; original retired",
            static_cast<unsigned long long>(taken.lane_key),taken.slot,definition.brand,definition.model);
    }
    car_.car_contact_speed=0.0f;
    prev_car_=car_;
    std::vector<glm::vec3> obstacles;
    for (const auto& parked:parked_vehicles_)
        obstacles.push_back(parked.state.position);
    world_.set_parked_vehicle_poses(std::move(obstacles));
    seen_impact_count_=car_.impact_count;
    dev_menu_.set_player_car(car_visual_.active_car());
    return true;
}

bool App::vehicle_transition_route_clear(glm::vec3 from, glm::vec3 to, float ground) const {
    const float distance = glm::length(to-from);
    if (!std::isfinite(distance) || distance > 5.f) return false;
    CharacterTuning clearance = character_tuning_;
    clearance.height_m += .1f;
    const int samples = std::max(1, static_cast<int>(std::ceil(distance/.12f)));
    for (int i=0; i<=samples; ++i) {
        const glm::vec3 p = glm::mix(from,to,float(i)/float(samples));
        if (p.y < ground-1.25f || p.y > ground+.65f ||
            !character_position_clear(collider_,p,clearance)) return false;
        for (const auto& vehicle:world_.traffic().vehicles()) {
            const auto f=traffic_vehicle_footprint(traffic_vehicle_kind(vehicle));
            const auto delta=p-vehicle.pos;
            const glm::vec3 right{-vehicle.fwd.z,0,vehicle.fwd.x};
            if (std::fabs(glm::dot(delta,vehicle.fwd)) < f.half_length_m+.45f &&
                std::fabs(glm::dot(delta,right)) < f.half_width_m+.45f) return false;
        }
    }
    const glm::vec3 origin=from+glm::vec3{0,.85f,0};
    const auto hit=distance>.01f ? collider_.raycast(origin,(to-from)/distance,distance)
                               : TerrainCollider::GroundHit{};
    return !hit.hit || hit.distance >= distance-.08f;
}

bool App::vehicle_transition_door_clear(PlayerCarId car, const Transform& body,bool passenger) const {
    auto layout=vehicle_driver_door(car);
    if (passenger) layout.hinge.x=-layout.hinge.x;
    CharacterTuning panel;
    panel.radius_m=.08f;
    panel.height_m=layout.panel_height*body.scale.y;
    panel.max_step_m=0.f;
    for (int angle=0;angle<=12;++angle) {
        const auto door=passenger
            ? vehicle_passenger_door_transform(car,body,float(angle)/12.f)
            : vehicle_driver_door_transform(car,body,float(angle)/12.f);
        for (int span=0;span<=12;++span) {
            const auto p=door.transform_point({layout.hinge.x,layout.sill_y,
                glm::mix(layout.front_z,layout.rear_z,float(span)/12.f)});
            if (!character_position_clear(collider_,p,panel)) return false;
            for (const auto& vehicle:world_.traffic().vehicles()) {
                const auto footprint=traffic_vehicle_footprint(traffic_vehicle_kind(vehicle));
                const auto delta=p-vehicle.pos;
                const glm::vec3 right{-vehicle.fwd.z,0,vehicle.fwd.x};
                if (std::fabs(glm::dot(delta,vehicle.fwd))<footprint.half_length_m+.08f &&
                    std::fabs(glm::dot(delta,right))<footprint.half_width_m+.08f) return false;
            }
        }
    }
    return true;
}

bool App::begin_vehicle_transition(const VehicleEntryTarget& target, bool entering) {
    const PlayerCarVisual* visual=&car_visual_;
    const VehicleState* state=&car_;
    const VehicleTuning* tuning=&tuning_;
    std::size_t obstacle=current_vehicle_collider_;
    if (entering && target.kind==VehicleEntryTarget::Kind::Parked) {
        if (target.parked_index>=parked_vehicles_.size()) return false;
        const auto& parked=parked_vehicles_[target.parked_index];
        visual=&parked.visual; state=&parked.state; tuning=&parked.tuning;
        obstacle=parked.collider;
    }
    if (!has_animated_driver(visual->active_car()) ||
        glm::length(state->velocity)>kVehicleEntryMaxSpeed ||
        (state->orientation*glm::vec3{0,1,0}).y<.8f) return false;
    const Transform body=visual->fitted_body_transform(*state);
    const auto& layout=vehicle_driver_layout(visual->active_car());
    const bool motorbike=is_motorbike(visual->active_car());
    const glm::vec3 driver_side=body.rotation*glm::vec3{1,0,0};
    const glm::vec3 facing=entering ? -driver_side : driver_side;
    const float yaw=std::atan2(facing.x,-facing.z);
    const auto side=body.transform_point({layout.approach_x,0,layout.hip.z})+
        driver_side*(motorbike?.20f:.55f);
    const bool enabled=obstacle<collider_.static_boxes().size() && collider_.static_boxes()[obstacle].enabled;
    collider_.set_kinematic_enabled(obstacle,false);
    auto door=spawn_character(collider_,side.x,side.z,yaw);
    auto start=entering ? player_character_ : spawn_character(collider_,
        side.x+driver_side.x*.35f,side.z+driver_side.z*.35f,yaw);
    const float ground=state->position.y-tuning->wheel_radius-
        static_suspension_length(*tuning)-tuning->com_height_above_mount;
    glm::vec3 seat=body.transform_point(layout.hip); seat.y=door.position.y;
    const glm::vec3 local=glm::inverse(body.rotation)*(start.position-body.position);
    const bool clear=local.x>body.scale.x*layout.approach_x &&
        (motorbike || vehicle_transition_door_clear(visual->active_car(),body)) &&
        vehicle_transition_route_clear(start.position,door.position,ground) &&
        vehicle_transition_route_clear(door.position,seat,ground);
    collider_.set_kinematic_enabled(obstacle,enabled);
    if (!clear) {
        vehicle_interaction_notice_=motorbike
            ? (entering ? "Move beside the bike" : "No room to dismount")
            : (entering ? "Move to the clear driver's side" : "No room on the driver's side");
        vehicle_notice_until_=step_index_+240;
        return false;
    }
    if (entering && !take_nearby_vehicle(target)) return false;
    transition_start_=start;
    transition_start_.velocity=glm::vec3{0}; transition_start_.sprinting=false;
    transition_door_=door;
    transition_door_.view_yaw=start.view_yaw;
    transition_door_.view_pitch=start.view_pitch;
    transition_door_.distance_walked_m=start.distance_walked_m;
    transition_car_position_=car_.position;
    transition_car_rotation_=car_.orientation;
    vehicle_transition_={entering ? VehicleTransitionDirection::Enter : VehicleTransitionDirection::Exit,0};
    transition_enters_mission_car_ = entering &&
        target.kind == VehicleEntryTarget::Kind::Current;
    transition_waiting_=false;
    transition_enters_mission_car_=false;
    if (!entering) player_character_=prev_player_character_=transition_door_;
    character_look_dx_pending_=character_look_dy_pending_=0;
    transition_camera_=camera_;
    transition_camera_release_=0;
    vehicle_notice_until_=0;
    sync_current_vehicle_obstacle();
    return true;
}

void App::cancel_vehicle_transition() {
    if (!vehicle_transition_.active()) return;
    vehicle_transition_={};
    transition_waiting_=false;
    player_character_.velocity=glm::vec3{0};
    prev_player_character_=player_character_;
    transition_camera_=camera_;
    transition_camera_release_=1.f;
    sync_current_vehicle_obstacle();
}

void App::step_vehicle_transition() {
    if (!vehicle_transition_.active()) return;
    if (!has_animated_driver(car_visual_.active_car()) ||
        glm::distance(car_.position,transition_car_position_)>.35f ||
        std::fabs(glm::dot(car_.orientation,transition_car_rotation_))<.999f) {
        cancel_vehicle_transition();
        vehicle_interaction_notice_="Vehicle moved; try again";
        vehicle_notice_until_=step_index_+240;
        return;
    }
    const float ground=car_.position.y-tuning_.wheel_radius-
        static_suspension_length(tuning_)-tuning_.com_height_above_mount;
    const auto body=car_visual_.fitted_body_transform(car_);
    auto seat=body.transform_point(vehicle_driver_layout(car_visual_.active_car()).hip);
    seat.y=transition_door_.position.y;
    collider_.set_kinematic_enabled(current_vehicle_collider_,false);
    const bool clear=(is_motorbike(car_visual_.active_car()) ||
        vehicle_transition_door_clear(car_visual_.active_car(),body)) &&
        vehicle_transition_route_clear(transition_start_.position,transition_door_.position,ground) &&
        vehicle_transition_route_clear(transition_door_.position,seat,ground);
    sync_current_vehicle_obstacle();
    transition_waiting_=!clear;
    if (!clear) {
        player_character_.velocity=glm::vec3{0};
        vehicle_interaction_notice_=is_motorbike(car_visual_.active_car())
            ? "Bike side blocked - E / A to cancel"
            : "Driver's side blocked - E / A to cancel";
        vehicle_notice_until_=step_index_+30;
        return;
    }
    const bool finished=advance_vehicle_transition(vehicle_transition_);
    const auto sample=sample_vehicle_transition(vehicle_transition_);
    const auto previous=player_character_;
    player_character_=transition_start_;
    player_character_.position=glm::mix(transition_start_.position,transition_door_.position,sample.approach);
    const float yaw_delta=std::remainder(transition_door_.facing_yaw-transition_start_.facing_yaw,6.283185307f);
    player_character_.facing_yaw=transition_start_.facing_yaw+yaw_delta*sample.approach;
    player_character_.velocity=(player_character_.position-previous.position)/float(kSimDt);
    player_character_.distance_walked_m=previous.distance_walked_m+
        glm::length(glm::vec2{player_character_.position.x-previous.position.x,
                            player_character_.position.z-previous.position.z});
    if (!finished) return;
    const bool entering=vehicle_transition_.direction==VehicleTransitionDirection::Enter;
    const bool mission_car=transition_enters_mission_car_;
    vehicle_transition_={};
    transition_enters_mission_car_=false;
    on_foot_=!entering;
    player_character_.velocity=glm::vec3{0};
    if (entering) {
        if (mission_car) enter_mission_car(mission_stage_);
        vehicle_audio_.set_model(player_car_definition(car_visual_.active_car()).mesh_path);
        vehicle_audio_.enter_vehicle(false,car_.position);
    } else {
        police_emergency_enabled_=false;
        vehicle_audio_.exit_vehicle();
        // Body facing and camera orbit are independent. Keep the view on the
        // same side of the player instead of sweeping through them toward the
        // car when the outward-facing exit hands back to the on-foot camera.
        const auto target=player_character_.position+glm::vec3{0,1.38f,0};
        const auto view=target-camera_.position;
        const float flat=glm::length(glm::vec2{view.x,view.z});
        player_character_.view_yaw=std::atan2(view.x,-view.z);
        player_character_.view_pitch=std::atan2(view.y,std::max(flat,1e-4f));
        prev_player_character_.view_yaw=player_character_.view_yaw;
        prev_player_character_.view_pitch=player_character_.view_pitch;
        // The obstruction ray now starts at the standing player, not the
        // vehicle/door midpoint. Seed its distance from the current eye.
        camera_obstruction_distance_=glm::length(view);
    }
    transition_camera_=camera_;
    transition_camera_release_=1.f;
    character_look_dx_pending_=character_look_dy_pending_=0;
    sync_current_vehicle_obstacle();
    AP_INFO("%s player %s after %u fixed ticks",player_car_definition(car_visual_.active_car()).model,
        entering ? "entered" : "exited",kVehicleTransitionTicks);
}

void App::run_driver_transition_check() {
    if (step_index_<30 || driver_check_stage_>=6) return;
    const auto model=car_visual_.active_car();
    const auto& layout=vehicle_driver_layout(model);
    const auto fail=[&](const char* reason) {
        AP_ERROR("%s transition regression: %s",player_car_definition(model).model,reason);
        driver_check_stage_=7;
    };
    const auto cancel_blocked=[&]() {
        const bool was_on_foot=on_foot_;
        const auto starts=vehicle_audio_.startup_count();
        const auto block=collider_.add_kinematic_oriented_box(
            transition_door_.position+glm::vec3{0,1,0},{.7f,1.5f,.7f},0);
        step_vehicle_transition();
        const bool waiting=transition_waiting_ && vehicle_transition_.tick==0;
        toggle_player_mode();
        collider_.set_kinematic_enabled(block,false);
        return waiting && !vehicle_transition_.active() && on_foot_==was_on_foot &&
            vehicle_audio_.startup_count()==starts;
    };
    if (driver_check_stage_==0) {
        const auto body=car_visual_.fitted_body_transform(car_);
        const auto side=body.rotation*glm::vec3{1,0,0};
        const auto p=body.transform_point({layout.approach_x,0,layout.hip.z})+side*.8f;
        player_character_=spawn_character(collider_,p.x,p.z,std::atan2(-side.x,side.z));
        prev_player_character_=player_character_;
        driver_check_starts_=vehicle_audio_.startup_count();
        if (!is_motorbike(model)) {
            const auto sweep_block=collider_.add_kinematic_oriented_box(
                vehicle_driver_door_transform(model,body,.6f).transform_point(vehicle_driver_door(model).handle),
                {.08f,.2f,.08f},0);
            toggle_player_mode();
            const bool door_blocked=!vehicle_transition_.active() && on_foot_;
            collider_.set_kinematic_enabled(sweep_block,false);
            if (!door_blocked) { fail("door swung through an obstacle"); return; }
        }
        toggle_player_mode();
        if (!vehicle_transition_.active() || !on_foot_) { fail("entry did not stage"); return; }
        toggle_player_mode();
        if (vehicle_transition_.tick!=0 || vehicle_audio_.startup_count()!=driver_check_starts_) {
            AP_ERROR("%s repeat diagnostic: transition tick=%u, ignition starts=%u (expected %u)",
                player_car_definition(model).model,vehicle_transition_.tick,
                vehicle_audio_.startup_count(),driver_check_starts_);
            fail("repeat interaction restarted transition or ignition ran early"); return;
        }
        if (!cancel_blocked()) { fail("blocked entry cancellation failed"); return; }
        toggle_player_mode();
        if (!vehicle_transition_.active()) { fail("entry after cancellation failed"); return; }
        driver_check_started_=step_index_; driver_check_stage_=1;
    } else if (driver_check_stage_==1 || driver_check_stage_==3 || driver_check_stage_==5) {
        if (step_index_-driver_check_started_>kVehicleTransitionTicks+120) {
            fail("transition timed out"); return;
        }
        if (vehicle_transition_.active()) return;
        if (step_index_-driver_check_started_!=kVehicleTransitionTicks) {
            fail("transition ended before its fixed duration"); return;
        }
        const bool expect_foot=driver_check_stage_==3;
        if (on_foot_!=expect_foot) { fail("completion mode wrong"); return; }
        if (expect_foot) {
            driver_check_started_=step_index_;
            driver_check_previous_eye_=camera_.position;
        }
        const unsigned starts=driver_check_starts_+(driver_check_stage_==5 ? 2u : 1u);
        if (vehicle_audio_.started() && !audio_device_.bank().engine_start.empty() &&
            vehicle_audio_.startup_count()!=starts) { fail("ignition count wrong"); return; }
        ++driver_check_stage_;
        if (driver_check_stage_==6)
            AP_INFO("%s transition regression PASSED: %s clearance, staged entry, repeated input, blocked exit, blocked entry/exit cancellation, safe exit, re-entry, parked ignition once per entry",
                player_car_definition(model).model,is_motorbike(model)?"saddle":"door sweep");
    } else if (driver_check_stage_==2) {
        const auto block=collider_.add_kinematic_oriented_box(
            transition_door_.position+glm::vec3{0,1,0},{.7f,1.5f,.7f},0);
        toggle_player_mode();
        const bool blocked=!vehicle_transition_.active() && !on_foot_;
        collider_.set_kinematic_enabled(block,false);
        if (!blocked) { fail("blocked exit started"); return; }
        toggle_player_mode();
        if (!vehicle_transition_.active() || on_foot_) { fail("safe exit did not stage"); return; }
        if (!cancel_blocked()) { fail("blocked exit cancellation failed"); return; }
        toggle_player_mode();
        if (!vehicle_transition_.active()) { fail("exit after cancellation failed"); return; }
        driver_check_started_=step_index_; driver_check_stage_=3;
    } else if (driver_check_stage_==4) {
        // Keep the player on foot through the full camera release. Immediate
        // re-entry used to hide a sweep through the character at this boundary.
        const auto target=player_character_.position+glm::vec3{0,1.38f,0};
        const float distance=glm::distance(camera_.position,target);
        const float jump=glm::distance(camera_.position,driver_check_previous_eye_);
        driver_check_min_camera_distance_=std::min(driver_check_min_camera_distance_,distance);
        driver_check_max_camera_step_=std::max(driver_check_max_camera_step_,jump);
        driver_check_previous_eye_=camera_.position;
        if (distance<2.f || jump>.4f) { fail("exit camera crossed the player or snapped during release"); return; }
        if (step_index_-driver_check_started_<90 || transition_camera_release_>0.f) return;
        AP_INFO("%s exit camera PASSED: minimum distance %.3f m, largest frame step %.3f m",
            player_car_definition(model).model,static_cast<double>(driver_check_min_camera_distance_),
            static_cast<double>(driver_check_max_camera_step_));
        if (!character_position_clear(collider_,player_character_.position,character_tuning_)) {
            fail("exit destination overlaps solid geometry"); return;
        }
        const auto reentry_target=nearby_vehicle();
        const auto body=car_visual_.fitted_body_transform(car_);
        const auto local=glm::inverse(body.rotation)*(player_character_.position-body.position);
        toggle_player_mode();
        if (!vehicle_transition_.active()) {
            AP_ERROR("%s re-entry diagnostic: target=%u model=%u local=(%.3f, %.3f, %.3f) notice='%s'",
                player_car_definition(model).model,static_cast<unsigned>(reentry_target.kind),
                static_cast<unsigned>(reentry_target.model),static_cast<double>(local.x),
                static_cast<double>(local.y),static_cast<double>(local.z),
                vehicle_interaction_notice_.c_str());
            fail("re-entry did not stage"); return;
        }
        driver_check_started_=step_index_; driver_check_stage_=5;
    }
}

void App::run_vehicle_entry_check() {
    // Let the actual unattended starter sit for five seconds before touching
    // any controls. The old brake/reverse bug already drove it away by then.
    if (step_index_<600 || step_index_%30!=0) return;
    const float parked_drift=glm::length(glm::vec2{car_.position.x,car_.position.z}-start_position_);
    if (!on_foot_ || car_.gear==kGearReverse || parked_drift>.1f) {
        AP_ERROR("vehicle entry regression: unattended starter moved %.3f m (gear %d)",
            static_cast<double>(parked_drift),car_.gear);
        return;
    }
    if (step_index_==600) AP_INFO("unattended starter PASSED: five seconds parked, %.4f m drift, gear %d",
        static_cast<double>(parked_drift),car_.gear);
    const auto parked_before=parked_vehicles_.size(); // The freight yard already owns a parked tractor.
    const auto old_model=car_visual_.active_car();
    const auto old_position=car_.position;
    const auto old_damage=pack_vehicle_damage0(car_.body_damage);
    const auto old_paint=car_visual_.paint(scene_);
    const auto old_plate=car_visual_.registration();
    const auto fail=[&](const char* reason) {
        AP_ERROR("vehicle entry regression: %s",reason);
    };
    // Use a real stopped traffic car, its current paint/layout and the same
    // public toggle as E. No fake render-only test object or direct AI edits.
    const auto traffic=world_.traffic().vehicles();
    for (const auto& candidate:traffic) {
        if (candidate.speed_mps>.8f || glm::length(candidate.collision_velocity_xz)>.1f) continue;
        const auto f=traffic_vehicle_footprint(traffic_vehicle_kind(candidate));
        const auto rotation=traffic_rotation(candidate);
        const auto door=candidate.pos+rotation*glm::vec3{-(f.half_width_m+.8f),0,-f.half_length_m*.22f};
        const auto trial=spawn_character(collider_,door.x,door.z,0);
        if (!character_position_clear(collider_,trial.position,character_tuning_)) continue;
        player_character_=trial;prev_player_character_=trial;
        const auto target=nearby_vehicle();
        if (target.kind!=VehicleEntryTarget::Kind::Traffic || target.lane_key!=candidate.lane_key || target.slot!=candidate.slot) continue;
        const auto expected_paint=traffic_visual_.vehicle_paint(candidate);
        const auto expected_plate=traffic_visual_.vehicle_registration(candidate);
        const auto starts_before = vehicle_audio_.startup_count();
        toggle_player_mode();
        if (vehicle_audio_.startup_count() != starts_before) { fail("traffic takeover replayed ignition");return; }
        if (on_foot_ || parked_vehicles_.size()!=parked_before+1u || car_visual_.active_car()!=traffic_model(candidate)) { fail("takeover failed");return; }
        if (!collider_.static_boxes()[parked_vehicles_[parked_before].collider].is_vehicle) {
            fail("parked car lost its vehicle collision/audio tag");return;
        }
        if (pack_vehicle_damage0(car_.body_damage)!=pack_vehicle_damage0(candidate.body_damage)) { fail("damage changed on takeover");return; }
        if (pack_vehicle_damage1(car_.body_damage)!=pack_vehicle_damage1(candidate.body_damage) ||
            car_visual_.paint(scene_)!=expected_paint) { fail("paint or scratches changed on takeover");return; }
        if (car_visual_.registration()!=expected_plate || parked_vehicles_[parked_before].visual.registration()!=old_plate) { fail("plate changed on takeover or parking");return; }
        if (parked_vehicles_[parked_before].state.position!=old_position) { fail("old car teleported");return; }
        for (const auto& v:world_.traffic().vehicles())
            if (v.lane_key==candidate.lane_key && v.slot==candidate.slot) { fail("AI copy still active");return; }
        car_.velocity=glm::vec3{0};prev_car_=car_;
        toggle_player_mode();
        if (!on_foot_) { fail("safe exit failed");return; }
        const auto parked=parked_vehicles_[parked_before];
        const auto return_door=parked.state.position+parked.state.orientation*
            glm::vec3{-(parked.tuning.car_collision_half_width+.8f),0,-parked.tuning.car_collision_half_length*.22f};
        player_character_=spawn_character(collider_,return_door.x,return_door.z,0);
        prev_player_character_=player_character_;
        toggle_player_mode();
        if (on_foot_ || car_visual_.active_car()!=old_model ||
            pack_vehicle_damage0(car_.body_damage)!=old_damage || car_visual_.paint(scene_)!=old_paint ||
            parked_vehicles_.size()!=parked_before+1u) { fail("parked re-entry failed");return; }
        if (car_visual_.registration()!=old_plate) { fail("parked re-entry lost plate");return; }
        if (vehicle_audio_.started() && !audio_device_.bank().engine_start.empty() &&
            vehicle_audio_.startup_count() != starts_before + 1) { fail("parked entry did not play ignition");return; }
        car_.velocity=glm::vec3{0};prev_car_=car_;
        // Surround all exit candidates. The player must stay in the car.
        std::vector<std::size_t> blockers;
        for (const glm::vec3 local:{glm::vec3{-(tuning_.car_collision_half_width+.85f),0,0},
                                  glm::vec3{tuning_.car_collision_half_width+.85f,0,0},
                                  glm::vec3{0,0,tuning_.car_collision_half_length+.9f}}) {
            const auto p=car_.position+car_.orientation*local;
            blockers.push_back(collider_.add_kinematic_oriented_box(p,{.65f,2,.65f},0));
        }
        toggle_player_mode();
        const bool blocked=!on_foot_;
        if (vehicle_audio_.started() && !audio_device_.bank().engine_start.empty() &&
            vehicle_audio_.startup_count() != starts_before + 1) { fail("blocked exit retriggered ignition");return; }
        for (auto id:blockers) collider_.set_kinematic_enabled(id,false);
        if (!blocked) { fail("blocked exit was allowed");return; }
        toggle_player_mode();
        if (!on_foot_) { fail("exit did not recover after removing blockers");return; }
        // Finish in the taken vehicle, making the bounded screenshot show the
        // transferred model/paint in the normal driving renderer and HUD.
        const auto& stolen=parked_vehicles_[parked_before];
        const auto stolen_door=stolen.state.position+stolen.state.orientation*
            glm::vec3{-(stolen.tuning.car_collision_half_width+.8f),0,-stolen.tuning.car_collision_half_length*.22f};
        player_character_=spawn_character(collider_,stolen_door.x,stolen_door.z,0);
        prev_player_character_=player_character_;
        toggle_player_mode();
        if (on_foot_ || car_visual_.paint(scene_)!=expected_paint) { fail("taken car re-entry lost paint");return; }
        if (car_visual_.registration()!=expected_plate) { fail("taken car re-entry lost plate");return; }
        AP_INFO("license plate transfer PASSED: %s / %s; original %s preserved",
            city::state_name(expected_plate.state),plate_serial(expected_plate).c_str(),plate_serial(old_plate).c_str());
        vehicle_entry_check_passed_=true;
        AP_INFO("vehicle entry regression PASSED: real traffic takeover, exact model/damage, AI removal, parked preservation/re-entry, blocked/safe exit, parked-only ignition (%u starts)", vehicle_audio_.startup_count());
        return;
    }
}
} // namespace apricot
