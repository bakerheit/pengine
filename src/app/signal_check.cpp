#include "app/app.h"
#include "core/log.h"
#include <limits>

namespace apricot {

// Only QA placement/launch is scripted. Contacts, damage, collider release,
// animation, lighting and the outbound/return streaming use production paths.
InputFrame App::signal_check_input() {
    InputFrame input;
    auto fail=[&](const char* reason) {
        AP_ERROR("signal check: %s",reason);signal_check_failed_=true;
    };
    if(signal_check_failed_ || signal_check_done_)return input;
    if(signal_check_ticks_==0) {
        float nearest=std::numeric_limits<float>::max();
        for(std::size_t i=0;i<traffic_visual_.signal_head_count();++i) {
            const auto status=traffic_visual_.signal_status(i);
            const float distance=glm::distance(status.base,car_.position);
            if(distance<nearest) { nearest=distance;signal_check_index_=i; }
        }
        if(nearest==std::numeric_limits<float>::max()) { fail("no signals");return input; }
        const auto status=traffic_visual_.signal_status(signal_check_index_);
        signal_check_base_=status.base;
        signal_check_direction_=-(status.standing[2].rotation*glm::vec3{0,0,1});
        AP_INFO("signal check: target %zu lane %llu at %.2f %.2f %.2f",
            signal_check_index_,static_cast<unsigned long long>(status.damage.lane_key),
            status.base.x,status.base.y,status.base.z);
    }
    auto launch=[&](float distance,float speed) {
        const auto p=signal_check_base_-signal_check_direction_*distance;
        const float yaw=std::atan2(-signal_check_direction_.x,-signal_check_direction_.z);
        teleport(p,yaw);
        on_foot_=false;in_aircraft_=false;in_boat_=false;
        car_.velocity=signal_check_direction_*speed;
        prev_car_=car_;
    };
    if(signal_check_ticks_==0)launch(tuning_.car_collision_half_length+.16f,0);
    if(signal_check_ticks_==60)signal_check_capture_="before";
    if(signal_check_ticks_==90)launch(tuning_.car_collision_half_length+.14f,2);
    if(signal_check_ticks_==180) {
        const auto status=traffic_visual_.signal_status(signal_check_index_);
        if(status.damage.broken || !collider_.static_boxes()[status.collider_id].enabled)
            fail("low-speed brush released pole");
        const auto delta=car_.position-signal_check_base_;
        if(glm::dot(delta,signal_check_direction_)>-.5f)fail("intact pole did not block brush");
        signal_check_capture_="brush";
        AP_INFO("signal check: low-speed brush stayed solid");
    }
    if(signal_check_ticks_==240)launch(5.f,12.f);
    if(signal_check_ticks_>240 && signal_check_ticks_<350) {
        const auto status=traffic_visual_.signal_status(signal_check_index_);
        if(status.damage.broken && status.damage.fall_seconds>.38f &&
            status.damage.fall_seconds<.39f)signal_check_capture_="falling";
    }
    if(signal_check_ticks_==480) {
        const auto status=traffic_visual_.signal_status(signal_check_index_);
        if(!status.damage.broken || status.damage.fall_seconds<kSignalFallSeconds)
            fail("hard impact did not fell pole");
        if(collider_.static_boxes()[status.collider_id].enabled)fail("upright collision still enabled");
        if(glm::dot(car_.position-signal_check_base_,signal_check_direction_)<2.f)
            fail("vehicle did not pass former pole");
        for(std::size_t n=0;n<status.nodes.size();++n) {
            const auto expected=status.damage.pose(status.base)*status.standing[n];
            const auto* actual=scene_.get(status.nodes[n]);
            if(!actual || glm::distance(actual->local.position,expected.position)>.002f)
                fail("detached signal component");
            if(n>=3 && n<=5 && actual && actual->renderable.tint.a>1.f)
                fail("broken lens still emissive");
        }
        signal_check_capture_="fallen";
        AP_INFO("signal check: hard hit passed pole; seven parts attached, all lenses dark");
    }
    if(signal_check_ticks_==540) {
        teleport({-1800,0,-1800},0);
        AP_INFO("signal check: streamed out 3km away");
    }
    if(signal_check_ticks_==600)launch(12.f,0);
    if(signal_check_ticks_==660) {
        const auto status=traffic_visual_.signal_status(signal_check_index_);
        if(!status.damage.broken || collider_.static_boxes()[status.collider_id].enabled)
            fail("streaming restored broken pole");
        signal_check_capture_="returned";
        AP_INFO("signal check: streamed back; same broken state and disabled collider");
    }
    if(signal_check_ticks_==720)launch(tuning_.car_collision_half_length+.14f,2);
    if(signal_check_ticks_==900) {
        if(glm::dot(car_.position-signal_check_base_,signal_check_direction_)<.25f)
            fail("returned pole has invisible blocker");
        AP_INFO("signal check: low-speed return crossed old upright footprint");
        traffic_visual_.reset_signals(scene_,collider_);
        launch(10,0);
    }
    if(signal_check_ticks_==960) {
        const auto status=traffic_visual_.signal_status(signal_check_index_);
        if(status.damage.broken || !collider_.static_boxes()[status.collider_id].enabled)
            fail("new-session reset failed");
        signal_check_capture_="reset";
        signal_check_done_=true;
        if(!signal_check_failed_)AP_INFO("signal check: PASS brush, hard hit, attached dark heads, streaming, collision and reset");
    }
    if(signal_check_ticks_>=400 || signal_check_ticks_<90)input.handbrake=1;
    // The return contact must coast through, not sit under the parking brake.
    if(signal_check_ticks_>=720 && signal_check_ticks_<900) {
        input.handbrake=0;input.throttle=.25f;
    }
    ++signal_check_ticks_;
    return input;
}

void App::signal_check_camera() {
    if(signal_check_ticks_==0)return;
    const auto side=glm::cross(signal_check_direction_,glm::vec3{0,1,0});
    const auto status=traffic_visual_.signal_status(signal_check_index_);
    const auto aim=(signal_check_base_+status.standing[2].position)*.5f+
        signal_check_direction_*1.5f;
    camera_.position=aim-signal_check_direction_*17.f+side*15.f+glm::vec3{0,9,0};
    const auto dir=aim-camera_.position;
    camera_.yaw=std::atan2(dir.x,-dir.z);
    camera_.pitch=std::atan2(dir.y,glm::length(glm::vec2{dir.x,dir.z}));
}

void App::capture_signal_check() {
    if(signal_check_capture_.empty())return;
    if(!screenshot_path_.empty() && !save_screenshot(screenshot_path_+"."+signal_check_capture_+".bmp"))
        signal_check_failed_=true;
    signal_check_capture_.clear();
}

} // namespace apricot
