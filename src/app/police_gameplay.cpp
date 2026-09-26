#include "app/app.h"

#include <algorithm>

#include "core/log.h"

namespace apricot {

bool App::player_has_drawn_weapon() const {
    return on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
        !vehicle_transition_.active() && !boat_transition_.active() &&
        // A LIT BOTTLE COUNTS. The offence the city is reporting is "that
        // person is holding something they are about to hurt somebody with",
        // and a molotov out in the open in the street is exactly that. Each
        // weapon reports its own draw clock, because they are different
        // lengths and a shared one would have to be wrong for one of them.
        ((weapon_use_.equipped == WeaponId::Pistol &&
          weapon_use_.equip_blend >= 0.80f) ||
         (weapon_wheel_.equipped == WeaponId::Molotov &&
          molotov_use_.equip_blend >= 0.80f));
}

float App::current_speed_limit_mps() const {
    if (on_foot_ || in_aircraft_ || in_helicopter_ || in_boat_) return 0.0f;
    const glm::vec3 forward3 = car_.orientation * glm::vec3{0.0f, 0.0f, -1.0f};
    const auto lane = world_.lanes().nearest_lane_along(
        {car_.position.x, car_.position.z}, {forward3.x, forward3.z}, 8.0f);
    return lane.valid() ? world_.lanes().lane(lane.lane).speed_limit_mps : 0.0f;
}

void App::check_police_driving_offenses() {
    PoliceDrivingSample sample;
    sample.vehicle_identity=car_.mechanical_key;
    sample.position=car_.position;
    sample.forward=car_.orientation*glm::vec3{0,0,-1};
    sample.velocity=car_.velocity;
    sample.half_length_m=tuning_.car_collision_half_length;
    sample.step=static_cast<int64_t>(step_index_+1u);
    sample.driving=!on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
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
        if (report.kind == PoliceOffenseReport::Kind::RedLight) {
            ++police_red_light_reports_;
            AP_INFO("police witnessed red-light crossing: lane %u; wanted %d",
                report.incoming,wanted_.level());
        } else if (report.kind == PoliceOffenseReport::Kind::StopSign) {
            ++police_stop_sign_reports_;
            AP_INFO("police witnessed stop-sign run: lane %u; wanted %d",
                report.incoming,wanted_.level());
        } else if (report.kind == PoliceOffenseReport::Kind::Speeding) {
            ++police_speeding_reports_;
            AP_INFO("police witnessed speeding: %.1f mph in %.1f mph zone; wanted %d",
                static_cast<double>(report.observed_speed_mps * 2.2369363f),
                static_cast<double>(report.speed_limit_mps * 2.2369363f),
                wanted_.level());
        }
    }
}

void App::check_police_armed_offense(bool player_armed) {
    std::vector<PoliceOffenseWitness> witnesses;
    if (player_armed) {
        const auto visible = visible_police(player_character_.position, true);
        for (const auto& agent : world_.traffic().vehicles()) {
            if (!agent.police_unit || std::find(visible.begin(), visible.end(),
                    VisiblePoliceIdentity{agent.lane_key, agent.slot}) == visible.end())
                continue;
            witnesses.push_back({police_officer_eye_position(agent),
                police_officer_forward(agent), true});
        }
    }
    const auto report = police_offenses_.observe_armed(
        player_character_.position, player_armed, witnesses,
        world_.traffic().police_tuning());
    if (!report) return;
    wanted_.add_heat(report.heat, report.crime);
    ++police_armed_reports_;
    AP_INFO("police witnessed armed threat; wanted %d", wanted_.level());
}

void App::check_police_collision_offenses() {
    if (on_foot_ || in_aircraft_ || in_helicopter_ || in_boat_ || vehicle_transition_.active()) return;
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
    const bool arrestable=on_foot_ && !in_aircraft_ && !in_helicopter_ && !in_boat_ &&
        !vehicle_transition_.active() && !boat_transition_.active() &&
        !player_has_drawn_weapon();
    const auto event=police_arrest_.observe(step_index_,arrestable,wanted_.level(),
        player_character_.position,world_.traffic().vehicles(),visible);
    if (!event) return;
    wanted_.reset();
    police_escalation_.reset();
    police_stop_feedback_s_=0.0f;
    world_.set_police_context(0,player_focus_position());
    arrested_feedback_s_=GameUi::kArrestedDisplaySeconds;
    ++police_arrest_reports_;
    AP_INFO("Arrested: officer key %llu slot %u held within %.1f m for %.1f s",
        static_cast<unsigned long long>(event->officer.lane_key),event->officer.slot,
        kPoliceArrestRangeM,kPoliceArrestHoldSeconds);
}

