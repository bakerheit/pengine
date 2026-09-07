#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "city/skyscraper_window_lighting.h"
#include "city/start_area.h"

namespace apricot::city {

enum class TowerCrownStyle : uint8_t {
    PlantRoom,
    BeaconMast,
    Lantern,
    TwinFins,
    Terrace,
};

enum class TowerFrontage : uint8_t {
    North,
    South,
};

struct NeighborhoodTower {
    StartSite site;
    float shaft_width_m;
    float shaft_depth_m;
    int floors;
    BuildingFinish masonry;
    BuildingFinish trim;
    int setback_floor;
    SkyscraperUse use;
    TowerCrownStyle crown_style = TowerCrownStyle::PlantRoom;
    TowerFrontage frontage = TowerFrontage::North;
};

constexpr Vec2 tower_grid_point(float east,float south) {
    return {70.f+kGridCos*east+kGridSin*south,
            -40.f-kGridSin*east+kGridCos*south};
}

// Existing 92 x 62m downtown blocks. These sites are kept inside Vellum Row's
// authored grid; none consumes its forecourt, nearby businesses or road
// corridors.
inline constexpr std::array<NeighborhoodTower,22> kNeighborhoodTowers{{
    {{"Mercer Exchange",tower_grid_point(-138,-93),kGridCos,kGridSin,{0,0},54,38,12,1800},
        34.f,24.f,22,BuildingFinish::Brick,BuildingFinish::WarmWall,16,
        SkyscraperUse::Office,TowerCrownStyle::TwinFins},
    {{"Halloway Centre",tower_grid_point(138,-93),kGridCos,kGridSin,{0,0},54,38,12,2000},
        28.f,22.f,28,BuildingFinish::Steel,BuildingFinish::TealDoor,23,
        SkyscraperUse::Office,TowerCrownStyle::BeaconMast},
    {{"Ashford House",tower_grid_point(230,93),kGridCos,kGridSin,{0,0},54,38,12,1600},
        36.f,24.f,18,BuildingFinish::WarmWall,BuildingFinish::Brick,12,
        SkyscraperUse::Residential,TowerCrownStyle::Terrace},
    // The east side is a deliberate high-rise run up the unused Ashford
    // frontage, with different crowns so it reads as a neighbourhood and not
    // one duplicated prefab.
    {{"Juniper Spire",tower_grid_point(230,-93),kGridCos,kGridSin,{0,0},54,38,12,1900},
        30.f,23.f,24,BuildingFinish::Brick,BuildingFinish::RedTrim,16,
        SkyscraperUse::Mixed,TowerCrownStyle::Lantern},
    {{"Cinder Pinnacle",tower_grid_point(230,-155),kGridCos,kGridSin,{0,0},54,38,12,2200},
        30.f,22.f,30,BuildingFinish::Steel,BuildingFinish::RedTrim,24,
        SkyscraperUse::Office,TowerCrownStyle::TwinFins},
    {{"Wren Financial",tower_grid_point(230,-217),kGridCos,kGridSin,{0,0},54,38,12,2100},
        32.f,24.f,26,BuildingFinish::Steel,BuildingFinish::TealDoor,20,
        SkyscraperUse::Office},
    {{"Eastbank Crown",tower_grid_point(230,155),kGridCos,kGridSin,{0,0},54,38,12,1900},
        26.f,20.f,28,BuildingFinish::Steel,BuildingFinish::WarmWall,22,
        SkyscraperUse::Mixed,TowerCrownStyle::Lantern},
    {{"Vellum Gate",tower_grid_point(230,217),kGridCos,kGridSin,{0,0},54,38,12,1700},
        34.f,25.f,22,BuildingFinish::WarmWall,BuildingFinish::Brick,14,
        SkyscraperUse::Residential,TowerCrownStyle::Terrace},
    {{"Halloway Annex",tower_grid_point(230,-31),kGridCos,kGridSin,{0,0},54,30,12,1800},
        28.f,20.f,20,BuildingFinish::Brick,BuildingFinish::TealDoor,13,
        SkyscraperUse::Mixed},
    {{"Ashford Exchange",tower_grid_point(230,31),kGridCos,kGridSin,{0,0},54,30,12,1800},
        32.f,21.f,23,BuildingFinish::Steel,BuildingFinish::RedTrim,18,
        SkyscraperUse::Office,TowerCrownStyle::TwinFins},
    {{"Northline Tower",tower_grid_point(230,-279),kGridCos,kGridSin,{0,0},54,38,12,1600},
        30.f,23.f,25,BuildingFinish::WarmWall,BuildingFinish::Steel,19,
        SkyscraperUse::Mixed,TowerCrownStyle::BeaconMast},
    // Six different towers populate the two new Vellum blocks north of Tenth.
    // They all sit east of the arterial spine, keeping the western edge open
    // for the hospital approaches and the long Route 1 view.
    {{"Cinder North Court",tower_grid_point(322,-341),kGridCos,kGridSin,{0,0},54,30,12,1850},
        30.f,22.f,22,BuildingFinish::WarmWall,BuildingFinish::TealDoor,14,
        SkyscraperUse::Residential,TowerCrownStyle::Terrace},
    {{"Juniper North Tower",tower_grid_point(138,-341),kGridCos,kGridSin,{0,0},54,30,12,2350},
        29.f,23.f,31,BuildingFinish::Steel,BuildingFinish::RedTrim,23,
        SkyscraperUse::Office,TowerCrownStyle::BeaconMast},
    {{"Ashford Crest",tower_grid_point(230,-341),kGridCos,kGridSin,{0,0},54,30,12,2150},
        34.f,24.f,27,BuildingFinish::Brick,BuildingFinish::WarmWall,18,
        SkyscraperUse::Mixed,TowerCrownStyle::Lantern},
    {{"Cinder Quarter Residences",tower_grid_point(322,-403),kGridCos,kGridSin,{0,0},54,30,12,1950},
        35.f,25.f,24,BuildingFinish::WarmWall,BuildingFinish::Brick,15,
        SkyscraperUse::Residential},
    {{"Juniper Beacon",tower_grid_point(138,-403),kGridCos,kGridSin,{0,0},54,30,12,2500},
        28.f,22.f,34,BuildingFinish::Steel,BuildingFinish::TealDoor,25,
        SkyscraperUse::Office,TowerCrownStyle::TwinFins},
    {{"Cinder Gate",tower_grid_point(230,-403),kGridCos,kGridSin,{0,0},54,30,12,2250},
        32.f,24.f,29,BuildingFinish::Brick,BuildingFinish::RedTrim,20,
        SkyscraperUse::Mixed,TowerCrownStyle::Lantern},
    // Southeast Vellum fills five surveyed gaps on the district's flat 12 m
    // plate. The two outer sites step eight metres west and use narrower lots
    // to stay clear of Nickel Heights' terrain feather and the avenue walk.
    {{"Bellweather Crown",tower_grid_point(312,155),kGridCos,kGridSin,{0,0},46,38,12,2400},
        28.f,21.f,33,BuildingFinish::Steel,BuildingFinish::TealDoor,25,
        SkyscraperUse::Office,TowerCrownStyle::BeaconMast},
    {{"Mariner House",tower_grid_point(312,217),kGridCos,kGridSin,{0,0},46,38,12,1850},
        35.f,25.f,24,BuildingFinish::WarmWall,BuildingFinish::Brick,15,
        SkyscraperUse::Residential,TowerCrownStyle::Terrace},
    // Three west-side towers finish the block face north of Tenth Street.
    // Their lobbies face south toward the only public frontage; Eleventh
    // Street intentionally remains an east-side road beyond the Ferrone fork.
    {{"Briar Sentinel",tower_grid_point(-414,-341),kGridCos,kGridSin,{0,0},54,38,12,2050},
        31.f,23.f,29,BuildingFinish::Brick,BuildingFinish::Steel,22,
        SkyscraperUse::Mixed,TowerCrownStyle::TwinFins,TowerFrontage::South},
    {{"Mercer North",tower_grid_point(-322,-341),kGridCos,kGridSin,{0,0},54,38,12,1900},
        35.f,24.f,23,BuildingFinish::WarmWall,BuildingFinish::Brick,15,
        SkyscraperUse::Residential,TowerCrownStyle::Terrace,TowerFrontage::South},
    {{"Bellweather Point",tower_grid_point(-230,-341),kGridCos,kGridSin,{0,0},54,38,12,2200},
        29.f,22.f,32,BuildingFinish::Steel,BuildingFinish::TealDoor,25,
        SkyscraperUse::Office,TowerCrownStyle::BeaconMast,TowerFrontage::South},
}};
inline constexpr float kTowerPodiumHeightM=7.5f;
inline constexpr float kTowerFloorHeightM=3.4f;
inline constexpr int kTowerFrontRearWindowBays=8;
inline constexpr int kTowerSideWindowBays=6;
inline constexpr int kTowerWindowsPerFloor=
    kTowerFrontRearWindowBays*2+kTowerSideWindowBays*2;

inline float neighborhood_tower_roof(const NeighborhoodTower& tower) {
    return kTowerPodiumHeightM+static_cast<float>(tower.floors)*kTowerFloorHeightM;
}

inline std::vector<StartPart> bake_neighborhood_tower(std::size_t index) {
    const auto& tower=kNeighborhoodTowers.at(index);
    std::vector<StartPart> out;
    out.reserve(1050);
    const auto add=[&](const char* name,float x,float z,float bottom,
                       float width,float height,float depth,BuildingFinish finish,
                       bool solid=false) {
        out.push_back({name,{x,z},bottom,width,height,depth,finish,solid});
    };
    // A paved plaza reaches the sidewalk through a separate short apron.
    // The apron ends inside the pavement, two metres clear of the road edge.
    add("tower plaza lot",tower.site.lot_centre.x,tower.site.lot_centre.z,.02f,
        tower.site.lot_width_m,.08f,tower.site.lot_depth_m,BuildingFinish::Concrete);
    const bool compact_lot=tower.site.lot_depth_m<34.f;
    add("tower entrance walk",0,compact_lot?-14.2f:-20.5f,.08f,5,.10f,
        compact_lot?1.2f:3.f,BuildingFinish::Concrete);
    add("tower podium floor",0,1,.10f,44,.10f,28,BuildingFinish::Concrete);
    add("tower podium west wall",-21.8f,1,.2f,.4f,7.3f,28,tower.masonry,true);
    add("tower podium east wall",21.8f,1,.2f,.4f,7.3f,28,tower.masonry,true);
    add("tower podium rear wall",0,14.8f,.2f,44,7.3f,.4f,tower.masonry,true);
    // Recessed open central doorway, flanked by display/lobby windows. The
    // podium has an actual lobby volume rather than a solid box behind glass.
    for(float side:{-1.f,1.f}) {
        add("tower entrance pier",side*3.4f,-12.8f,.2f,.7f,5.8f,.5f,tower.trim,true);
        add("tower podium front sill",side*13.f,-12.8f,.2f,17.3f,.65f,.4f,tower.masonry,true);
        add("tower podium lobby glass",side*13.f,-12.86f,.85f,17.3f,4.65f,.055f,BuildingFinish::Glass,true);
        for(int column=0;column<6;++column)
            add("tower lobby mullion",side*(5.4f+float(column)*2.8f),-12.93f,.85f,.12f,4.65f,.10f,tower.trim);
    }
    add("tower podium front lintel",0,-12.8f,5.5f,44,2.f,.4f,tower.masonry,true);
    add("tower entrance canopy",0,-14.2f,4.5f,8.4f,.3f,4.f,tower.trim);
    add("tower lobby rear divider",0,-5.6f,.2f,44,7.3f,.3f,tower.masonry,true);
    add("tower lobby reception desk",6,-7.4f,.2f,5,1.05f,1.2f,tower.trim,true);
    add("tower lobby desk top",6,-7.4f,1.25f,5.2f,.1f,1.3f,BuildingFinish::Concrete);
    add("tower podium roof",0,1,7.25f,44.4f,.35f,28.4f,tower.trim,true);
    for(float side:{-1.f,1.f}) {
        add("tower plaza planter",side*16.f,-16.2f,.1f,6.4f,.55f,1.4f,BuildingFinish::Concrete,true);
        add("tower planter soil",side*16.f,-16.2f,.61f,5.9f,.06f,1.1f,BuildingFinish::DarkRoof);
        add("tower plaza bench",side*8.8f,-16.f,.5f,3.5f,.15f,.65f,BuildingFinish::Steel,true);
        add("tower plaza bench legs",side*8.8f,-16.f,.1f,2.6f,.4f,.4f,BuildingFinish::Steel,true);
    }
    const auto tier=[&](float width,float depth,int first,int count) {
        const float bottom=kTowerPodiumHeightM+float(first)*kTowerFloorHeightM;
        const float height=float(count)*kTowerFloorHeightM;
        add("tower structural core",0,1,bottom,width,height,depth,tower.masonry,true);
        // Glazing is recessed behind piers, with physical floor bands and
        // mullions. Four faces carry the rhythm, including the rear skyline.
        for(float side:{-1.f,1.f}) {
            add("tower front rear glazing",0,1+side*(depth*.5f+.035f),bottom+.35f,
                width-1.2f,height-.65f,.055f,BuildingFinish::Glass);
            add("tower side glazing",side*(width*.5f+.035f),1,bottom+.35f,
                .055f,height-.65f,depth-1.2f,BuildingFinish::Glass);
            for(int col=0;col<=8;++col) {
                const float x=-width*.5f+float(col)*width/8.f;
                add("tower facade vertical pier",x,1+side*(depth*.5f+.12f),bottom,
                    index==1u?.18f:.42f,height,.28f,tower.trim);
            }
            for(int col=0;col<=6;++col) {
                const float z=1-depth*.5f+float(col)*depth/6.f;
                add("tower side vertical pier",side*(width*.5f+.12f),z,bottom,
                    .28f,height,index==1u?.18f:.42f,tower.trim);
            }
        }
        // The large recessed glass sheets above remain the dark daytime and
        // unoccupied-night backing. These shallow, non-solid panes sit just
        // in front of them, one per office suite. World owns their live
        // emissive state, while the physical piers and spandrels keep the
        // windows reading as real facade bays instead of a blinking texture.
        for(int floor=0;floor<count;++floor) {
            const float window_bottom=bottom+float(floor)*kTowerFloorHeightM+.48f;
            constexpr float window_height=2.48f;
            const float front_bay_width=width/float(kTowerFrontRearWindowBays);
            const float side_bay_width=depth/float(kTowerSideWindowBays);
            for(float side:{-1.f,1.f}) {
                const float face_z=1+side*(depth*.5f+.077f);
                for(int bay=0;bay<kTowerFrontRearWindowBays;++bay) {
                    const float x=-width*.5f+(float(bay)+.5f)*front_bay_width;
                    add("tower office window light",x,face_z,window_bottom,
                        front_bay_width-.52f,window_height,.025f,
                        BuildingFinish::Glass);
                }
                const float face_x=side*(width*.5f+.077f);
                for(int bay=0;bay<kTowerSideWindowBays;++bay) {
                    const float z=1-depth*.5f+(float(bay)+.5f)*side_bay_width;
                    add("tower office window light",face_x,z,window_bottom,
                        .025f,window_height,side_bay_width-.52f,
                        BuildingFinish::Glass);
                }
            }
        }
        for(int floor=0;floor<=count;++floor) {
            const float y=bottom+float(floor)*kTowerFloorHeightM;
            add("tower floor spandrel",0,1,y,width+.30f,.34f,depth+.30f,tower.trim);
            if(index!=1u && floor%4==0)
                add("tower masonry belt course",0,1,y+.34f,width+.65f,.15f,depth+.65f,tower.masonry);
        }
    };
    tier(tower.shaft_width_m,tower.shaft_depth_m,0,tower.setback_floor);
    const float roof=neighborhood_tower_roof(tower);
    const float setback_y=kTowerPodiumHeightM+float(tower.setback_floor)*kTowerFloorHeightM;
    add("tower setback terrace",0,1,setback_y,tower.shaft_width_m+.5f,.25f,tower.shaft_depth_m+.5f,BuildingFinish::Concrete);
    tier(tower.shaft_width_m-6.f,tower.shaft_depth_m-5.f,tower.setback_floor,tower.floors-tower.setback_floor);
    add("tower crown cap",0,1,roof,tower.shaft_width_m-5.3f,.65f,tower.shaft_depth_m-4.3f,tower.trim);
    add("tower rooftop plant room",0,2,roof+.65f,10,3.4f,8,tower.masonry,true);
    for(float side:{-1.f,1.f}) {
        add("tower rooftop ventilation unit",side*7.3f,2,roof+.65f,2.6f,1.4f,3.4f,BuildingFinish::Steel,true);
        add("tower rooftop grille",side*7.3f,.24f,roof+.9f,2.25f,.9f,.06f,BuildingFinish::DarkRoof);
    }
    switch (tower.crown_style) {
        case TowerCrownStyle::PlantRoom:
            break;
        case TowerCrownStyle::BeaconMast:
            add("tower antenna mast",0,2,roof+4.05f,.18f,5.6f,.18f,
                BuildingFinish::Steel);
            add("tower mast beacon",0,2,roof+9.65f,.32f,.3f,.32f,
                BuildingFinish::RedTrim);
            break;
        case TowerCrownStyle::Lantern:
            add("tower crown lantern",0,2,roof+.65f,8.2f,4.4f,6.2f,
                BuildingFinish::Glass);
            add("tower lantern cap",0,2,roof+5.05f,8.8f,.35f,6.8f,
                tower.trim);
            for (float x : {-3.5f, 3.5f})
                add("tower lantern mullion",x,2,roof+.65f,.20f,4.4f,.20f,
                    tower.trim);
            break;
        case TowerCrownStyle::TwinFins:
            for (float x : {-5.3f, 5.3f})
                add("tower crown fin",x,2,roof+.65f,2.5f,6.0f,4.8f,
                    tower.trim);
            break;
        case TowerCrownStyle::Terrace:
            for (float x : {-5.5f, 5.5f}) {
                add("tower roof terrace planter",x,2,roof+.65f,3.6f,.65f,
                    2.4f,BuildingFinish::Concrete,true);
                add("tower roof terrace planting",x,2,roof+1.30f,3.0f,.25f,
                    1.8f,BuildingFinish::WarmWall);
            }
            break;
    }
    if (tower.frontage == TowerFrontage::South) {
        for (auto& part : out) {
            part.centre.x = -part.centre.x;
            part.centre.z = -part.centre.z;
            part.yaw_deg += 180.0f;
        }
    }
    return out;
}

}  // namespace apricot::city
