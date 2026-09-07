#pragma once
#include <cstring>
#include <iterator>
#include "city/start_area.h"

namespace apricot::city {
// On Sixth Street, one block from The Bent Elbow and near Second Chance Pawn.
// Earlier parcels at (-48,-93) and (46,-93) were absorbed by the hospital
// garage and southeast service yard. Grid (-230,-93) is a vacant Sixth Street
// parcel, keeps the existing access direction, and clears the full hospital
// logistics footprint. Local +Z faces Sixth; the shell is centred at (0,-4).
inline constexpr StartSite kGunStoreSite{
    "Brassline Arms",{70.f+kGridCos*-230.f+kGridSin*-93.f,
                     -40.f-kGridSin*-230.f+kGridCos*-93.f},
    kGridCos,kGridSin,{0,0},32,34};
inline constexpr BuildingOpening kGunStoreEntry[]{
    {"gun store entrance",OpeningKind::Door,1.5f,2.4f,0,2.8f,
     StartFinish::Glass,0,0,StartFinish::Steel,false},
};
inline constexpr BuildingWall kGunStoreWalls[]{
    {"gun store west brick wall",{-8,-10},{-8,2},.2f,4,.28f,StartFinish::Brick},
    {"gun store east brick wall",{8,2},{8,-10},.2f,4,.28f,StartFinish::Brick},
    {"gun store rear brick wall",{8,-10},{-8,-10},.2f,4,.28f,StartFinish::Brick},
    {"gun store entrance wall",{-1.5f,2},{1.5f,2},.2f,4,.28f,StartFinish::WarmWall,kGunStoreEntry,1},
    {"gun store west front pier",{-8,2},{-6.9f,2},.2f,4,.28f,StartFinish::WarmWall},
    {"gun store east front pier",{6.9f,2},{8,2},.2f,4,.28f,StartFinish::WarmWall},
    {"gun store west inner pier",{-3.7f,2},{-1.5f,2},.2f,4,.28f,StartFinish::WarmWall},
    {"gun store east inner pier",{1.5f,2},{3.7f,2},.2f,4,.28f,StartFinish::WarmWall},
    {"gun store west display sill",{-6.9f,2},{-3.7f,2},.2f,.65f,.28f,StartFinish::WarmWall},
    {"gun store east display sill",{3.7f,2},{6.9f,2},.2f,.65f,.28f,StartFinish::WarmWall},
    {"gun store front lintel",{-8,2},{8,2},3.05f,1.15f,.28f,StartFinish::WarmWall},
};
inline constexpr BuildingRoof kGunStoreRoofs[]{
    {"gun store parapet roof",{0,-4},4.2f,16,12,0,.18f,.2f,.22f,
     RoofStyle::Flat,RidgeAxis::AlongX,StartFinish::DarkRoof,StartFinish::WarmWall},
};
inline constexpr BuildingPiece kGunStoreFixtures[]{
    {"gun store lot",{0,0},0,32,.1f,34,StartFinish::Asphalt},
    {"gun store entrance walk",{0,10.6f},.1f,2.4f,.1f,12.8f,StartFinish::Concrete,true},
    {"gun store storefront walk",{0,3.2f},.1f,16.3f,.1f,2.4f,StartFinish::Concrete,true},
    {"gun store interior floor",{0,-4},.1f,15.72f,.1f,11.72f,StartFinish::Concrete,true},
    {"gun store interior threshold",{0,2},.1f,2.4f,.1f,.7f,StartFinish::Concrete,true},
    {"gun store interior ceiling",{0,-4},4.1f,15.7f,.1f,11.7f,StartFinish::WarmWall},
    // Raised fascia preserves the generated sign's native 2121:741 aspect.
    {"gun store sign backing",{0,2.55f},3.02f,6.7f,2.30f,.15f,StartFinish::RedTrim},
    {"gun store shop sign face",{0,2.638f},3.05f,6.4f,6.4f*741.f/2121.f,.02f,StartFinish::White},
    {"gun store entrance canopy",{0,2.6f},2.99f,3.1f,.12f,1.55f,StartFinish::DarkRoof},
    // Angled frame leaves show the open state without an opaque glass wall.
    {"gun store held open leaf stile",{-1.1f,1.46f},.2f,.07f,2.72f,1.05f,StartFinish::Steel},
    {"gun store held open leaf stile",{1.1f,1.46f},.2f,.07f,2.72f,1.05f,StartFinish::Steel},
    {"gun store counter cabinet",{0,-6.4f},.2f,10,.78f,1.2f,StartFinish::RedTrim,true},
    {"gun store counter display deck",{0,-6.4f},.98f,10.1f,.07f,1.25f,StartFinish::WarmWall},
    {"gun store counter top rear",{0,-6.9f},1.20f,10.2f,.06f,.28f,StartFinish::DarkRoof},
    {"gun store counter top front",{0,-5.9f},1.20f,10.2f,.06f,.14f,StartFinish::Steel},
    {"gun store counter solid display volume",{0,-6.4f},.98f,10,.28f,1.2f,StartFinish::Glass,true},
    {"gun store register",{-3.8f,-6.45f},1.26f,.62f,.32f,.48f,StartFinish::Steel},
    {"gun store register screen",{-3.8f,-6.18f},1.40f,.40f,.20f,.03f,StartFinish::DarkRoof},
    {"gun store rear rack backboard",{0,-9.64f},1.2f,8,2.2f,.10f,StartFinish::WarmWall},
    {"gun store island display base",{-4,-2.7f},.2f,1.6f,.89f,2.6f,StartFinish::RedTrim,true},
    {"gun store island display top",{-4,-2.7f},1.09f,1.65f,.06f,2.65f,StartFinish::DarkRoof},
    {"gun store safe cabinet",{6.6f,-8.75f},.2f,1.3f,2.1f,1.5f,StartFinish::Steel,true},
    {"gun store safe door seam",{6.6f,-7.989f},.35f,1.16f,1.79f,.022f,StartFinish::DarkRoof},
    {"gun store safe door panel",{6.6f,-7.97f},.39f,1.06f,1.71f,.022f,StartFinish::Steel},
    {"gun store safe handle",{7,-7.91f},1.02f,.07f,.42f,.10f,StartFinish::DarkRoof},
    {"gun store exterior bin",{-7.1f,3.15f},.2f,.6f,.88f,.6f,StartFinish::Steel,true},
    {"gun store roof HVAC",{5,-5},4.38f,2.2f,.75f,1.7f,StartFinish::Steel},
    {"gun store HVAC cap",{5,-5},5.13f,2.3f,.06f,1.8f,StartFinish::DarkRoof},
    {"gun store rear utility cabinet",{-6,-11.3f},.1f,1.2f,1.8f,.7f,StartFinish::Steel,true},
};
inline constexpr BuildingPlan kGunStorePlan{
    "Brassline Arms",kGunStoreWalls,std::size(kGunStoreWalls),
    kGunStoreRoofs,std::size(kGunStoreRoofs),kGunStoreFixtures,std::size(kGunStoreFixtures)};

inline bool gun_store_ground_piece(const BuildingPiece& p) {
    return std::strcmp(p.name,"gun store lot")==0 ||
        std::strcmp(p.name,"gun store entrance walk")==0 ||
        std::strcmp(p.name,"gun store storefront walk")==0 ||
        std::strcmp(p.name,"gun store interior floor")==0 ||
        std::strcmp(p.name,"gun store interior threshold")==0;
}

inline std::vector<BuildingPiece> bake_gun_store() {
    auto out=bake_building(kGunStorePlan);
    const auto add=[&](const char* name,float x,float z,float y,float w,float h,float d,
                       StartFinish f,bool solid=false,float roll=0.f) {
        out.push_back({name,{x,z},y,w,h,d,f,solid,0,0,roll});
    };
    for(float x:{-5.3f,5.3f}) {
        for(float y:{.85f,3.f})
            add("gun store window frame",x,2.025f,y,3.22f,.07f,.13f,StartFinish::Steel);
        for(float dx:{-1.57f,-.52f,.52f,1.57f})
            add("gun store window security bar",x+dx,2.06f,.92f,.035f,2.08f,.035f,StartFinish::Steel,true);
        add("gun store window display plinth",x,1.2f,.2f,3,.65f,.9f,StartFinish::WarmWall,true);
        add("gun store window hard case",x,1.2f,.85f,1.1f,.22f,.48f,StartFinish::DarkRoof);
        for(float end:{-1.1f,1.1f})
            add("gun store window stock carton",x+end,1.2f,.85f,.46f,.33f,.48f,StartFinish::RedTrim);
    }
    for(float x:{-7.35f,7.35f}) {
        add("gun store shelf back",x+(x<0?-.33f:.33f),-3.2f,.2f,.08f,2.65f,5,StartFinish::WarmWall,true);
        for(float z:{-5.7f,-.7f})
            add("gun store shelf end",x,z,.2f,.75f,2.65f,.08f,StartFinish::Steel,true);
        for(float y:{.45f,1.15f,1.85f,2.55f}) {
            add("gun store shelf tray",x,-3.2f,y,.75f,.06f,5,StartFinish::WarmWall,true);
            for(int i=0;i<6;++i) {
                const float z=-5.15f+static_cast<float>(i)*.77f;
                add("gun store boxed stock",x,z,y+.06f,.46f,.28f,.56f,
                    i%2?StartFinish::RedTrim:StartFinish::WarmWall);
                add("gun store stock paper band",x,z,y+.12f,.47f,.09f,.57f,StartFinish::White);
            }
        }
    }
    // Recognizable original display silhouettes; no combat or pickups.
    for(int i=0;i<4;++i) {
        const float x=-2.7f+static_cast<float>(i)*1.8f;
        add("gun store display rifle stock",x-.38f,-9.47f,1.96f,.48f,.19f,.075f,StartFinish::WarmWall,false,-12);
        add("gun store display rifle receiver",x,-9.45f,2.06f,.40f,.13f,.065f,StartFinish::Steel);
        add("gun store display rifle barrel",x+.47f,-9.45f,2.11f,.56f,.048f,.045f,StartFinish::DarkRoof);
        add("gun store display rifle foregrip",x+.22f,-9.44f,2.04f,.22f,.085f,.07f,StartFinish::WarmWall);
        add("gun store display rifle trigger guard",x-.07f,-9.45f,1.98f,.13f,.075f,.035f,StartFinish::DarkRoof);
        add("gun store display rifle muzzle",x+.76f,-9.45f,2.10f,.045f,.067f,.055f,StartFinish::Steel);
        for(float dx:{-.42f,.35f})
            add("gun store rack peg",x+dx,-9.50f,1.9f,.05f,.22f,.20f,StartFinish::Steel);
        add("gun store rack price tag",x,-9.55f,1.52f,.35f,.16f,.015f,StartFinish::White);
    }
    for(float x:{-1.3f,1.3f}) {
        add("gun store handgun mat",x,-6.35f,1.265f,.8f,.015f,.5f,StartFinish::WarmWall);
        add("gun store display pistol slide",x,-6.31f,1.28f,.30f,.065f,.07f,StartFinish::Steel);
        add("gun store display pistol grip",x-.10f,-6.40f,1.28f,.075f,.055f,.18f,StartFinish::DarkRoof);
        add("gun store display pistol trigger",x,-6.37f,1.28f,.085f,.035f,.06f,StartFinish::DarkRoof);
    }
    for(float z:{-3.5f,-2.f}) {
        add("gun store island hard case",-4,z,1.15f,1.05f,.18f,.52f,StartFinish::DarkRoof);
        add("gun store hard case handle",-4,z+.30f,1.19f,.26f,.07f,.08f,StartFinish::Steel);
        for(float x:{-4.4f,-3.6f})
            add("gun store hard case latch",x,z+.27f,1.20f,.07f,.06f,.03f,StartFinish::Steel);
    }
    for(float x:{-4.f,4.f}) for(float z:{-1.f,-7.f}) {
        add("gun store ceiling light housing",x,z,4.0f,1.7f,.1f,.38f,StartFinish::Steel);
        add("gun store ceiling light lens",x,z,3.985f,1.55f,.025f,.29f,StartFinish::White);
    }
    // Dedicated wall washes make the vertical stock and clerk readable at
    // night; ceiling downlights mainly illuminate horizontal surfaces.
    for(float x:{-2.5f,0.f,2.5f}) {
        add("gun store rack light housing",x,-7.8f,3.82f,1.25f,.12f,.25f,StartFinish::Steel);
        add("gun store rack light lens",x,-7.84f,3.80f,1.1f,.025f,.20f,StartFinish::White);
    }
    for(float x:{-2.f,2.f}) {
        add("gun store counter light housing",x,-4,4.f,1.4f,.10f,.3f,StartFinish::Steel);
        add("gun store counter light lens",x,-4,3.985f,1.25f,.025f,.24f,StartFinish::White);
    }
    for(float x:{-2.5f,2.5f}) {
        add("gun store sign lamp arm",x,2.80f,5.34f,.07f,.07f,.6f,StartFinish::Steel);
        add("gun store sign lamp housing",x,3.11f,5.21f,.32f,.14f,.32f,StartFinish::DarkRoof);
        add("gun store exterior light lens",x,3.11f,5.19f,.26f,.025f,.26f,StartFinish::White);
    }
    for(float x:{-8.6f,-5.8f,-3.f,3.f,5.8f,8.6f})
        add("gun store parking stripe",x,13.15f,.104f,.09f,.008f,5.5f,StartFinish::White);
    for(float x:{-7.2f,-4.4f,4.4f,7.2f})
        add("gun store parking stop",x,15.5f,.1f,2.15f,.13f,.20f,StartFinish::Concrete,true);
    // Paint on asphalt keeps the vehicle maneuver aisle level. The central
    // raised walk is split around this crossing in the final bake below.
    for(float z=4.65f;z<10.4f;z+=.65f)
        add("gun store crossing paint",0,z,.104f,2.4f,.008f,.24f,StartFinish::White);
    for(auto& p:out) if(std::strcmp(p.name,"gun store entrance walk")==0) {
        p.centre.z=13.7f;p.depth_m=6.6f;
    }
    for(int i=0;i<7;++i)
        add("gun store HVAC louver",5,-4.135f,4.48f+static_cast<float>(i)*.075f,1.8f,.025f,.018f,StartFinish::DarkRoof);
    return out;
}
} // namespace apricot::city