// Bodies the player's car left in the road, drained on the step they happen.
//
// The crowd decides who dies — it owns the footprint, the closing speed and
// city/body_damage.h's curve — and this decides what it costs. Keeping the
// split means the sim never grows an opinion about heat, and it is the same
// split check_police_collision_offenses() already makes for cruisers.
void App::check_pedestrian_casualties() {
    for (const auto& kill : world_.traffic().ped_run_downs()) {
        ++pedestrian_kill_reports_;
        police_escalation_.record_civilian_kill();
        wanted_.add_heat(kCivilianRunDownHeat,
                         WantedSystem::Crime::VehicularAssault);
        AP_INFO("ran down pedestrian %llu/%u at %.1f mph; wanted %d",
            static_cast<unsigned long long>(kill.lane_key), kill.slot,
            static_cast<double>(kill.closing_speed_mps * 2.2369363f),
            wanted_.level());
    }
}

void App::check_police_shots() {
    if (!on_foot_ || !player_vitals_.alive()) return;
    const glm::vec3 torso = player_character_.position + glm::vec3{0.0f, 1.05f, 0.0f};
    for (const PoliceShotEvent& shot : world_.traffic().police_shots()) {
        ++police_shot_reports_;
        VoiceParams sound;
        sound.category = Category::Impacts;
        sound.gain = 0.72f;
        sound.spatial = true;
        sound.position = shot.origin;
        audio_device_.mixer().play_oneshot(&weapon_shot_clip_, sound);

        const glm::vec3 travel = shot.end - shot.origin;
        const float distance = glm::length(travel);
        if (!(distance > 0.01f)) continue;
        const auto world_hit = collider_.raycast(
            shot.origin, travel / distance, distance);
        if (world_hit.hit && world_hit.distance < distance - 0.08f) continue;
        if (!police_shot_hits_player(shot, torso)) continue;

        weapon_visual_.show_blood(torso, travel / distance,
            shot.lane_key ^ (static_cast<uint64_t>(shot.ordinal) << 32));
        // One door for every cause of death; src/app/player_damage.cpp owns
        // what dying means. This used to inline the respawn right here, which
        // is why there was only ever one thing in the city that could kill
        // you.
        if (damage_player(kPoliceBulletDamage, "a police round")) break;
    }
}

// This check scripts the player's placement/input only. Police perception,
// contacts, braking, walking and every door phase use the live game path.
InputFrame App::police_officer_check_input() {
    if (police_pursuit_check_) return police_pursuit_check_input();
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
            police_armed_reports_=0;
            police_shot_reports_=0;
            player_vitals_.revive();
            economy_.owned_weapons|=weapon_bit(WeaponId::Pistol); // QA owns it
            weapon_wheel_.equipped=WeaponId::Pistol;
            advance(5);
            AP_INFO("police officer check: officer walking toward suspect; drawing pistol");
        }
        return input;
    }
    if (police_officer_check_stage_==5) {
        if (police_shot_reports_==0) return input;
        if (police_armed_reports_==0 || !cop.officer.armed) {
            fail("officer fired without witnessed armed response");return input;
        }
        police_officer_check_capture_="armed-fire";
        advance(6);
        AP_INFO("police officer check: armed threat witnessed and officer fired");
        return input;
    }
    if (police_officer_check_stage_==6) {
        if (elapsed<30u) return input;
        weapon_wheel_.equipped=WeaponId::Unarmed;
        advance(7);
        AP_INFO("police officer check: armed pose captured; holstering for arrest");
        return input;
    }
    if (police_officer_check_stage_==7) {
        if (police_arrest_reports_==0) return input;
        if (wanted_.level()!=0 || arrested_feedback_s_<=0.0f) {
            fail("arrest did not clear pursuit and show feedback");return input;
        }
        police_officer_check_capture_="arrested";
        advance(8);
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
        if (police_armed_reports_!=1u || police_shot_reports_==0u) {
            fail("armed response did not report exactly once and fire");return input;
        }
        AP_INFO("police officer check: PASS witnessed red, attributed cruiser hit, exit, walk, armed fire, arrest, return, enter and drive");
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
    if (police_pursuit_check_ && (police_officer_check_stage_ < 4 ||
        police_officer_check_capture_ == "traffic-resumed")) {
        const auto initial = world_.lanes().pose(police_pursuit_check_start_lane_, 0.0f);
        const glm::vec3 left{initial.tangent.z, 0, -initial.tangent.x};
        auto target = cop->pos + glm::vec3{0, 0.8f, 0};
        float range = 1.f;
        if (police_officer_check_capture_ == "traffic-pulled-aside" ||
            police_officer_check_capture_ == "traffic-resumed") {
            target = (cop->pos + police_pursuit_check_yield_position_) * .5f + glm::vec3{0,.8f,0};
            range = std::max(1.f, glm::distance(cop->pos, police_pursuit_check_yield_position_) / 16.f);
        }
        camera_.position = target + (left * 12.0f - initial.tangent * 10.0f + glm::vec3{0, 8, 0}) * range;
        const auto look = target - camera_.position;
        camera_.yaw = std::atan2(look.x, -look.z);
        camera_.pitch = std::atan2(look.y, glm::length(glm::vec2{look.x, look.z}));
        return;
    }
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
