#pragma once

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace apricot {
struct BreakawayContact {
    bool hit = false;
    glm::vec3 normal{0.0f};
    float penetration = 0.0f;
};

// A thin pole against the car's actual yaw footprint, not its centre-radius
// wall guard. That guard lets the bonnet swallow a pole before it reacts.
inline BreakawayContact breakaway_contact(glm::vec3 car, glm::quat orientation,
                                          glm::vec2 half, glm::vec3 pole,
                                          float pole_radius) {
    const auto f3=orientation*glm::vec3{0,0,-1};
    glm::vec2 forward{f3.x,f3.z};
    const float length=glm::length(forward);
    if(length<1e-5f)return {};
    forward/=length;
    const glm::vec2 right{-forward.y,forward.x};
    const glm::vec2 delta{pole.x-car.x,pole.z-car.z};
    const glm::vec2 local{glm::dot(delta,right),glm::dot(delta,forward)};
    const glm::vec2 nearest=glm::clamp(local,-half,half);
    const glm::vec2 gap=local-nearest;
    const float distance=glm::length(gap);
    if(distance>=pole_radius)return {};
    glm::vec2 normal;
    float penetration=0;
    if(distance>1e-5f) {
        normal=-(right*gap.x+forward*gap.y)/distance;
        penetration=pole_radius-distance;
    } else {
        const glm::vec2 to_face=half-glm::abs(local);
        if(to_face.x<to_face.y) {
            normal=right*(local.x>=0 ? -1.f : 1.f);
            penetration=to_face.x+pole_radius;
        } else {
            normal=forward*(local.y>=0 ? -1.f : 1.f);
            penetration=to_face.y+pole_radius;
        }
    }
    return {true,{normal.x,0,normal.y},penetration};
}
} // namespace apricot
