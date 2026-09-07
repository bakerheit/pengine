#pragma once
#include <algorithm>
#include <cmath>
#include "game/character.h"

namespace apricot {
// Hinges use the same clockwise X/Z yaw as render transforms and collision.
struct HouseDoor {
    glm::vec3 hinge{}; // At the bottom of the leaf, world coordinates.
    float closed_yaw=0, width=1.5f, height=2.28f, thickness=.065f;
    float max_angle=1.65806f; // 95 degrees either way: push away from either side.
    float pivot_inset=0; // Distance from the hinge-side edge to the pivot axis.
};
struct HouseDoorState { float angle=0, angular_velocity=0, return_delay=0; };
inline glm::vec2 house_door_tangent(const HouseDoor& d,float angle) {
    const float yaw=d.closed_yaw+angle;return {std::cos(yaw),-std::sin(yaw)};
}
inline glm::vec3 house_door_centre(const HouseDoor& d,float angle) {
    const auto t=house_door_tangent(d,angle);
    const float offset=d.width*.5f-d.pivot_inset;
    return d.hinge+glm::vec3{t.x*offset,d.height*.5f,t.y*offset};
}
inline float house_door_clearance(const HouseDoor& d,float angle,glm::vec3 feet,float radius) {
    const auto t=house_door_tangent(d,angle);const glm::vec2 n{-t.y,t.x};
    const glm::vec2 delta{feet.x-d.hinge.x,feet.z-d.hinge.z};
    const float along=glm::dot(delta,t)+d.pivot_inset,across=glm::dot(delta,n);
    const glm::vec2 outside{std::max({-along,along-d.width,0.f}),
        std::max(std::abs(across)-d.thickness*.5f,0.f)};
    return glm::length(outside)-radius;
}
inline glm::vec3 house_door_walk_velocity(const PlayerCharacterState& actor,
        const CharacterTuning& tuning,const InputFrame& input) {
    glm::vec2 intent{input.steer,input.throttle-input.brake};
    const float magnitude=glm::length(intent);if(magnitude<=.0001f)return {};
    const float yaw=actor.view_yaw+input.look_dx;
    const auto direction=(character_forward(yaw)*intent.y+
        glm::vec3{std::cos(yaw),0,std::sin(yaw)}*intent.x)/magnitude;
    const float speed=is_held(input,kBtnShiftUp)&&magnitude>.25f?
        tuning.sprint_speed_mps:tuning.walk_speed_mps;
    return direction*speed*std::min(magnitude,1.f);
}
// This runs BEFORE character collision. It does not move the actor or disable
// the solid panel. Walking supplies contact torque, never a proximity trigger.
inline HouseDoorState step_house_door(const HouseDoor& door,HouseDoorState state,
        const PlayerCharacterState* actor,const CharacterTuning& tuning,
        glm::vec3 attempted_velocity,float dt) {
    if(dt<=0)return state;
    dt=std::min(dt,1.f/30.f);
    const bool body=actor && actor->position.y+tuning.height_m>door.hinge.y &&
        actor->position.y+tuning.max_step_m<door.hinge.y+door.height;
    bool pushing=false;float drive=0;
    if(body && glm::length(glm::vec2{attempted_velocity.x,attempted_velocity.z})>.001f) {
        const auto candidate=actor->position+attempted_velocity*dt;
        const float clearance=house_door_clearance(door,state.angle,actor->position,tuning.radius_m);
        const float attempted=house_door_clearance(door,state.angle,candidate,tuning.radius_m);
        if(attempted<.002f && attempted<clearance-.000001f &&
           clearance<glm::length(attempted_velocity)*dt+.003f) {
            // Differentiate actual capsule/rectangle clearance, not signed
            // distance to an infinite door plane. At free ends/corners the
            // nearest contact normal is NOT the broad face normal; ignoring
            // that locks an actor against an almost edge-on returning leaf.
            constexpr float probe=.01f;
            const float plus=house_door_clearance(door,state.angle+probe,candidate,tuning.radius_m);
            const float minus=house_door_clearance(door,state.angle-probe,candidate,tuning.radius_m);
            const float gradient=(plus-minus)/(2.f*probe);
            const float direction=std::abs(gradient)>.0001f?(gradient>0?1.f:-1.f):
                (state.angle>0?-1.f:1.f);
            const float approach=(clearance-attempted)/dt;
            drive=direction*std::min(4.5f,approach/std::max(std::abs(gradient),.15f)*1.4f);
            pushing=true;state.return_delay=1.5f;
        }
    }
    if(pushing)state.angular_velocity=drive;
    else {
        state.return_delay=std::max(0.f,state.return_delay-dt);
        const float spring=state.return_delay<=0 ? -state.angle*1.3f : 0.f;
        state.angular_velocity+=(spring-state.angular_velocity*5.f)*dt;
    }
    const float target=std::clamp(state.angle+state.angular_velocity*dt,-door.max_angle,door.max_angle);
    // Sweep in small angular increments. An occupied return/swing stops the
    // panel itself instead of shoving or teleporting a stationary character.
    const int steps=std::max(1,static_cast<int>(std::ceil(std::abs(target-state.angle)/.008f)));
    const float increment=(target-state.angle)/static_cast<float>(steps);
    for(int i=0;i<steps;++i) {
        const float next=state.angle+increment;
        if(body) {
            const float before=house_door_clearance(door,state.angle,actor->position,tuning.radius_m);
            const float after=house_door_clearance(door,next,actor->position,tuning.radius_m);
            if(after<.001f && after<before-.00001f) {
                state.angular_velocity=0;state.return_delay=1.5f;break;
            }
        }
        state.angle=next;
    }
    if(std::abs(state.angle)>=door.max_angle-.00001f)state.angular_velocity=0;
    if(!pushing && std::abs(state.angle)<.0005f && std::abs(state.angular_velocity)<.001f) {
        state.angle=0;state.angular_velocity=0;
    }
    return state;
}
} // namespace apricot
