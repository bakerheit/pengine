#pragma once
#include <iterator>
#include "city/start_area.h"

namespace apricot::city {
// Vacant block south of Causeway Court, facing Halloway Street. Grid (-140,-31)
// leaves the wash/billboard block to the east and every existing site intact.
inline constexpr StartSite kPawnShopSite{
    "Second Chance Pawn", {-65.992683f,-85.464164f}, kGridCos,kGridSin,
    {0,0},56,33.8f};
inline constexpr BuildingOpening kPawnEntry[] = {
    {"pawn entrance",OpeningKind::Door,1.7f,2.4f,0,3.1f,
     StartFinish::Glass,0,0,StartFinish::TealDoor,false},
};
// Display windows are open glazed-frame spans, matching the laundromat's
// visible interiors. No opaque blue pane conceals the merchandise behind it.
inline constexpr BuildingWall kPawnWalls[] = {
    {"pawn west wall",{-11,-12.5f},{-11,2.5f},0,5.6f,.28f,StartFinish::Brick},
    {"pawn east wall",{11,2.5f},{11,-12.5f},0,5.6f,.28f,StartFinish::Brick},
    {"pawn back wall",{11,-12.5f},{-11,-12.5f},0,5.6f,.28f,StartFinish::Brick},
    {"pawn entrance wall",{-1.7f,2.5f},{1.7f,2.5f},0,5.6f,.28f,StartFinish::WarmWall,kPawnEntry,1},
    {"pawn west display sill",{-10.4f,2.5f},{-1.7f,2.5f},0,.7f,.28f,StartFinish::WarmWall},
    {"pawn east display sill",{1.7f,2.5f},{10.4f,2.5f},0,.7f,.28f,StartFinish::WarmWall},
    {"pawn west front pier",{-11,2.5f},{-10.4f,2.5f},0,5.6f,.28f,StartFinish::TealDoor},
    {"pawn east front pier",{10.4f,2.5f},{11,2.5f},0,5.6f,.28f,StartFinish::TealDoor},
    {"pawn display lintel",{-11,2.5f},{11,2.5f},3.2f,2.4f,.28f,StartFinish::WarmWall},
};
inline constexpr BuildingRoof kPawnRoofs[] = {
    {"pawn parapet roof",{0,-5},5.6f,22,15,0,.22f,.35f,.38f,RoofStyle::Flat,
     RidgeAxis::AlongX,StartFinish::DarkRoof,StartFinish::WarmWall},
};
inline constexpr BuildingPiece kPawnFixtures[] = {
    {"pawn lot",{0,0},0,56,.1f,33.8f,StartFinish::Asphalt},
    {"pawn entrance walk",{0,9.6f},.10f,3.2f,.10f,14.6f,StartFinish::Concrete,true},
    {"pawn interior floor",{0,-5},.10f,21.7f,.10f,14.7f,StartFinish::Concrete,true},
    {"pawn interior threshold",{0,2.5f},.10f,2.3f,.10f,.65f,StartFinish::Concrete,true},
    {"pawn interior ceiling",{0,-5},5.10f,21.6f,.12f,14.6f,StartFinish::WarmWall},
    {"pawn sign backing",{0,2.98f},3.27f,7.1f,2.27f,.18f,StartFinish::TealDoor},
    {"pawn shop sign face",{0,3.083f},3.35f,6.3f,2.1f,.02f,StartFinish::White},
    {"pawn entry canopy",{0,3.35f},3.12f,3.8f,.12f,1.4f,StartFinish::TealDoor},
    {"pawn held open entry leaf",{-1.07f,1.83f},.2f,1.15f,2.84f,.06f,StartFinish::Glass,false,0,82},
    {"pawn held open entry leaf",{1.07f,1.83f},.2f,1.15f,2.84f,.06f,StartFinish::Glass,false,0,-82},
    {"pawn sales counter",{0,-7.8f},.2f,10,1.f,1.2f,StartFinish::TealDoor,true},
    {"pawn counter top",{0,-7.8f},1.2f,10.15f,.09f,1.35f,StartFinish::Steel},
    {"pawn register base",{-3,-7.8f},1.29f,.8f,.18f,.6f,StartFinish::DarkRoof},
    {"pawn register screen",{-3,-7.9f},1.47f,.58f,.4f,.12f,StartFinish::Glass},
    {"pawn valuation mat",{1.8f,-7.65f},1.292f,2.1f,.025f,.8f,StartFinish::RedTrim},
    {"pawn back office safe",{8.7f,-11.1f},.2f,2.2f,2.5f,1.5f,StartFinish::Steel,true},
    {"pawn safe door seam",{8.7f,-10.332f},.45f,1.88f,2.05f,.025f,StartFinish::DarkRoof},
    {"pawn safe door face",{8.7f,-10.31f},.5f,1.78f,1.95f,.025f,StartFinish::Steel},
    {"pawn safe handle",{9.2f,-10.27f},1.05f,.07f,.55f,.08f,StartFinish::DarkRoof},
    {"pawn bench seat",{-6,4.3f},.45f,3.4f,.18f,.65f,StartFinish::TealDoor,true},
    {"pawn bench back",{-6,4.63f},.65f,3.4f,.75f,.12f,StartFinish::TealDoor,true},
    {"pawn bench leg",{-7.25f,4.3f},.1f,.15f,.35f,.55f,StartFinish::Steel},
    {"pawn bench leg",{-4.75f,4.3f},.1f,.15f,.35f,.55f,StartFinish::Steel},
    {"pawn litter bin",{5.3f,4.5f},.1f,.75f,1.05f,.75f,StartFinish::Steel,true},
};
inline constexpr BuildingPlan kPawnShopPlan{
    "Second Chance Pawn",kPawnWalls,std::size(kPawnWalls),kPawnRoofs,std::size(kPawnRoofs),
    kPawnFixtures,std::size(kPawnFixtures)};

inline std::vector<BuildingPiece> bake_pawn_shop() {
    auto parts=bake_building(kPawnShopPlan);
    for(float x:{-6.1f,6.1f}) {
        parts.push_back({"pawn display mullion",{x,2.5f},.7f,.07f,2.5f,.12f,StartFinish::Steel,true});
        parts.push_back({"pawn window display plinth",{x,1.35f},.2f,7.6f,.65f,1.3f,StartFinish::WarmWall,true});
        parts.push_back({"pawn island display base",{x,-3.5f},.2f,2.2f,.85f,3.8f,StartFinish::TealDoor,true});
        parts.push_back({"pawn island display top",{x,-3.5f},1.05f,2.3f,.06f,3.9f,StartFinish::Steel});
        for(float z:{-2.5f,-4.5f}) {
            parts.push_back({"pawn used radio body",{x,z},1.11f,.66f,.36f,.30f,StartFinish::DarkRoof});
            parts.push_back({"pawn radio front face",{x,z+.158f},1.11f,.66f,.36f,.012f,StartFinish::White});
            parts.push_back({"pawn radio carry handle",{x,z},1.56f,.42f,.03f,.05f,StartFinish::Steel});
            for(float side:{-.20f,.20f})
                parts.push_back({"pawn radio handle support",{x+side,z},1.47f,.03f,.12f,.05f,StartFinish::Steel});
        }
    }
    // Recognizable secondhand stock: CRTs, speakers, guitar silhouettes and
    // tool cases. Props stand on display furniture rather than on walking paths.
    for(float x:{-8.3f,-5.8f,4.5f,7.f}) {
        parts.push_back({"pawn used television cabinet",{x,1.4f},.85f,.92f,.69f,.55f,StartFinish::DarkRoof});
        parts.push_back({"pawn television front face",{x,1.689f},.8575f,.90f,.675f,.018f,StartFinish::White});
        parts.push_back({"pawn television rear housing",{x,1.20f},.94f,.79f,.53f,.26f,StartFinish::DarkRoof});
        for(float side:{-.34f,.34f})
            parts.push_back({"pawn television foot",{x+side,1.4f},.85f,.10f,.04f,.44f,StartFinish::Steel});
    }
    for(float x:{-7.8f,-5.9f,-4.f}) {
        parts.push_back({"pawn guitar body",{x,-11.85f},1.35f,.39f,.52f,.10f,StartFinish::White});
        parts.push_back({"pawn guitar neck",{x,-11.831f},1.83f,.057f,.43f,.055f,StartFinish::DarkRoof});
        parts.push_back({"pawn guitar headstock",{x,-11.85f},2.25f,.083f,.16f,.045f,StartFinish::WarmWall});
        for(int fret=0;fret<16;++fret)
            parts.push_back({"pawn guitar fret",{x,-11.800f},1.86f+static_cast<float>(fret)*.024f,
                .058f,.0018f,.002f,StartFinish::Steel});
        for(int string=0;string<6;++string)
            parts.push_back({"pawn guitar string",{x-.018f+static_cast<float>(string)*.0072f,-11.798f},
                1.83f,.0011f,.51f,.001f,StartFinish::Steel});
        for(float side:{-.057f,.057f}) for(int peg=0;peg<3;++peg)
            parts.push_back({"pawn guitar tuning peg",{x+side,-11.85f},2.27f+static_cast<float>(peg)*.05f,
                .032f,.016f,.028f,StartFinish::Steel});
        parts.push_back({"pawn guitar wall hanger",{x,-12.15f},2.18f,.12f,.08f,.54f,StartFinish::DarkRoof});
    }
    for(float x:{-10.f,10.f}) {
        parts.push_back({"pawn stock shelf back",{x+(x<0?-.55f:.55f),-7.4f},.2f,.12f,2.5f,4.f,StartFinish::WarmWall,true});
        for(float z:{-9.4f,-5.4f})
            parts.push_back({"pawn stock shelf end",{x,z},.2f,1.3f,2.5f,.12f,StartFinish::WarmWall,true});
        for(float y:{.45f,1.2f,1.95f})
            parts.push_back({"pawn stock shelf tray",{x,-7.4f},y,1.3f,.1f,4.f,StartFinish::WarmWall,true});
        for(float y:{.55f,1.3f,2.05f}) for(float z:{-8.6f,-7.3f,-6.f}) {
            parts.push_back({"pawn tool case",{x,z},y,.8f,.45f,1.05f,StartFinish::DarkRoof});
            parts.push_back({"pawn tool case handle",{x,z},y+.45f,.34f,.08f,.12f,StartFinish::Steel});
        }
    }
    for(float x:{-6.f,0.f,6.f}) for(float z:{-.8f,-8.8f}) {
        parts.push_back({"pawn ceiling light housing",{x,z},5.0f,1.7f,.12f,.45f,StartFinish::Steel});
        parts.push_back({"pawn ceiling light lens",{x,z},4.99f,1.55f,.025f,.32f,StartFinish::White});
    }
    for(float x:{-22.f,-18.f,-14.f,14.f,18.f,22.f}) {
        parts.push_back({"pawn parking stripe",{x,11.5f},.104f,.10f,.008f,5.8f,StartFinish::White});
        // Cars approach from the shop-side aisle and park facing the road.
        parts.push_back({"pawn parking stop",{x+1.8f,14.1f},.10f,2.5f,.15f,.22f,StartFinish::Concrete,true});
    }
    return parts;
}
} // namespace apricot::city
