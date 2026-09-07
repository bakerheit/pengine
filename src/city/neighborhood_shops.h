#pragma once
#include <iterator>
#include "city/start_area.h"

namespace apricot::city {
// The vacant block across the street from the motel, west of Quickbite.
// Two small businesses share a forecourt; neither changes the road network.
inline constexpr StartSite kAutoRepairSite{
    "Rook's Auto Repair", {-94.8665f,36.1841f}, kGridCos,kGridSin,
    {0,0},34,38};
inline constexpr StartSite kLaundromatSite{
    "Spin Cycle Laundromat", {-59.0638f,39.947f}, kGridCos,kGridSin,
    {0,0},26,38};
inline constexpr BuildingOpening kRepairOpenings[] = {
    {"repair bay one",OpeningKind::Door,7,6.4f,0,4.2f,StartFinish::Steel,0,0,StartFinish::RedTrim,false},
    {"repair bay two",OpeningKind::Door,15,6.4f,0,4.2f,StartFinish::Steel,0,0,StartFinish::RedTrim,false},
    {"repair office entry",OpeningKind::Door,27,2.1f,0,2.8f,StartFinish::TealDoor,0,0,StartFinish::RedTrim,false},
};
inline constexpr BuildingWall kRepairWalls[] = {
    {"repair front",{-16,4},{16,4},0,5.8f,.3f,StartFinish::WarmWall,kRepairOpenings,3},
    {"repair west",{-16,-15},{-16,4},0,5.8f,.3f,StartFinish::Brick},
    {"repair east",{16,4},{16,-15},0,5.8f,.3f,StartFinish::Brick},
    {"repair back",{16,-15},{-16,-15},0,5.8f,.3f,StartFinish::Brick},
    {"repair office divider",{5,-15},{5,4},0,4.8f,.2f,StartFinish::WarmWall},
};
inline constexpr BuildingRoof kRepairRoofs[] = {
    {"Rook's garage roof",{0,-5.5f},5.8f,32,19,0,.25f,.35f,.45f,RoofStyle::Flat,RidgeAxis::AlongX,StartFinish::DarkRoof,StartFinish::WarmWall},
};
inline constexpr BuildingPiece kRepairFixtures[] = {
    {"repair lot",{0,0},0,34,.10f,38,StartFinish::Asphalt},
    {"repair interior floor",{0,-5.5f},.10f,31.5f,.10f,18.5f,StartFinish::Concrete},
    {"repair sign backing",{-3.5f,4.3f},4.35f,24,1.22f,.20f,StartFinish::RedTrim},
    {"repair shop sign face",{-3.5f,4.42f},4.39f,23.5f,1.12f,.02f,StartFinish::White},
    {"repair office counter",{10,-1},.2f,5,1.05f,1.2f,StartFinish::TealDoor,true},
    {"repair tool chest",{-12,-13},.2f,4,1.25f,.8f,StartFinish::RedTrim,true},
    {"repair workbench",{-3,-13},.2f,8,.95f,1.0f,StartFinish::Steel,true},
    {"repair rooftop hvac",{10,-9},6.1f,3.2f,1.2f,2.2f,StartFinish::Steel,true},
    {"repair rear fence",{0,-18.3f},.1f,32,1.8f,.12f,StartFinish::Steel,true},
    {"repair side fence",{-16.8f,-10},.1f,.12f,1.8f,16,StartFinish::Steel,true},
};
inline constexpr BuildingPlan kAutoRepairPlan{
    "Rook's Auto Repair",kRepairWalls,std::size(kRepairWalls),
    kRepairRoofs,std::size(kRepairRoofs),kRepairFixtures,std::size(kRepairFixtures)};

// Window spans are genuinely open, with sill, mullions and lintel. That keeps
// the authored washers visible without pretending an opaque glass box is clear.
inline constexpr BuildingWall kLaundryWalls[] = {
    {"laundry west",{-11,-14},{-11,3},0,4.6f,.25f,StartFinish::WarmWall},
    {"laundry east",{11,3},{11,-14},0,4.6f,.25f,StartFinish::WarmWall},
    {"laundry back",{11,-14},{-11,-14},0,4.6f,.25f,StartFinish::Brick},
    {"laundry front left pier",{-11,3},{-9.5f,3},0,4.6f,.25f,StartFinish::WarmWall},
    {"laundry entry pier",{-7,3},{-6.5f,3},0,4.6f,.25f,StartFinish::TealDoor},
    {"laundry window sill",{-6.5f,3},{10.5f,3},0,.72f,.25f,StartFinish::WarmWall},
    {"laundry front right pier",{10.5f,3},{11,3},0,4.6f,.25f,StartFinish::WarmWall},
    {"laundry front lintel",{-11,3},{11,3},3.4f,1.2f,.25f,StartFinish::TealDoor},
};
inline constexpr BuildingRoof kLaundryRoofs[] = {
    {"Spin Cycle roof",{0,-5.5f},4.6f,22,17,0,.23f,.3f,.3f,RoofStyle::Flat,RidgeAxis::AlongX,StartFinish::DarkRoof,StartFinish::WarmWall},
};
inline constexpr BuildingPiece kLaundryFixtures[] = {
    {"laundry lot",{0,0},0,26,.10f,38,StartFinish::Asphalt},
    {"laundry interior floor",{0,-5.5f},.10f,21.5f,.10f,16.5f,StartFinish::Concrete},
    {"laundry shop sign face",{0,3.16f},3.52f,20,1,.02f,StartFinish::White},
    {"laundry window mullion",{-2,3},.72f,.08f,2.68f,.1f,StartFinish::Steel,true},
    {"laundry window mullion",{3,3},.72f,.08f,2.68f,.1f,StartFinish::Steel,true},
    {"laundry window mullion",{7,3},.72f,.08f,2.68f,.1f,StartFinish::Steel,true},
    {"laundry folding table",{2,-6},.2f,8,.9f,1.6f,StartFinish::White,true},
    {"laundry waiting bench",{7,1},.2f,5,.5f,.8f,StartFinish::TealDoor,true},
    {"laundry vending cabinet",{-9,-10},.2f,1.4f,2.1f,1.1f,StartFinish::Yellow,true},
    {"laundry rooftop vent",{8,-10},4.95f,.6f,.8f,.6f,StartFinish::Steel},
};
inline constexpr BuildingPlan kLaundromatPlan{
    "Spin Cycle Laundromat",kLaundryWalls,std::size(kLaundryWalls),
    kLaundryRoofs,std::size(kLaundryRoofs),kLaundryFixtures,std::size(kLaundryFixtures)};

inline std::vector<BuildingPiece> bake_auto_repair() {
    auto parts=bake_building(kAutoRepairPlan);
    for(float bay : {-9.f,-1.f}) {
        for(float side : {-2.f,2.f}) {
            parts.push_back({"repair lift column",{bay+side,-6},.2f,.35f,3.6f,.5f,StartFinish::Yellow,true});
            parts.push_back({"repair lift foot",{bay+side,-6},.2f,.8f,.10f,2.5f,StartFinish::Steel});
        }
        parts.push_back({"repair raised shutter",{bay,3.7f},4.0f,6.2f,.20f,.35f,StartFinish::Steel});
        for(float z : {5.2f,13.f})
            parts.push_back({"repair bay stripe",{bay,z},.105f,5.8f,.015f,.10f,StartFinish::Yellow});
    }
    for(int stack=0;stack<3;++stack) for(int tire=0;tire<3;++tire)
        parts.push_back({"repair tire stack",{12.f+float(stack)*1.3f,-17},.12f+float(tire)*.32f,1.05f,.30f,1.05f,StartFinish::DarkRoof,true});
    return parts;
}
inline std::vector<BuildingPiece> bake_laundromat() {
    auto parts=bake_building(kLaundromatPlan);
    for(int i=0;i<8;++i) {
        const float x=-5.f+float(i)*2.f;
        parts.push_back({"laundry washer body",{x,-11.8f},.2f,1.55f,1.35f,1.4f,StartFinish::White,true});
        parts.push_back({"laundry washer drum rim",{x,-11.04f},.87f,.9f,.06f,.9f,StartFinish::Steel,false,90});
        parts.push_back({"laundry washer drum glass",{x,-11.f},.865f,.7f,.07f,.7f,StartFinish::Glass,false,90});
        parts.push_back({"laundry washer controls",{x,-11.06f},1.26f,1.25f,.18f,.04f,StartFinish::TealDoor});
    }
    for(float x : {-8.f,-2.f,4.f,10.f}) {
        parts.push_back({"laundry parking stripe",{x,12},.105f,.1f,.015f,6,StartFinish::White});
        parts.push_back({"laundry parking stop",{x+2.5f,8},.1f,3,.13f,.22f,StartFinish::Concrete,true});
    }
    return parts;
}
} // namespace apricot::city
