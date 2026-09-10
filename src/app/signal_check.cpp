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
    auto target_roadside_fixture=[&](int kind) {
        const std::size_t count=kind==1 ? traffic_visual_.street_lamp_count()
                                        : traffic_visual_.stop_sign_count();
        if(count==0) {
            fail(kind==1 ? "no street lamps" : "no stop signs");
            return;
        }
        float nearest=std::numeric_limits<float>::max();
        std::size_t nearest_index=0;
        for(std::size_t i=0;i<count;++i) {
            const auto status=kind==1 ? traffic_visual_.street_lamp_status(i)
                                      : traffic_visual_.stop_sign_status(i);
            const float distance=glm::distance(status.base,car_.position);
            if(distance<nearest) { nearest=distance;nearest_index=i; }
        }
        signal_check_fixture_kind_=kind;
        signal_check_index_=nearest_index;
        const auto status=kind==1
            ? traffic_visual_.street_lamp_status(nearest_index)
            : traffic_visual_.stop_sign_status(nearest_index);
        signal_check_base_=status.base;
        signal_check_direction_=-(
            status.standing[0].rotation*glm::vec3{0,0,1});
        signal_check_direction_.y=0;
        signal_check_direction_=glm::normalize(signal_check_direction_);
        AP_INFO("signal check: target %s %zu lane %llu at %.2f %.2f %.2f",
            kind==1 ? "street lamp" : "stop sign",nearest_index,
            static_cast<unsigned long long>(status.damage.lane_key),
            status.base.x,status.base.y,status.base.z);
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
        if(status.damage.broken && status.debris.age_seconds>.38f &&
            status.debris.age_seconds<.39f)signal_check_capture_="falling";
    }
    if(signal_check_ticks_==480) {
        const auto status=traffic_visual_.signal_status(signal_check_index_);
        if(!status.damage.broken || status.debris.age_seconds<1.5f)
            fail("hard impact did not fell pole");
        if(collider_.static_boxes()[status.collider_id].enabled)fail("upright collision still enabled");
        if(glm::dot(car_.position-signal_check_base_,signal_check_direction_)<2.f)
            fail("vehicle did not pass former pole");
        std::size_t separated_parts=0;
        for(std::size_t n=0;n<status.nodes.size();++n) {
            const auto* actual=scene_.get(status.nodes[n]);
            const auto& piece=status.debris.pieces[n];
            if(!actual || glm::distance(actual->local.position,piece.pose.position)>.002f)
                fail("signal debris pose mismatch");
            if(!piece.touched_ground)fail("signal debris froze above the ground");
            if(n>0 && actual &&
               glm::distance(actual->local.position,status.standing[n].position)>.15f)
                ++separated_parts;
            if(n>=3 && n<=5 && actual && actual->renderable.tint.a>1.f)
                fail("broken lens still emissive");
        }
        if(separated_parts<5)fail("signal pieces did not break apart");
        const auto* pole=scene_.get(status.nodes[0]);
        if(pole && status.debris.pieces[0].sleeping &&
           !roadside_debris_resting_orientation(
               status.debris.pieces[0],glm::vec3{0,1,0}))
            fail("traffic signal pole stopped before falling flat");
        signal_check_capture_="fallen";
        AP_INFO("signal check: hard hit passed pole; %zu parts separated, all lenses dark",
                separated_parts);
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
    if(signal_check_ticks_==840) {
        const auto status=traffic_visual_.signal_status(signal_check_index_);
        for(std::size_t n=0;n<status.nodes.size();++n) {
            const auto& piece=status.debris.pieces[n];
            if(piece.sleeping && !roadside_debris_resting_orientation(
                                     piece,glm::vec3{0,1,0}))
                fail("traffic signal debris balanced above the road");
        }
        signal_check_capture_="grounded";
        AP_INFO("signal check: no traffic signal piece slept without support");
    }
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
        target_roadside_fixture(1);
        if(!signal_check_failed_)launch(5.f,12.f);
    }
    if(signal_check_ticks_==1200) {
        const auto status=traffic_visual_.street_lamp_status(signal_check_index_);
        if(!status.damage.broken ||
           collider_.static_boxes()[status.collider_id].enabled)
            fail("street lamp did not break and release collision");
        std::size_t separated=0;
        for(std::size_t n=0;n<status.part_count;++n) {
            const auto* actual=scene_.get(status.nodes[n]);
            const auto& piece=status.debris.pieces[n];
            if(!actual || glm::distance(actual->local.position,piece.pose.position)>.002f)
                fail("street lamp debris pose mismatch");
            if(!piece.touched_ground)fail("street lamp debris froze above the ground");
            if(n>0 && actual &&
               glm::distance(actual->local.position,status.standing[n].position)>.15f)
                ++separated;
        }
        const auto* bulb=scene_.get(status.nodes[3]);
        if(separated<2)fail("street lamp pieces did not separate");
        const auto* pole=scene_.get(status.nodes[0]);
        if(pole && status.debris.pieces[0].sleeping &&
           !roadside_debris_resting_orientation(
               status.debris.pieces[0],glm::vec3{0,1,0}))
            fail("street lamp pole stopped before falling flat");
        if(bulb && bulb->renderable.tint.a>1.f)
            fail("broken street lamp still emissive");
        signal_check_capture_="lamp-fractured";
        AP_INFO("signal check: street lamp fell, went dark and split into %zu loose parts",
                separated);
    }
    if(signal_check_ticks_==1202) {
        target_roadside_fixture(2);
        if(!signal_check_failed_)launch(5.f,12.f);
    }
    if(signal_check_ticks_==1440) {
        const auto status=traffic_visual_.stop_sign_status(signal_check_index_);
        if(!status.damage.broken ||
           collider_.static_boxes()[status.collider_id].enabled)
            fail("stop sign did not break and release collision");
        for(std::size_t n=0;n<status.part_count;++n) {
            const auto* actual=scene_.get(status.nodes[n]);
            const auto& piece=status.debris.pieces[n];
            if(!actual || glm::distance(actual->local.position,piece.pose.position)>.002f)
                fail("stop sign debris pose mismatch");
            if(!piece.touched_ground)fail("stop sign debris froze above the ground");
        }
        const auto& pole_piece=status.debris.pieces[kRoadSignPolePart];
        const auto& backing=status.debris.pieces[kRoadSignBackingPart];
        if(glm::distance(pole_piece.pose.position,backing.pose.position)<.15f &&
           std::fabs(glm::dot(pole_piece.pose.rotation,
                              backing.pose.rotation))>.98f)
            fail("stop sign plate did not separate from pole");
        for(std::size_t layer : {kRoadSignWhitePart,kRoadSignRedPart}) {
            const auto& piece=status.debris.pieces[layer];
            if(glm::distance(piece.pose.position,backing.pose.position)>.0001f ||
               std::fabs(glm::dot(piece.pose.rotation,
                                  backing.pose.rotation))<.99999f)
                fail("stop sign lettering separated from plate");
        }
        const auto* pole=scene_.get(status.nodes[0]);
        if(pole && status.debris.pieces[0].sleeping &&
           !roadside_debris_resting_orientation(
               status.debris.pieces[0],glm::vec3{0,1,0}))
            fail("stop sign pole stopped before falling flat");
        signal_check_capture_="stop-fractured";
        AP_INFO("signal check: stop sign split into pole and intact lettered plate");
    }
    if(signal_check_ticks_==2040) {
        auto settled=[&](const TrafficVisual::RoadsideFixtureStatus& status,
                         const char* label) {
            for(std::size_t n=0;n<status.part_count;++n) {
                const auto& piece=status.debris.pieces[n];
                if(!piece.sleeping)fail(label);
                if(!roadside_debris_resting_orientation(piece,glm::vec3{0,1,0}))
                    fail("roadside fixture debris balanced above the ground");
            }
        };
        for(std::size_t i=0;i<traffic_visual_.street_lamp_count();++i) {
            const auto status=traffic_visual_.street_lamp_status(i);
            if(status.damage.broken)
                settled(status,"street lamp debris did not finish settling");
        }
        for(std::size_t i=0;i<traffic_visual_.stop_sign_count();++i) {
            const auto status=traffic_visual_.stop_sign_status(i);
            if(status.damage.broken)
                settled(status,"stop sign debris did not finish settling");
        }
        AP_INFO("signal check: street lamp and stop sign debris fully grounded");
    }
    if(signal_check_ticks_==2700) {
        bool expired_lamp=false,expired_stop=false;
        for(std::size_t i=0;i<traffic_visual_.street_lamp_count();++i) {
            const auto status=traffic_visual_.street_lamp_status(i);
            if(status.damage.broken) {
                expired_lamp|=status.debris.expired;
                for(std::size_t n=0;n<status.part_count;++n) {
                    const auto* node=scene_.get(status.nodes[n]);
                    if(node && node->visible)fail("expired street lamp debris still visible");
                }
            }
        }
        for(std::size_t i=0;i<traffic_visual_.stop_sign_count();++i) {
            const auto status=traffic_visual_.stop_sign_status(i);
            if(status.damage.broken) {
                expired_stop|=status.debris.expired;
                for(std::size_t n=0;n<status.part_count;++n) {
                    const auto* node=scene_.get(status.nodes[n]);
                    if(node && node->visible)fail("expired stop sign debris still visible");
                }
            }
        }
        if(!expired_lamp || !expired_stop)fail("roadside debris did not expire");
        signal_check_capture_="debris-cleared";
        AP_INFO("signal check: settled debris disappeared after %.0f seconds",
                static_cast<double>(kRoadsideDebrisLifetimeSeconds));
    }
    if(signal_check_ticks_==2760)traffic_visual_.reset_signals(scene_,collider_);
    if(signal_check_ticks_==2820) {
        for(std::size_t i=0;i<traffic_visual_.signal_head_count();++i) {
            const auto status=traffic_visual_.signal_status(i);
            if(status.damage.broken ||
               !collider_.static_boxes()[status.collider_id].enabled)
                fail("traffic signal reset failed");
        }
        for(std::size_t i=0;i<traffic_visual_.street_lamp_count();++i) {
            const auto status=traffic_visual_.street_lamp_status(i);
            if(status.damage.broken ||
               !collider_.static_boxes()[status.collider_id].enabled)
                fail("street lamp reset failed");
        }
        for(std::size_t i=0;i<traffic_visual_.stop_sign_count();++i) {
            const auto status=traffic_visual_.stop_sign_status(i);
            if(status.damage.broken ||
               !collider_.static_boxes()[status.collider_id].enabled)
                fail("stop sign reset failed");
        }
        signal_check_capture_="all-reset";
        signal_check_done_=true;
        if(!signal_check_failed_)AP_INFO("signal check: PASS signals, street lamps and stop signs fall under physics, settle, disappear, release collision and reset");
    }
    if(signal_check_ticks_>=400 || signal_check_ticks_<90)input.handbrake=1;
    // The return contact must coast through, not sit under the parking brake.
    if((signal_check_ticks_>=720 && signal_check_ticks_<900) ||
       (signal_check_ticks_>=960 && signal_check_ticks_<1440)) {
        input.handbrake=0;input.throttle=.25f;
    }
    ++signal_check_ticks_;
    return input;
}

void App::signal_check_camera() {
    if(signal_check_ticks_==0)return;
    const auto side=glm::cross(signal_check_direction_,glm::vec3{0,1,0});
    glm::vec3 fixture_top=signal_check_base_+glm::vec3{0,2,0};
    if(signal_check_fixture_kind_==0) {
        const auto status=traffic_visual_.signal_status(signal_check_index_);
        fixture_top=status.standing[2].position;
    } else if(signal_check_fixture_kind_==1) {
        const auto status=traffic_visual_.street_lamp_status(signal_check_index_);
        fixture_top=status.standing[2].position;
    }
    const auto aim=(signal_check_base_+fixture_top)*.5f+
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
