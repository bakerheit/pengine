#pragma once

#include <array>
#include "city/building_creator.h"

namespace apricot::city {

// One shared ATM cabinet. Both bank units and the convenience-store unit use
// these exact dimensions and the same square control-panel artwork.
constexpr std::array<BuildingPiece,5> make_atm_component(
    Vec2 centre, float floor, bool face_positive_z,
    const std::array<const char*,5>& names) {
    const float direction = face_positive_z ? 1.0f : -1.0f;
    const float yaw = face_positive_z ? 0.0f : 180.0f;
    return {{
        {names[0],centre,floor,.86f,1.60f,.55f,BuildingFinish::DarkRoof,true,0,yaw},
        {names[1],{centre.x,centre.z+direction*.285f},floor+.74f,.78f,.78f,.035f,
         BuildingFinish::Steel,false,0,yaw},
        {names[2],{centre.x,centre.z+direction*.305f},floor+.78f,.70f,.70f,.012f,
         BuildingFinish::White,false,0,yaw},
        {names[3],{centre.x,centre.z+direction*.285f},floor+.08f,.70f,.59f,.025f,
         BuildingFinish::Steel,false,0,yaw},
        {names[4],{centre.x+direction*.25f,centre.z+direction*.310f},floor+.47f,
         .035f,.10f,.025f,BuildingFinish::DarkRoof,false,0,yaw},
    }};
}
}  // namespace apricot::city
