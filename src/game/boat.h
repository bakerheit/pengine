#pragma once
#include <algorithm>
#include <cmath>
#include <iterator>
#include <glm/gtc/quaternion.hpp>
#include "core/input_frame.h"
#include "physics/terrain_collider.h"

namespace apricot {
// Fixed-step arcade hull: +Z bow, origin at mean sea level. Visual rocking
// never feeds back into navigation, contact, or boarding range.
struct BoatState {
    glm::vec3 position{}, velocity{};
    float yaw=0, speed=0, throttle=0, phase=0, roll=0, pitch=0;
    bool blocked=false;
};
struct BoatBox { glm::vec3 centre,half; };
inline constexpr BoatBox kBoatBoxes[]={
    {{0,-.10f,-1.05f},{.80f,.23f,1.60f}},
    {{-.98f,.40f,-1.05f},{.12f,.30f,1.60f}},
    {{.98f,.40f,-1.05f},{.12f,.30f,1.60f}},
    {{0,.47f,-2.88f},{.77f,.14f,.22f}},
    {{0,.55f,1.28f},{.72f,.28f,.72f}},
    {{0,.42f,2.5f},{.32f,.22f,.5f}}
};
inline glm::quat boat_rotation(const BoatState& s) {
    return glm::angleAxis(s.yaw,glm::vec3{0,1,0});
}
inline glm::vec3 boat_point(const BoatState& s,glm::vec3 p) {
    return s.position+boat_rotation(s)*p;
}
inline glm::vec3 boat_forward(const BoatState& s) {
    return boat_rotation(s)*glm::vec3{0,0,1};
}
inline bool boat_stopped(const BoatState& s) { return glm::length(s.velocity)<.35f; }
inline bool boat_in_boarding_range(const BoatState& s,glm::vec3 feet) {
    if (!boat_stopped(s) || !std::isfinite(feet.x+feet.y+feet.z)) return false;
    const auto p=glm::inverse(boat_rotation(s))*(feet-s.position);
    return std::fabs(p.x)>1.18f && std::fabs(p.x)<3.3f &&
        p.z> -2.6f && p.z< -.5f && p.y>.15f && p.y<1.6f;
}
inline TerrainCollider::GroundHit boat_landing_support(const TerrainCollider& ground,glm::vec3 p) {
    auto hit=ground.probe_down({p.x,1.7f,p.z},1.55f);
    if(!hit.hit || hit.point.y<.15f || hit.normal.y<.7f) return {};
    // Enough dry support for both feet: reject a piling top or a deck edge
    // whose centre probe alone happens to hit. Swimming is not implemented.
    for(const glm::vec2 offset:{glm::vec2{.28f,0},{-.28f,0},{0,.28f},{0,-.28f}}) {
        const auto foot=ground.probe_down({p.x+offset.x,1.7f,p.z+offset.y},1.55f);
        if(!foot.hit || std::fabs(foot.point.y-hit.point.y)>.15f || foot.normal.y<.7f) return {};
    }
    return hit;
}
// Probe the full perimeter and keel, including yaw sweeps. The host must
// exclude this boat's own collider while calling this function.
inline bool boat_clear(const BoatState& from,const BoatState& to,const TerrainCollider& ground) {
    constexpr glm::vec2 stations[]={{-.0f,3.5f},{.35f,2.8f},{.72f,2.f},
        {1.10f,.8f},{1.18f,-.5f},{1.14f,-1.8f},{1.02f,-3.1f},{.70f,-3.6f}};
    for (const auto station:stations) for(float side:{-1.f,1.f}) {
        const glm::vec3 local{station.x*side,0,station.y};
        const auto keel=boat_point(to,local);
        if (ground.height(keel.x,keel.z)>-.60f) return false;
        for(float y:{-.25f,.25f,.70f}) {
            const auto a=boat_point(from,local+glm::vec3{0,y,0});
            const auto b=boat_point(to,local+glm::vec3{0,y,0});
            const auto delta=b-a;
            const float distance=glm::length(delta);
            if(distance>.000001f) {
                const auto hit=ground.raycast(a,delta/distance,distance+.035f);
                if(hit.hit) return false;
            }
            for(const auto& box:ground.static_boxes()) {
                if(!box.enabled) continue;
                const auto p=box.local_point(b);
                const auto bounds=box.collision_bounds();
                if(p.x>=bounds.min.x && p.x<=bounds.max.x &&
                   p.y>=bounds.min.y && p.y<=bounds.max.y &&
                   p.z>=bounds.min.z && p.z<=bounds.max.z) return false;
            }
        }
    }
    // Perimeter edges catch slim pilings between the swept station samples.
    for(float side:{-1.f,1.f}) for(std::size_t i=1;i<std::size(stations);++i) {
        for(float y:{-.25f,.25f,.70f}) {
            const auto a=boat_point(to,{stations[i-1].x*side,y,stations[i-1].y});
            const auto b=boat_point(to,{stations[i].x*side,y,stations[i].y});
            const auto d=b-a;const float length=glm::length(d);
            if(ground.raycast(a,d/length,length).hit) return false;
        }
    }
    for(float y:{-.25f,.25f,.70f}) {
        const auto a=boat_point(to,{-.70f,y,-3.6f});
        const auto b=boat_point(to,{.70f,y,-3.6f});
        if(ground.raycast(a,glm::normalize(b-a),glm::length(b-a)).hit) return false;
    }
    if(ground.height(to.position.x,to.position.z)>-.60f) return false;
    return true;
}
inline BoatState step_boat(const BoatState& previous,const InputFrame& input,
                           const TerrainCollider& ground,float dt) {
    if(!(dt>0) || !std::isfinite(dt)) return previous;
    dt=std::min(dt,1.f/30.f);
    auto s=previous;
    const float pedal=std::clamp(input.throttle-input.brake,-1.f,1.f);
    const float steer=std::clamp(input.steer,-1.f,1.f);
    const bool stopping=input.handbrake>.1f;
    s.throttle=stopping ? 0.f : pedal;
    const float thrust=s.throttle*(s.throttle>0 ? 5.5f:3.f);
    const float drag=(.22f+.010f*std::fabs(s.speed)+(stopping?1.8f:0.f))*s.speed;
    s.speed=std::clamp(s.speed+(thrust-drag)*dt,-5.f,25.f);
    if(std::fabs(s.speed)<.025f && std::fabs(pedal)<.01f) s.speed=0;
    const float authority=std::clamp(s.speed/4.f,-1.f,1.f);
    s.yaw=std::remainder(s.yaw-steer*.65f*authority*dt,glm::two_pi<float>());
    s.velocity=glm::mix(s.velocity,boat_forward(s)*s.speed,std::min(1.f,dt*(stopping?6.f:2.5f)));
    if(s.speed==0 && glm::length(s.velocity)<.025f) s.velocity={};
    s.position+=s.velocity*dt;s.position.y=0;
    s.blocked=!boat_clear(previous,s,ground);
    if(s.blocked) { s.position=previous.position;s.yaw=previous.yaw;s.velocity={};s.speed=0; }
    s.phase=std::fmod(s.phase+dt*1.7f,glm::two_pi<float>());
    s.roll=glm::mix(s.roll,steer*.10f*authority,std::min(1.f,dt*3.f));
    s.pitch=glm::mix(s.pitch,std::min(.065f,std::max(s.speed,0.f)*.004f),std::min(1.f,dt*2.f));
    return s;
}
} // namespace apricot
