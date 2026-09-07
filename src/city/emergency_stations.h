#pragma once
#include <vector>
#include "city/start_area.h"

namespace apricot::city {
constexpr Vec2 station_grid(float x,float z) {
    return {70+kGridCos*x+kGridSin*z,-40-kGridSin*x+kGridCos*z};
}
inline constexpr StartSite kFireStationSite{
    "Halloway Fire Station",station_grid(-230,33),kGridCos,kGridSin,{0,0},68,38};
inline constexpr StartSite kPoliceStationSite{
    "Halloway Police Station",station_grid(138,-33),kGridCos,kGridSin,{0,0},68,38};

inline std::vector<StartPart> bake_emergency_station(bool fire) {
    std::vector<StartPart> p;
    const auto add=[&](const char* n,float x,float z,float y,float w,float h,float d,
                       StartFinish f,bool solid=false) {
        p.push_back({n,{x,z},y,w,h,d,f,solid});
    };
    const auto accent=fire?StartFinish::RedTrim:StartFinish::TealDoor;
    add(fire?"fire station lot":"police station lot",0,0,0,68,.10f,38,StartFinish::Asphalt);
    add("station interior floor",0,-5,.1f,54,.1f,26,StartFinish::Concrete,true);
    add("station rear wall",0,-17.8f,.2f,54,7,.35f,StartFinish::Brick,true);
    for(float x:{-26.8f,26.8f})
        add("station side wall",x,-5,.2f,.35f,7,26,StartFinish::Brick,true);
    add("station roof",0,-5,7.2f,54.6f,.3f,26.6f,StartFinish::DarkRoof,true);
    add("station roof front coping",0,8.1f,7.2f,54.6f,.65f,.45f,StartFinish::WarmWall);
    add("station sign backing",0,8.02f,5.65f,53.5f,1.5f,.3f,accent);
    add(fire?"fire station sign face":"police station sign face",0,8.19f,5.78f,15,1.12f,.02f,StartFinish::White);
    if(fire) {
        // Three real open apparatus bays. Concrete approaches stay level with
        // the interior and no solid invisible door spans their openings.
        for(float x:{-26.8f,-18.f,-9.f,0.f})
            add("station engine bay pier",x,7.8f,.2f,.65f,5.45f,.5f,StartFinish::Brick,true);
        add("station apparatus apron",-13.4f,13.5f,.10f,27.5f,.10f,11,StartFinish::Concrete);
        for(float x:{-22.4f,-13.5f,-4.5f}) {
            add("station rolled bay door",x,7.55f,5.25f,7.8f,.4f,.55f,StartFinish::Steel);
            for(float side:{-1.f,1.f})
                add("station bay lane line",x+side*2.2f,-3,.204f,.10f,.008f,21,StartFinish::White);
            add("station hose rack",x,-16.7f,.25f,5.3f,1.8f,.65f,StartFinish::Steel,true);
            for(float offset:{-1.8f,-.9f,0.f,.9f,1.8f})
                add("station coiled hose",x+offset,-16.31f,.7f,.6f,1,.18f,StartFinish::WarmWall);
        }
        add("station crew divider",1,-5,.2f,.3f,5.4f,26,StartFinish::WarmWall,true);
        add("station crew front left",6,7.8f,.2f,10,5.4f,.35f,StartFinish::Brick,true);
        add("station crew front right",22,7.8f,.2f,10,5.4f,.35f,StartFinish::Brick,true);
        add("station crew entrance lintel",14,7.8f,3.2f,6,2.4f,.35f,StartFinish::WarmWall,true);
        add("station entrance walk",14,13.5f,.1f,4,.1f,11,StartFinish::Concrete);
        add("station crew table",14,-4,.2f,7,1,2.5f,StartFinish::WarmWall,true);
        for(float x:{9.f,19.f}) for(float z:{-6.f,-2.f})
            add("station crew chair",x,z,.2f,.7f,.85f,.7f,accent,true);
        for(float x:{5.f,7.f,9.f,11.f,13.f,15.f,17.f,19.f,21.f,23.f}) {
            add("station gear locker",x,-16,.2f,1.6f,2.1f,1,StartFinish::Steel,true);
            add("station locker handle",x+.5f,-15.47f,1,.06f,.4f,.06f,StartFinish::DarkRoof);
        }
    } else {
        // Public reception with an open central door; the rear wing is staff
        // space. Parking sits to the side of the pedestrian entrance.
        for(float x:{-15.f,15.f}) {
            add("station public front wall",x,7.8f,.2f,24,1,.35f,StartFinish::Brick,true);
            add("station lobby window",x,7.8f,1.2f,22.8f,2.8f,.08f,StartFinish::Glass,true);
            add("station public front lintel",x,7.8f,4,24,1.65f,.35f,StartFinish::WarmWall,true);
            for(float dx:{-8.f,-4.f,0.f,4.f,8.f})
                add("station lobby window mullion",x+dx,8,1.2f,.12f,2.8f,.12f,accent);
        }
        add("station entrance lintel",0,7.8f,3.3f,6,2.35f,.35f,StartFinish::WarmWall,true);
        add("station entrance walk",0,13.5f,.1f,4,.1f,11,StartFinish::Concrete);
        add("station reception counter",0,-1,.2f,12,1.1f,1.3f,accent,true);
        add("station reception top",0,-1,1.3f,12.2f,.09f,1.5f,StartFinish::Steel);
        for(float x:{-3.f,3.f}) {
            add("station desk monitor",x,-1,1.4f,.8f,.55f,.15f,StartFinish::DarkRoof);
            add("station waiting bench",x*5,3,.2f,5,.8f,.8f,accent,true);
        }
        add("station staff divider",-3,-5,.2f,48,5.4f,.25f,StartFinish::WarmWall,true);
        for(float x:{-18.f,-6.f,6.f,18.f})
            add("station office desk",x,-12,.2f,5,1.1f,2,StartFinish::WarmWall,true);
    }
    for(float x:{-30.f,30.f}) {
        add("station flagpole",x,6,.1f,.12f,9,.12f,StartFinish::Steel);
        add("station civic pennant",x+.9f,6,7.2f,1.8f,1.1f,.025f,accent);
        add("station entrance bollard",fire?13.f+x*.09f:x*.13f,9.5f,.1f,.22f,1,.22f,StartFinish::Steel,true);
    }
    // Visitor bays clear the apparatus apron and customer route.
    for(float x:fire?std::vector<float>{22.f,25.f,28.f,31.f}:
                         std::vector<float>{-28.f,-25.f,-22.f,-19.f,19.f,22.f,25.f,28.f}) {
        add("station parking stripe",x,14,.106f,.10f,.008f,5,StartFinish::White);
        add("station parking stop",x+1.35f,11.7f,.1f,2,.16f,.3f,StartFinish::Concrete,true);
    }
    return p;
}
} // namespace apricot::city
