#include "app/app.h"

#include <algorithm>

#include "core/log.h"

namespace apricot {

// Only stages the player. The selected driver brakes, accumulates frustration,
// requests its horn and resumes through the ordinary Crowd and audio paths.
InputFrame App::traffic_horn_check_input() {
    InputFrame input; input.handbrake=1.f;
    ++traffic_horn_check_tick_;
    const auto fail=[&](const char* reason) {
        traffic_horn_check_failed_=true;
        AP_ERROR("traffic horn check: %s (stage %d)",reason,traffic_horn_check_stage_);
    };
    if (traffic_horn_check_done_ || traffic_horn_check_failed_ || traffic_horn_check_tick_<120)
        return input;
    const auto& cars=world_.traffic().vehicles();
    const auto& graph=world_.lanes();
    if (traffic_horn_check_stage_==0) {
        if (!audio_device_.running() || traffic_horn_audio_.loaded_clip_count()!=3) {
            fail("audio device or recordings unavailable");return input;
        }
        for (const auto& driver:cars) {
            if (driver.police_unit || !graph.valid(driver.lane) || driver.turn_from_lane!=kInvalidLane ||
                driver.speed_mps>7.f || driver.speed_mps<1.f ||
                driver.profile.honk_after>3.f || vehicle_engine_failed(driver.mechanical)) continue;
            const auto& lane=graph.lane(driver.lane);
            const float clearance=traffic_junction_clearance(graph,lane.junction_to,world_.traffic_tuning());
            if (driver.dist_along_m<20.f || lane.length_m-driver.dist_along_m<clearance+65.f ||
                glm::distance(driver.pos,car_.position)>120.f) continue;
            const bool leader=std::any_of(cars.begin(),cars.end(),[&](const auto& other) {
                return other.lane==driver.lane && other.dist_along_m>driver.dist_along_m &&
                    other.dist_along_m-driver.dist_along_m<45.f;
            });
            if (leader) continue;
            traffic_horn_check_driver_={driver.lane_key,driver.slot};
            const auto pose=graph.pose(driver.lane,driver.dist_along_m+22.f);
            AP_INFO("traffic horn check: blocking driver %llu/%u, profile %u, horn %zu",
                static_cast<unsigned long long>(driver.lane_key),driver.slot,
                static_cast<unsigned>(driver.profile.kind),traffic_horn_clip(driver));
            teleport(pose.position,std::atan2(-pose.tangent.x,-pose.tangent.z));
            on_foot_=false; traffic_horn_check_stage_=1;
            traffic_horn_check_stage_tick_=traffic_horn_check_tick_;
            traffic_horn_check_seen_=traffic_horn_audio_.play_count();
            break;
        }
        if (traffic_horn_check_tick_>2400 && traffic_horn_check_stage_==0)
            fail("no suitable nearby driver");
        return input;
    }
    const auto driver=std::find_if(cars.begin(),cars.end(),[&](const auto& car){
        return VisiblePoliceIdentity{car.lane_key,car.slot}==traffic_horn_check_driver_;
    });
    if (driver==cars.end()) {fail("selected driver retired");return input;}
    const uint64_t elapsed=traffic_horn_check_tick_-traffic_horn_check_stage_tick_;
    if (elapsed>2400) {
        AP_ERROR("traffic horn check: driver speed %.2f, delay %.2f, player gap %.2f",
            driver->speed_mps,driver->delay_seconds,glm::distance(driver->pos,car_.position));
        fail("stage timed out");return input;
    }
    if (traffic_horn_audio_.play_count()!=traffic_horn_check_seen_) {
        traffic_horn_check_seen_=traffic_horn_audio_.play_count();
        const auto& event=traffic_horn_audio_.last_event();
        if (event.driver==traffic_horn_check_driver_) {
            if (traffic_horn_check_stage_==3) {
                AP_ERROR("traffic horn check: new obstruction after release; speed %.2f, delay %.2f, gap %.2f, lane %u, to end %.2f",
                    driver->speed_mps,driver->delay_seconds,glm::distance(driver->pos,car_.position),
                    driver->lane,graph.length(driver->lane)-driver->dist_along_m);
                traffic_horn_check_capture_="blocked-after-release";
                fail("driver blocked again before release check finished");return input;
            }
            AP_INFO("traffic horn check: live horn %zu at step %llu, speed %.2f, delay %.2f, listener %.2fm",
                event.clip,static_cast<unsigned long long>(event.step),driver->speed_mps,
                driver->delay_seconds,glm::distance(camera_.position,event.position));
            traffic_horn_check_capture_="honk";
            traffic_horn_check_stage_=2; traffic_horn_check_stage_tick_=traffic_horn_check_tick_;
            return input;
        }
    }
    if (traffic_horn_check_stage_==2 && elapsed>=180) {
        const glm::vec3 right{-driver->fwd.z,0,driver->fwd.x};
        teleport(car_.position+right*18.f,std::atan2(-driver->fwd.x,-driver->fwd.z));
        traffic_horn_check_stage_=3; traffic_horn_check_stage_tick_=traffic_horn_check_tick_;
        AP_INFO("traffic horn check: player cleared the lane");
    } else if (traffic_horn_check_stage_==3 && elapsed>=480 && driver->speed_mps>2.f) {
        traffic_horn_check_done_=true;traffic_horn_check_capture_="released";
        AP_INFO("traffic horn check: PASS; driver resumed at %.2fm/s, no horn after release, device %uHz, dropped commands %llu",
            driver->speed_mps,audio_device_.sample_rate(),
            static_cast<unsigned long long>(audio_device_.mixer().dropped_commands()));
    }
    if (traffic_horn_check_stage_==3 && elapsed%120u==0)
        AP_INFO("traffic horn check: release %.1fs, driver speed %.2f, delay %.2f, gap %.2f",
            double(elapsed)*kSimDt,driver->speed_mps,driver->delay_seconds,
            glm::distance(driver->pos,car_.position));
    return input;
}

void App::traffic_horn_check_camera() {
    if (traffic_horn_check_stage_==0) return;
    const auto& cars=world_.traffic().vehicles();
    const auto driver=std::find_if(cars.begin(),cars.end(),[&](const auto& car){
        return VisiblePoliceIdentity{car.lane_key,car.slot}==traffic_horn_check_driver_;
    });
    if (driver==cars.end()) return;
    const glm::vec3 right{-driver->fwd.z,0,driver->fwd.x};
    const auto target=driver->pos+driver->fwd*4.f;
    camera_.position=target+right*8.f-driver->fwd*6.f+glm::vec3{0,5,0};
    const auto look=target-camera_.position;
    camera_.yaw=std::atan2(look.x,-look.z);
    camera_.pitch=std::atan2(look.y,glm::length(glm::vec2{look.x,look.z}));
}

void App::capture_traffic_horn_check() {
    if (traffic_horn_check_capture_.empty()) return;
    if (!screenshot_path_.empty() && !save_screenshot(screenshot_path_+"."+traffic_horn_check_capture_+".png"))
        traffic_horn_check_failed_=true;
    traffic_horn_check_capture_.clear();
}

} // namespace apricot
