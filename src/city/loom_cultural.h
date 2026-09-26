#pragma once

#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include "city/loom_museum_interior.h"
#include "city/start_area.h"

namespace apricot::city {

// Loom's larger museum occupies the same block, with a Doric entrance facing
// Loom Way. The wings and raised entrance remain within the surveyed parcel.
inline constexpr StartSite kLoomMuseumSite{
    "Pinatty Museum", {-286,270}, .9946918f,-.1028992f,
    {0,-22},60,88,13,1500};
inline constexpr StartSite kLoomParkSite{
    "Sable Garden", {-365,329}, .988766f,.149465f,
    {0,0},30,22,13,1200};
struct LoomGallery { const char* title; float x,z,floor; };
inline constexpr LoomGallery kLoomGalleries[]={
    {"ART",-17,-8,.8f},{"ANTIQUITIES",17,-8,.8f},
    {"NATURAL HISTORY",-17,-44,.8f},{"PINATTY HISTORY",17,-44,.8f},
    {"SCIENCE",-17,-8,6.8f},{"SPACE",17,-8,6.8f},
    {"TRANSPORT",-17,-44,6.8f},{"DESIGN",17,-44,6.8f},
};
inline constexpr BuildingOpening kLoomMuseumEntrance[] = {
    {"museum public entrance",OpeningKind::Door,11,6,0,6.2f,
        BuildingFinish::Steel,0,0,BuildingFinish::WarmWall,false},
};
inline constexpr BuildingOpening kLoomMuseumWingWindows[] = {
    {"museum lower gallery window",OpeningKind::Window,4,2.6f,1.3f,3.2f,BuildingFinish::Glass,1,1,BuildingFinish::WarmWall},
    {"museum lower gallery window",OpeningKind::Window,12,2.6f,1.3f,3.2f,BuildingFinish::Glass,1,1,BuildingFinish::WarmWall},
    {"museum upper gallery window",OpeningKind::Window,4,2.6f,7.3f,3.2f,BuildingFinish::Glass,1,1,BuildingFinish::WarmWall},
    {"museum upper gallery window",OpeningKind::Window,12,2.6f,7.3f,3.2f,BuildingFinish::Glass,1,1,BuildingFinish::WarmWall},
};
inline constexpr auto kLoomMuseumSideWindows=[] {
    std::array<BuildingOpening,8> windows{};
    for(std::size_t floor=0;floor<2;++floor)for(std::size_t bay=0;bay<4;++bay)
        windows[floor*4+bay]={"museum side gallery window",OpeningKind::Window,
            9.f+static_cast<float>(bay)*18.f,2.6f,1.3f+static_cast<float>(floor)*6.f,3.2f,
            BuildingFinish::Glass,1,1,BuildingFinish::WarmWall};
    return windows;
}();
inline constexpr BuildingWall kLoomMuseumWalls[] = {
    {"museum west front stone wall",{-27,10},{-11,10},.8f,13,.6f,BuildingFinish::WarmWall,kLoomMuseumWingWindows,4},
    {"museum entrance stone wall",{-11,10},{11,10},.8f,13,.6f,BuildingFinish::WarmWall,kLoomMuseumEntrance,1},
    {"museum east front stone wall",{11,10},{27,10},.8f,13,.6f,BuildingFinish::WarmWall,kLoomMuseumWingWindows,4},
    {"museum west stone wall",{-27,-62},{-27,10},.8f,13,.6f,BuildingFinish::WarmWall,kLoomMuseumSideWindows.data(),8},
    {"museum east stone wall",{27,10},{27,-62},.8f,13,.6f,BuildingFinish::WarmWall,kLoomMuseumSideWindows.data(),8},
    {"museum rear stone wall",{27,-62},{-27,-62},.8f,13,.6f,BuildingFinish::WarmWall},
};
inline constexpr BuildingRoof kLoomMuseumRoofs[] = {
    {"museum west gallery roof",{-19,-26},13.8f,16,72,0,.28f,.45f,.55f,RoofStyle::Flat,
        RidgeAxis::AlongX,BuildingFinish::DarkRoof,BuildingFinish::WarmWall},
    {"museum east gallery roof",{19,-26},13.8f,16,72,0,.28f,.45f,.55f,RoofStyle::Flat,
        RidgeAxis::AlongX,BuildingFinish::DarkRoof,BuildingFinish::WarmWall},
    {"museum great hall roof",{0,-26},13.8f,22,72,0,.28f,.45f,.45f,RoofStyle::Flat,
        RidgeAxis::AlongX,BuildingFinish::DarkRoof,BuildingFinish::WarmWall},
    {"museum pediment roof",{0,13},11.9f,22,7,3.6f,.26f,.45f,0,RoofStyle::Gable,
        RidgeAxis::AlongZ,BuildingFinish::WarmWall,BuildingFinish::WarmWall,.38f,BuildingFinish::WarmWall},
};
inline constexpr BuildingPiece kLoomMuseumFixtures[] = {
    {"museum lot",{0,-22},0,60,.1f,88,BuildingFinish::Concrete},
    {"museum interior floor",{0,-26},.1f,54,.7f,72,BuildingFinish::Concrete,true},
    {"museum upper interior floor west",{-17,-26},6.55f,20,.25f,72,BuildingFinish::Concrete,true},
    {"museum upper interior floor east",{17,-26},6.55f,20,.25f,72,BuildingFinish::Concrete,true},
    {"museum upper interior floor north landing",{0,-53},6.55f,14,.25f,18,BuildingFinish::Concrete,true},
    {"museum upper interior floor south landing",{0,-20},6.55f,14,.25f,8,BuildingFinish::Concrete,true},
    {"museum upper interior floor west stair bypass",{-5,-34},6.55f,4,.25f,20,BuildingFinish::Concrete,true},
    {"museum upper interior floor east stair bypass",{5,-34},6.55f,4,.25f,20,BuildingFinish::Concrete,true},
    {"museum threshold",{0,10},.1f,6,.7f,1.2f,BuildingFinish::Concrete,true},
    {"museum entrance terrace",{0,13},.1f,22,.7f,6,BuildingFinish::Concrete,true},
    {"museum entrance walk",{0,20.5f},.1f,22,.1f,3,BuildingFinish::Concrete,true},
    {"museum gallery ceiling",{0,-26},13.6f,53.4f,.12f,71.4f,BuildingFinish::White},
    {"museum portico ceiling",{0,13},11.6f,21.4f,.16f,6.4f,BuildingFinish::White},
    // Reception and the hall centrepiece live in loom_museum_interior.h with
    // the rest of the furniture. The plan keeps only the shell: floors,
    // ceilings and the pieces the wall baker needs.
};
inline constexpr BuildingStair kLoomMuseumStairs[] = {
    {"museum entrance stair",{0,17.5f},.2f,22,3,.6f,4,180,BuildingFinish::Concrete,true},
    {"museum gallery stair",{0,-34},.8f,5,20,6,40,180,BuildingFinish::WarmWall,true},
};
inline constexpr BuildingPlan kLoomMuseumPlan{
    "Pinatty Museum",kLoomMuseumWalls,std::size(kLoomMuseumWalls),
    kLoomMuseumRoofs,std::size(kLoomMuseumRoofs),kLoomMuseumFixtures,std::size(kLoomMuseumFixtures),
    kLoomMuseumStairs,std::size(kLoomMuseumStairs)};

// Small raised metal capitals. These are geometry, so there is no raster sign
// dependency and the authored spelling is shared with its tests.
inline void loom_letters(std::vector<StartPart>& out,const char* text,
                         float z,float bottom,float pixel) {
    const float left=-static_cast<float>(std::strlen(text))*6*pixel*.5f;
    for(std::size_t i=0;text[i];++i) {
        std::array<unsigned,7> rows{};
        switch(text[i]) {
            case 'A':rows={14,17,17,31,17,17,17};break;
            case 'E':rows={31,16,16,30,16,16,31};break;
            case 'L':rows={16,16,16,16,16,16,31};break;
            case 'M':rows={17,27,21,21,17,17,17};break;
            case 'O':rows={14,17,17,17,17,17,14};break;
            case 'R':rows={30,17,17,30,20,18,17};break;
            case 'S':rows={15,16,16,14,1,1,30};break;
            case 'T':rows={31,4,4,4,4,4,4};break;
            case 'U':rows={17,17,17,17,17,17,14};break;
            case 'B':rows={30,17,17,30,17,17,30};break;
            case 'C':rows={14,17,16,16,16,17,14};break;
            case 'D':rows={30,17,17,17,17,17,30};break;
            case 'F':rows={31,16,16,30,16,16,16};break;
            case 'G':rows={14,17,16,23,17,17,14};break;
            case 'H':rows={17,17,17,31,17,17,17};break;
            case 'I':rows={31,4,4,4,4,4,31};break;
            case 'N':rows={17,25,25,21,19,19,17};break;
            case 'P':rows={30,17,17,30,16,16,16};break;
            case 'Q':rows={14,17,17,17,21,18,13};break;
            case 'V':rows={17,17,17,17,17,10,4};break;
            case 'Y':rows={17,17,10,4,4,4,4};break;
            default:break;
        }
        for(int row=0;row<7;++row) for(int col=0;col<5;++col)
            if(rows[static_cast<std::size_t>(row)]&(1u<<(4-col)))
                out.push_back({"museum raised sign letter",
                    {left+(static_cast<float>(i)*6+static_cast<float>(col)+.5f)*pixel,z},
                    bottom+static_cast<float>(6-row)*pixel,pixel*.9f,pixel*.9f,.04f,BuildingFinish::White});
    }
}

inline void loom_bench(std::vector<StartPart>& out,float x,float z) {
    out.push_back({"garden timber bench seat",{x,z},.55f,2.5f,.16f,.65f,BuildingFinish::WarmWall,true});
    out.push_back({"garden timber bench back",{x,z+.28f},.71f,2.5f,.65f,.12f,BuildingFinish::WarmWall,true});
    for(float side:{-1.f,1.f})
        out.push_back({"garden bench steel leg",{x+side,z},.1f,.12f,.45f,.6f,BuildingFinish::Steel,true});
}


inline std::vector<StartPart> bake_loom_museum() {
    auto out=bake_building(kLoomMuseumPlan);
    const auto add=[&](const char* name,float x,float z,float y,float w,float h,float d,
                       BuildingFinish finish=BuildingFinish::WarmWall,bool solid=false,
                       float yaw=0,float roll=0) {
        out.push_back({name,{x,z},y,w,h,d,finish,solid,0,yaw,roll});
    };
    add("museum foundation",0,-26,-.8f,54,.9f,72,BuildingFinish::Concrete,true);
    for(float x:{-29.85f,29.85f})
        add("museum terrace retaining edge",x,-22,-.8f,.3f,.9f,88,BuildingFinish::WarmWall,true);
    add("museum terrace retaining edge",0,-65.85f,-.8f,60,.9f,.3f,BuildingFinish::WarmWall,true);
    for(float x:{-16.3f,16.3f})
        add("museum terrace retaining edge",x,21.85f,-.8f,27.4f,.9f,.3f,BuildingFinish::WarmWall,true);
    // Four fluted Doric columns, with stepped bases, neck rings and capitals.
    for(float x:{-8.4f,-2.8f,2.8f,8.4f}) {
        add("museum column plinth",x,14,.8f,1.95f,.3f,1.95f,BuildingFinish::WarmWall,true);
        add("museum round column base",x,14,1.1f,1.8f,.22f,1.8f);
        add("museum round column shaft",x,14,1.32f,1.4f,9.0f,1.4f,BuildingFinish::WarmWall,true);
        add("museum round column neck",x,14,10.32f,1.6f,.28f,1.6f);
        add("museum round column capital",x,14,10.6f,1.9f,.4f,1.9f);
        add("museum column abacus",x,14,11,2.05f,.25f,2.05f);
        for(int flute=0;flute<16;++flute) {
            const float angle=static_cast<float>(flute)*6.2831853f/16;
            add("museum column flute",x+.695f*std::sin(angle),14+.695f*std::cos(angle),
                1.55f,.07f,8.5f,.06f,BuildingFinish::Concrete,false,angle*57.29578f);
        }
    }
    add("museum portico architrave",0,14,11.25f,22.5f,.65f,3.0f);
    add("museum entablature frieze",0,16.4f,10.95f,22,.95f,.6f);
    add("museum portico cornice",0,16.65f,11.7f,23,.25f,.8f);
    for(float x=-10.5f;x<=10.5f;x+=1.5f) {
        add("museum triglyph",x,16.7f,11.28f,.24f,.45f,.15f,BuildingFinish::Concrete);
        add("museum dentil",x,16.96f,11.65f,.3f,.16f,.25f);
    }
    loom_letters(out,"LOOM MUSEUM",16.85f,10.95f,.105f);
    // Continuous stone courses, plinths and cornices give the broad wings scale.
    for(float x:{-19.f,19.f}) {
        for(float y:{1.1f,5.8f,6.5f,12.4f,13.55f})
            add("museum facade stone course",x,10.35f,y,16,.16f,.23f);
        add("museum wing cornice",x,10.5f,13.65f,16.8f,.35f,.65f);
        for(float px:{x-7.4f,x,x+7.4f}) {
            add("museum engaged pilaster",px,10.35f,1.3f,.75f,12.3f,.38f);
            add("museum pilaster capital",px,10.4f,13.1f,1.15f,.4f,.55f);
        }
    }
    append_loom_galleries(out);
    // Broad, walkable forecourt with low planters outside the stair route.
    for(float x:{-23.f,23.f}) {
        add("museum forecourt planter",x,16,.1f,6,.65f,5,BuildingFinish::WarmWall,true);
        add("museum forecourt soil",x,16,.75f,5.5f,.06f,4.5f,BuildingFinish::DarkRoof);
        for(float dx:{-1.8f,0.f,1.8f})
            add("museum forecourt shrub",x+dx,16,.81f,1.6f,.7f,3.2f,BuildingFinish::TealDoor);
    }
    return out;
}

inline std::vector<StartPart> bake_loom_park() {
    const BuildingRoof roofs[] = {
        {"garden gazebo green roof",{0,1},3.5f,8,7,2,.18f,.6f,0,RoofStyle::Gable,
            RidgeAxis::AlongZ,BuildingFinish::TealDoor,BuildingFinish::WarmWall},
    };
    const BuildingPlan plan{"Sable Garden gazebo",nullptr,0,roofs,1};
    auto out=bake_building(plan);
    const auto add=[&](const char* name,float x,float z,float y,float w,float h,float d,
                        BuildingFinish finish,bool solid=false,float yaw=0.f) {
        out.push_back({name,{x,z},y,w,h,d,finish,solid,0,yaw});
    };
    add("garden entrance walk",0,-6.5f,.1f,4.4f,.1f,9,BuildingFinish::Concrete,true);
    add("garden cross walk",0,1,.1f,25,.1f,2.6f,BuildingFinish::Concrete,true);
    add("garden gazebo floor",0,1,.1f,8,.1f,7,BuildingFinish::WarmWall,true);
    for(float x:{-3.7f,3.7f}) for(float z:{-2.2f,4.2f}) {
        add("garden gazebo timber post",x,z,.2f,.28f,3.3f,.28f,BuildingFinish::WarmWall,true);
        add("garden gazebo post foot",x,z,.2f,.45f,.18f,.45f,BuildingFinish::Steel,true);
    }
    for(float x:{-3.7f,3.7f}) {
        add("garden gazebo timber beam",x,1,3.25f,.30f,.25f,7,BuildingFinish::WarmWall);
        add("garden gazebo timber rail",x,1,1.10f,.12f,.15f,6.4f,BuildingFinish::WarmWall,true);
        for(float z:{-1.7f,-.9f,-.1f,.7f,1.5f,2.3f,3.1f,3.9f})
            add("garden gazebo timber baluster",x,z,.2f,.06f,.9f,.06f,BuildingFinish::WarmWall,true);
    }
    // Two open ends and a level, step-free path through the shelter.
    for(float z:{-2.2f,4.2f})
        add("garden gazebo timber beam",0,z,3.25f,7.7f,.25f,.30f,BuildingFinish::WarmWall);
    add("garden gazebo light lens",0,1,3.2f,.45f,.12f,.45f,BuildingFinish::White);
    loom_bench(out,-10,2.8f); loom_bench(out,10,2.8f);
    loom_bench(out,0,3.6f);
    for(const Vec2 p: {Vec2{-11,-7},Vec2{11,-7},Vec2{-10,8},Vec2{10,8}}) {
        add("garden tree trunk",p.x,p.z,0,.5f,3,.5f,BuildingFinish::WarmWall,true);
        add("garden tree crown",p.x,p.z,2.4f,4.4f,2.2f,4.4f,BuildingFinish::TealDoor,false,20);
        add("garden tree crown",p.x+.3f,p.z,4,3.2f,1.8f,3.2f,BuildingFinish::TealDoor,false,-15);
    }
    for(float x:{-8.f,8.f}) {
        add("garden flower bed rim",x,-4,.02f,4,.25f,2.8f,BuildingFinish::WarmWall,true);
        add("garden flower bed soil",x,-4,.27f,3.7f,.04f,2.5f,BuildingFinish::DarkRoof);
        for(int i=0;i<5;++i) for(int j=0;j<3;++j)
            add("garden flower cluster",x-1.4f+static_cast<float>(i)*.7f,-4.8f+static_cast<float>(j)*.8f,.32f,.35f,.22f,.35f,
                (i+j)%2?BuildingFinish::Yellow:BuildingFinish::RedTrim);
    }
    return out;
}

inline bool loom_ground_piece(const StartPart& p) {
    return p.name && (std::strstr(p.name,"interior floor") || std::strstr(p.name,"museum gallery stair") ||
        std::strstr(p.name,"museum entrance terrace") || std::strstr(p.name,"museum entrance stair") ||
        std::strstr(p.name,"museum threshold") || std::strstr(p.name,"museum entrance walk") ||
        std::strstr(p.name,"garden entrance walk") || std::strstr(p.name,"garden cross walk") ||
        std::strstr(p.name,"garden gazebo floor"));
}
} // namespace apricot::city
