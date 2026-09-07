#pragma once
#include <algorithm>
#include <cmath>
#include "physics/vehicle.h"

namespace apricot {
// Trailer origin is at the ground below the box centre. Like the car, -Z is
// forward. Asset +Z is flipped only by the renderer. Distances are metres.
inline constexpr glm::vec3 kTrailerKingpin{0,1.28f,-4.f};
inline constexpr float kTrailerAxleZ=3.6f, kTrailerWheelRadius=.5f;
inline constexpr float kTrailerMaxAngle=1.134464f; // 65 degrees: cab clearance
inline constexpr glm::vec2 kFreightTrailerHome{515.f,2348.f};
inline constexpr float kFreightTrailerYaw=3.1415926536f;
struct TrailerState {
    glm::vec3 position{};
    float yaw=0, pitch=0, wheel_spin=0;
    bool attached=false, blocked=false;
};
inline glm::quat trailer_rotation(const TrailerState& s) {
    return glm::angleAxis(s.yaw,glm::vec3{0,1,0}) *
           glm::angleAxis(s.pitch,glm::vec3{1,0,0});
}
inline glm::vec3 trailer_point(const TrailerState& s,glm::vec3 p) {
    return s.position+trailer_rotation(s)*p;
}
inline float trailer_angle(float a) { return std::remainder(a,6.2831853072f); }
inline float tractor_yaw(const VehicleState& car) {
    const auto back=car.orientation*glm::vec3{0,0,1};
    return std::atan2(back.x,back.z);
}
inline glm::vec3 tractor_hitch(const VehicleState& car,const VehicleTuning& tuning) {
    return car.position+car.orientation*glm::vec3{0,
        1.28f-.50f-static_suspension_length(tuning)-tuning.com_height_above_mount,1.90f};
}
enum class TrailerCoupling { Ready, Moving, Misaligned, OutOfReach, Unsupported };
inline TrailerCoupling trailer_coupling(const VehicleState& car,const VehicleTuning& tuning,
                                        const TrailerState& trailer) {
    if (glm::length(car.velocity)>.35f || glm::length(car.angular_velocity)>.15f)
        return TrailerCoupling::Moving;
    if ((car.orientation*glm::vec3{0,1,0}).y<.9f || std::fabs(trailer.pitch)>.20f)
        return TrailerCoupling::Unsupported;
    if (std::fabs(trailer_angle(tractor_yaw(car)-trailer.yaw))>.20f)
        return TrailerCoupling::Misaligned;
    const auto d=tractor_hitch(car,tuning)-trailer_point(trailer,kTrailerKingpin);
    if (glm::length(glm::vec2{d.x,d.z})>.65f || std::fabs(d.y)>.25f)
        return TrailerCoupling::OutOfReach;
    return TrailerCoupling::Ready;
}
inline bool trailer_stopped(const VehicleState& car) {
    return glm::length(car.velocity)<.35f && glm::length(car.angular_velocity)<.15f;
}
inline TrailerState spawn_trailer(const TerrainCollider& ground,glm::vec2 xz,float yaw) {
    TrailerState s;s.position={xz.x,0,xz.y};s.yaw=yaw;
    const auto hit=ground.probe_down({xz.x,ground.height(xz.x,xz.y)+5.f,xz.y},10.f);
    s.position.y=hit.hit?hit.point.y:ground.height(xz.x,xz.y);
    return s;
}
// Orient around the fifth wheel while keeping the rear bogie on its support
// surface. Heading follows the old axle, so forward turns cut in and reverse
// steering grows articulation naturally instead of copying the tractor yaw.
inline TrailerState follow_trailer(const TrailerState& old,glm::vec3 hitch,
                                  const TerrainCollider& ground) {
    auto s=old;
    const auto axle=trailer_point(old,{0,.5f,kTrailerAxleZ});
    const glm::vec2 back{axle.x-hitch.x,axle.z-hitch.z};
    if (glm::length(back)>.01f) s.yaw=std::atan2(back.x,back.y);
    const auto yaw_rotation=glm::angleAxis(s.yaw,glm::vec3{0,1,0});
    const auto rear=hitch+yaw_rotation*glm::vec3{0,0,kTrailerAxleZ-kTrailerKingpin.z};
    const auto support=ground.probe_down({rear.x,hitch.y+2.f,rear.z},6.f);
    const float floor=support.hit?support.point.y:ground.height(rear.x,rear.z);
    s.pitch=std::clamp(std::atan2(hitch.y-floor-kTrailerKingpin.y,
                                 kTrailerAxleZ-kTrailerKingpin.z),-.3f,.3f);
    s.position=hitch-trailer_rotation(s)*kTrailerKingpin;
    const auto new_axle=trailer_point(s,{0,.5f,kTrailerAxleZ});
    s.wheel_spin+=glm::dot(new_axle-axle,yaw_rotation*glm::vec3{0,0,-1})/kTrailerWheelRadius;
    return s;
}
struct TrailerBox { glm::vec3 centre,half; };
inline constexpr TrailerBox kTrailerBoxes[]={
    {{0,2.60f,0},{1.25f,1.20f,5.f}}, // box clears the tractor deck
    {{0,.67f,3.75f},{1.24f,.65f,1.30f}},
    {{0,1.15f,0},{.55f,.18f,4.8f}}
};
// OBB SAT in XZ plus vertical overlap. Checks whole faces, including thin posts
// between wheels; support slabs under the trailer are not treated as walls.
inline bool trailer_box_overlap(const TrailerState& s,TrailerBox part,const StaticBox& box) {
    const auto centre=trailer_point(s,part.centre);
    const float vertical=part.half.y*std::cos(s.pitch)+part.half.z*std::fabs(std::sin(s.pitch));
    if (centre.y+vertical<=box.bounds.min.y+.02f || centre.y-vertical>=box.bounds.max.y-.02f) return false;
    const auto bounds=box.collision_bounds();
    const auto bcentre=box.oriented?box.centre+box.world_direction(bounds.center()):bounds.center();
    const auto bh=bounds.extents();
    const glm::vec2 ax{std::cos(s.yaw),-std::sin(s.yaw)},az{std::sin(s.yaw),std::cos(s.yaw)};
    const glm::vec2 bx=box.oriented?box.axis_x:glm::vec2{1,0};
    const glm::vec2 bz=box.oriented?box.axis_z:glm::vec2{0,1};
    const float along=part.half.z*std::cos(s.pitch)+part.half.y*std::fabs(std::sin(s.pitch));
    const glm::vec2 delta{centre.x-bcentre.x,centre.z-bcentre.z};
    for (const auto axis:{ax,az,bx,bz}) {
        const float a=part.half.x*std::fabs(glm::dot(ax,axis))+along*std::fabs(glm::dot(az,axis));
        const float b=bh.x*std::fabs(glm::dot(bx,axis))+bh.z*std::fabs(glm::dot(bz,axis));
        if (std::fabs(glm::dot(delta,axis))>=a+b-.025f) return false;
    }
    return true;
}
inline bool trailer_clear(const TrailerState& from,const TrailerState& to,const TerrainCollider& ground) {
    const float sweep=glm::distance(from.position,to.position)+5.f*std::fabs(trailer_angle(to.yaw-from.yaw));
    const int steps=std::clamp(static_cast<int>(std::ceil(sweep/.15f)),1,256);
    for(int i=1;i<=steps;++i) {
        const float t=float(i)/float(steps);auto s=to;
        s.position=glm::mix(from.position,to.position,t);
        s.yaw=from.yaw+trailer_angle(to.yaw-from.yaw)*t;
        s.pitch=glm::mix(from.pitch,to.pitch,t);
        for(const auto& box:ground.static_boxes()) if(box.enabled)
            for(const auto part:kTrailerBoxes) if(trailer_box_overlap(s,part,box)) return false;
    }
    return true;
}
// Full 3D separating-axis check: a yaw cap alone cannot keep the upper front
// corner out of the cab while the trailer pitches through a sharp crest.
inline bool trailer_cab_clear(const TrailerState& trailer,const VehicleState& car,const VehicleTuning& tuning) {
    const glm::mat3 a=glm::mat3_cast(trailer_rotation(trailer)),b=glm::mat3_cast(car.orientation);
    const glm::vec3 ah{1.255f,1.205f,5.03f},bh{1.10f,1.075f,.675f};
    const auto ac=trailer_point(trailer,{0,2.60f,0});
    const auto bc=car.position+car.orientation*glm::vec3{0,
        2.175f-.50f-static_suspension_length(tuning)-tuning.com_height_above_mount,-.725f};
    const auto delta=ac-bc;
    const auto separates=[&](glm::vec3 axis) {
        const float length=glm::length(axis);if(length<.00001f)return false;axis/=length;
        float ra=0,rb=0;for(int j=0;j<3;++j){ra+=ah[j]*std::fabs(glm::dot(a[j],axis));rb+=bh[j]*std::fabs(glm::dot(b[j],axis));}
        return std::fabs(glm::dot(delta,axis))>ra+rb+.02f;
    };
    for(int i=0;i<3;++i)if(separates(a[i]) || separates(b[i]))return true;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)if(separates(glm::cross(a[i],b[j])))return true;
    return false;
}
// Host disables this rig's collision slots during this query, then publishes
// both poses together. Rejected motion leaves the hitch intact and stops both.
inline bool step_tractor_trailer(TrailerState& trailer,VehicleState& car,
                                 const VehicleState& previous,const VehicleTuning& tuning,
                                 const TerrainCollider& ground) {
    if(!trailer.attached) return true;
    auto next=follow_trailer(trailer,tractor_hitch(car,tuning),ground);
    if(std::fabs(trailer_angle(tractor_yaw(car)-next.yaw))>kTrailerMaxAngle ||
       !trailer_cab_clear(next,car,tuning) || !trailer_clear(trailer,next,ground)) {
        car.position=previous.position;car.orientation=previous.orientation;
        car.velocity={};car.angular_velocity={};
        for(auto& w:car.wheels) w.angular_velocity=0;
        trailer.blocked=true;return false;
    }
    next.blocked=false;trailer=next;return true;
}
} // namespace apricot
