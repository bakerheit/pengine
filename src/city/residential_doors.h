#pragma once
#include "city/residential_neighborhood.h"
#include "game/house_door.h"

namespace apricot::city {
struct ResidentialDoor {
    const char* name;
    glm::vec2 local_hinge;
    float local_yaw;
    HouseDoor physics;
    StartFinish finish;
};
inline std::vector<ResidentialDoor> residential_doors(GroundSampler ground) {
    const auto& h=kResidentialHouses[kResidentialTargetHouse];
    const float floor=h.site.ground_m+residential_floor_top(kResidentialTargetHouse,ground)+.025f;
    std::vector<ResidentialDoor> out;
    const auto add=[&](const char* name,glm::vec2 hinge,float yaw,float width,float height,StartFinish finish) {
        const auto p=residential_world(h.site,hinge);
        HouseDoor d;d.hinge={p.x,floor,p.y};d.closed_yaw=std::atan2(h.site.sin_yaw,h.site.cos_yaw)+yaw;
        d.width=width;d.height=height;d.pivot_inset=.09f;
        out.push_back({name,hinge,yaw,d,finish});
    };
    // A double-action pivot sits 9cm into the leaf, leaving a 6mm reveal at
    // each jamb. The back corner sweeps sqrt(.09^2 + .0325^2) < .096m,
    // so the full-thickness leaf clears the existing frame in both directions.
    add("house push front door",{-.619f,6},0,1.418f,2.209f,StartFinish::TealDoor);
    add("house push garden door",{-7,-.644f},-1.570796327f,1.468f,2.209f,StartFinish::TealDoor);
    add("house push study door",{-3.594f,-1},0,1.368f,2.159f,StartFinish::White);
    add("house push bath door",{-.494f,-1},0,1.168f,2.159f,StartFinish::White);
    add("house push bedroom door",{2.406f,-1},0,1.368f,2.159f,StartFinish::White);
    return out;
}
// Leaf, recessed-looking raised panels, knob plates, knobs, and hinge straps
// move as a single rigid assembly. Only the full slab owns collision.
inline std::vector<StartPart> residential_door_parts(const ResidentialDoor& d,float angle) {
    const auto& h=kResidentialHouses[kResidentialTargetHouse];
    const float yaw=d.local_yaw+angle;const glm::vec2 t{std::cos(yaw),-std::sin(yaw)},n{-t.y,t.x};
    std::vector<StartPart> out;
    const auto add=[&](const char* name,float along,float across,float bottom,float w,float height,float depth,StartFinish finish,bool solid=false) {
        const auto p=d.local_hinge+t*(along-d.physics.pivot_inset)+n*across;
        out.push_back({name,{p.x,p.y},d.physics.hinge.y-h.site.ground_m+bottom,w,height,depth,finish,solid,0,glm::degrees(yaw)});
    };
    add(d.name,d.physics.width*.5f,0,0,d.physics.width,d.physics.height,d.physics.thickness,d.finish,true);
    for(float side:{-1.f,1.f}) {
        for(float y:{.18f,1.23f})add("house door raised panel",d.physics.width*.5f,side*.038f,y,d.physics.width-.36f,.83f,.025f,d.finish);
        add("house door knob plate",d.physics.width-.16f,side*.055f,.91f,.085f,.24f,.025f,StartFinish::Steel);
        add("house door brass knob",d.physics.width-.16f,side*.089f,.99f,.075f,.075f,.07f,StartFinish::Yellow);
    }
    for(float y:{.22f,1.03f,1.91f})add("house door hinge",d.physics.pivot_inset,0,y,.075f,.13f,.09f,StartFinish::Steel);
    return out;
}
} // namespace apricot::city
