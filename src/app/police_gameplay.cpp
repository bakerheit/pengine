#include "app/app.h"

#include <algorithm>

#include "core/log.h"

namespace apricot {

void App::check_police_driving_offenses() {
    PoliceDrivingSample sample;
    sample.vehicle_identity=car_.mechanical_key;
    sample.position=car_.position;
    sample.forward=car_.orientation*glm::vec3{0,0,-1};
    sample.velocity=car_.velocity;
    sample.half_length_m=tuning_.car_collision_half_length;
    sample.step=static_cast<int64_t>(step_index_+1u);
    sample.driving=!on_foot_ && !in_aircraft_ && !in_boat_ &&
        !vehicle_transition_.active() && !boat_transition_.active();
    std::vector<PoliceOffenseWitness> witnesses;
    if (sample.driving) {
        const auto visible=visible_police(car_.position,true);
        for (const auto& agent:world_.traffic().vehicles()) {
            if (!agent.police_unit || std::find(visible.begin(),visible.end(),
                    VisiblePoliceIdentity{agent.lane_key,agent.slot})==visible.end())
                continue;
            witnesses.push_back({police_officer_eye_position(agent),
                police_officer_forward(agent),true});
        }
    }
    const auto report=police_offenses_.observe_driving(world_.lanes(),
        world_.traffic_tuning(),sample,witnesses,world_.traffic().police_tuning());
    if (report) {
        wanted_.add_heat(report.heat,report.crime);
        ++police_red_light_reports_;
        AP_INFO("police witnessed red-light crossing: lane %u; wanted %d",
            report.incoming,wanted_.level());
    }
}

void App::check_police_collision_offenses() {
    if (on_foot_ || in_aircraft_ || in_boat_ || vehicle_transition_.active()) return;
    for (const auto& contact:world_.traffic().police_player_contacts()) {
        const auto report=police_offenses_.observe_police_contact({
            {contact.lane_key,contact.slot},contact.player_velocity,
            contact.police_velocity,contact.normal,static_cast<int64_t>(step_index_)});
        if (!report) continue;
        wanted_.add_heat(report.heat,report.crime);
        const auto target=player_focus_position();
        world_.set_police_context(wanted_.level(),target,visible_police(target));
        world_.report_police_vehicle_hit(report.cruiser);
        ++police_collision_reports_;
        AP_INFO("player hit police car: key %llu slot %u; wanted %d",
            static_cast<unsigned long long>(contact.lane_key),contact.slot,wanted_.level());
    }
}

void App::check_police_arrest(const std::vector<VisiblePoliceIdentity>& visible) {
    const bool arrestable=on_foot_ && !in_aircraft_ && !in_boat_ &&
        !vehicle_transition_.active() && !boat_transition_.active();
    const auto event=police_arrest_.observe(step_index_,arrestable,wanted_.level(),
        player_character_.position,world_.traffic().vehicles(),visible);
    if (!event) return;
    wanted_.reset();
    world_.set_police_context(0,player_focus_position());
    arrested_feedback_s_=GameUi::kArrestedDisplaySeconds;
    ++police_arrest_reports_;
    AP_INFO("Arrested: officer key %llu slot %u held within %.1f m for %.1f s",
        static_cast<unsigned long long>(event->officer.lane_key),event->officer.slot,
        kPoliceArrestRangeM,kPoliceArrestHoldSeconds);
}

// This check scripts the player's placement/input only. Police perception,
// contacts, braking, walking and every door phase use the live game path.
InputFrame App::police_officer_check_input() {
    InputFrame input;
    input.handbrake=1.0f;
    const auto fail=[&](const char* reason) {
        AP_ERROR("police officer check: %s (stage %d)",reason,police_officer_check_stage_);
        police_officer_check_failed_=true;
    };
    const auto advance=[&](int stage) {
        police_officer_check_stage_=stage;
        police_officer_check_stage_tick_=police_officer_check_tick_;
    };
    ++police_officer_check_tick_;
    if (police_officer_check_failed_ || police_officer_check_done_ ||
        police_officer_check_tick_<120u) return input;
    const auto& cars=world_.traffic().vehicles();
    const auto chosen=std::find_if(cars.begin(),cars.end(),[&](const auto& car) {
        return car.police_unit && car.lane_key==police_officer_check_unit_.lane_key &&
            car.slot==police_officer_check_unit_.slot;
    });
    const uint64_t elapsed=police_officer_check_tick_-police_officer_check_stage_tick_;
    if (elapsed>2400u) {
        if (chosen!=cars.end())
            AP_ERROR("police officer check: phase %u, transition %u, speed %.3f, walked %.2f, officer %.2f %.2f, target %.2f %.2f",
                static_cast<unsigned>(chosen->officer.phase),chosen->officer.transition.tick,
                chosen->speed_mps,chosen->officer.distance_walked_m,
                chosen->officer.pos.x,chosen->officer.pos.z,
                player_character_.position.x,player_character_.position.z);
        police_officer_check_capture_="failed";
        fail("stage timed out");return input;
    }
    if (police_officer_check_stage_==0) {
        const auto& graph=world_.lanes();
        for (LaneRef lane=0;lane<graph.lane_count();++lane) {
            if (graph.approach_control(lane)!=JunctionControl::Signal) continue;
            const auto& approach=graph.lane(lane);
            if (traffic_signal_phase(graph,approach.junction_to,lane,
                    static_cast<int64_t>(step_index_),world_.traffic_tuning())!=TrafficSignalPhase::Red ||
                traffic_signal_phase(graph,approach.junction_to,lane,
                    static_cast<int64_t>(step_index_+120u),world_.traffic_tuning())!=TrafficSignalPhase::Red)
                continue;
            const float station=police_signal_line_station(graph,lane,world_.traffic_tuning())-
                tuning_.car_collision_half_length-.8f;
            if (station<4.0f) continue;
            const auto pose=graph.pose(lane,station);
            if (glm::distance(pose.position,car_.position)>160.0f) continue;
            const auto visible=visible_police(pose.position,true);
            if (visible.empty()) continue;
            const auto cop=std::find_if(cars.begin(),cars.end(),[&](const auto& car) {
                return car.police_unit && car.lane_key==visible.front().lane_key &&
                    car.slot==visible.front().slot;
            });
            if (cop==cars.end()) continue;
            police_officer_check_unit_=visible.front();
            wanted_.reset();
            police_red_light_reports_=0;
            police_arrest_reports_=0;
            teleport(pose.position,std::atan2(-pose.tangent.x,-pose.tangent.z));
            on_foot_=false;
            car_.velocity=pose.tangent*6.0f;prev_car_=car_;
            sync_current_vehicle_obstacle();
            advance(1);
            AP_INFO("police officer check: approaching red lane %u in patrol view",lane);
            input.handbrake=0;input.throttle=.2f;
            break;
        }
        return input;
    }
    if (police_officer_check_stage_==1) {
        input.handbrake=0;input.throttle=.2f;
        if (police_red_light_reports_>0) {
            police_officer_check_capture_="red-light";
            advance(2);
        } else if (elapsed>120u) fail("visible red crossing did not report");
        return input;
    }
    if (chosen==cars.end()) { fail("selected officer retired near player");return input; }
    const VehicleAgent cop=*chosen;
    if (police_officer_check_stage_==2) {
        if (elapsed<15u) return input;
        const auto footprint=traffic_vehicle_footprint(TrafficVehicleKind::Police);
        const auto launch=cop.pos-cop.fwd*(footprint.half_length_m+
            tuning_.car_collision_half_length+.20f);
        wanted_.reset();
        police_collision_reports_=0;
        teleport(launch,std::atan2(-cop.fwd.x,-cop.fwd.z));
        on_foot_=false;sync_current_vehicle_obstacle();
        car_.velocity=cop.fwd*(cop.speed_mps+6.0f);
        prev_car_=car_;
        input.handbrake=0;input.throttle=.3f;
        advance(3);
        AP_INFO("police officer check: player approaching cruiser from behind");
        return input;
    }
    if (police_officer_check_stage_==3) {
        input.handbrake=0;input.throttle=.3f;
        if (police_collision_reports_>0) {
            if (!cop.police_pursuit) { fail("struck officer did not engage");return input; }
            police_officer_check_capture_="cruiser-hit";
            const glm::vec3 left{cop.fwd.z,0,-cop.fwd.x};
            const auto target=cop.pos+cop.fwd*12.0f+left*3.5f;
            player_character_=spawn_character(collider_,target.x,target.z,
                std::atan2(-cop.fwd.x,cop.fwd.z));
            prev_player_character_=player_character_;
            on_foot_=true;
            car_.velocity={};prev_car_=car_;
            vehicle_audio_.exit_vehicle();
            sync_current_vehicle_obstacle();
            advance(4);
            input.handbrake=1;input.throttle=0;
            AP_INFO("police officer check: suspect on foot; waiting for actual officer exit");
        } else if (elapsed>180u) fail("player cruiser impact did not report");
        return input;
    }
    const auto phase=cop.officer.phase;
    if (phase==PoliceOfficerPhase::Exiting && cop.officer.transition.tick>=90u &&
        (police_officer_check_phases_&1u)==0) {
        police_officer_check_phases_|=1u;
        police_officer_check_capture_="exiting";
        AP_INFO("police officer check: exit door %.2f, car speed %.3f",
            police_officer_door_open(cop.officer),cop.speed_mps);
    }
    if (police_officer_check_stage_==4) {
        if (phase==PoliceOfficerPhase::Pursuing && !police_officer_check_foot_target_set_) {
            const glm::vec3 left{cop.fwd.z,0,-cop.fwd.x};
            const auto target=cop.officer.pos+cop.fwd*7.0f+left*2.0f;
            player_character_=spawn_character(collider_,target.x,target.z,
                std::atan2(-cop.fwd.x,cop.fwd.z));
            prev_player_character_=player_character_;
            police_officer_check_foot_target_set_=true;
        }
        if (phase==PoliceOfficerPhase::Pursuing && cop.officer.distance_walked_m>2.0f) {
            police_officer_check_phases_|=2u;
            police_officer_check_capture_="on-foot";
            advance(5);
            AP_INFO("police officer check: officer walking toward suspect");
        }
        return input;
    }
    if (police_officer_check_stage_==5) {
        if (police_arrest_reports_==0) return input;
        if (wanted_.level()!=0 || arrested_feedback_s_<=0.0f) {
            fail("arrest did not clear pursuit and show feedback");return input;
        }
        police_officer_check_capture_="arrested";
        advance(6);
        AP_INFO("police officer check: arrested; officer must return to own cruiser");
        return input;
    }
    if (phase==PoliceOfficerPhase::Returning &&
        (police_officer_check_phases_&4u)==0) {
        police_officer_check_phases_|=4u;
        police_officer_check_capture_="returning";
    }
    if (phase==PoliceOfficerPhase::Entering && cop.officer.transition.tick>=175u &&
        (police_officer_check_phases_&8u)==0) {
        police_officer_check_phases_|=8u;
        police_officer_check_capture_="entering";
        AP_INFO("police officer check: re-entry door %.2f, car speed %.3f",
            police_officer_door_open(cop.officer),cop.speed_mps);
    }
    if (phase==PoliceOfficerPhase::Seated && cop.speed_mps>1.0f &&
        police_officer_check_phases_==15u) {
        police_officer_check_capture_="seated-again";
        police_officer_check_done_=true;
        if (police_arrest_reports_!=1u) { fail("arrest event repeated");return input; }
        AP_INFO("police officer check: PASS witnessed red, attributed cruiser hit, exit, walk, arrest, return, enter and drive");
    }
    return input;
}

void App::police_officer_check_camera() {
    if (police_officer_check_stage_==0) return;
    const auto& cars=world_.traffic().vehicles();
    const auto cop=std::find_if(cars.begin(),cars.end(),[&](const auto& car) {
        return car.lane_key==police_officer_check_unit_.lane_key &&
            car.slot==police_officer_check_unit_.slot;
    });
    if (cop==cars.end()) return;
    const glm::vec3 left{cop->fwd.z,0,-cop->fwd.x};
    glm::vec3 target=cop->pos+left*.8f+glm::vec3{0,.8f,0};
    float range=1.0f;
    if (police_officer_check_stage_<4) {
        target=(target+car_.position)*.5f;
        range=std::max(1.0f,glm::distance(car_.position,cop->pos)/14.0f);
    } else if (police_officer_on_foot(cop->officer)) {
        target=(target+cop->officer.pos+glm::vec3{0,.8f,0})*.5f;
    }
    camera_.position=target+(left*7.5f+cop->fwd*4.5f+glm::vec3{0,3.6f,0})*range;
    const auto look=target-camera_.position;
    camera_.yaw=std::atan2(look.x,-look.z);
    camera_.pitch=std::atan2(look.y,glm::length(glm::vec2{look.x,look.z}));
}

void App::capture_police_officer_check() {
    if (police_officer_check_capture_.empty()) return;
    if (!screenshot_path_.empty() && !save_screenshot(
            screenshot_path_+"."+police_officer_check_capture_+".png")) {
        police_officer_check_failed_=true;
        AP_ERROR("police officer check: screenshot failed");
    }
    police_officer_check_capture_.clear();
}

} // namespace apricot
