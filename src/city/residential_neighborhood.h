#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include "city/start_area.h"
#include "road/road_graph.h"

namespace apricot::city {
struct ResidentialHouse {
    StartSite site;
    int number;
    float width_m;
    float wall_height_m;
    float roof_rise_m;
    StartFinish wall_finish;
    StartFinish trim_finish;
    bool enterable;
};

struct ResidentialTextureStyle {
    const char* wall_texture;
    const char* roof_texture;
};
// A separate suburban destination on existing Sycamore Loop, ~1.7km by road
// from Halloway Gas. The 10m datum stays below the gently rolling terrain;
// foundations/paving sample the real mesh rather than flattening the yards.
inline constexpr std::array<ResidentialHouse,6> kResidentialHouses{{
    {{"101 Sycamore Loop",{993.959798f,222.004414f},-.989949494f,-.141421356f,{0,0},32,34,10,950},101,13,3.05f,1.55f,StartFinish::WarmWall,StartFinish::White,false},
    {{"102 Sycamore Loop",{1033.959798f,216.290128f},-.989949494f,-.141421356f,{0,0},32,34,10,950},102,14,3.15f,1.75f,StartFinish::WarmWall,StartFinish::TealDoor,true},
    {{"103 Sycamore Loop",{1125.397815f,248.927461f},-.747409319f,.664363839f,{0,0},32,34,10,950},103,15,3.35f,1.20f,StartFinish::Brick,StartFinish::White,false},
    {{"104 Sycamore Loop",{1011.994918f,324.214012f},.997458700f,-.071247050f,{0,0},32,34,10,950},104,14,3.05f,1.40f,StartFinish::WarmWall,StartFinish::RedTrim,false},
    {{"105 Sycamore Loop",{1051.994918f,327.071155f},.997458700f,-.071247050f,{0,0},32,34,10,950},105,13,3.25f,1.70f,StartFinish::Brick,StartFinish::TealDoor,false},
    {{"106 Sycamore Loop",{1091.994918f,329.928298f},.997458700f,-.071247050f,{0,0},32,34,10,950},106,15,3.10f,1.30f,StartFinish::WarmWall,StartFinish::White,false},
}};
inline constexpr std::size_t kResidentialTargetHouse=1;
// A few closed houses borrow only the PSX Houses 100 repeating surface maps.
// Geometry, openings, collision and the enterable 102 interior stay authored.
inline constexpr std::array<ResidentialTextureStyle,6> kResidentialTextureStyles{{
    {"Wall_1.png","Roof_3.png"},
    {nullptr,nullptr},
    {nullptr,nullptr},
    {"Wall_3.png","Roof_3.png"},
    {nullptr,nullptr},
    {"Wall_2.png","Roof_5.png"},
}};

inline bool residential_has_imported_texture(std::size_t index) {
    return index < kResidentialTextureStyles.size() &&
           kResidentialTextureStyles[index].wall_texture != nullptr;
}

inline std::size_t residential_house_index(const StartSite& site) {
    for(std::size_t i=0;i<kResidentialHouses.size();++i)
        if(&site==&kResidentialHouses[i].site)return i;
    return kResidentialHouses.size();
}

inline bool residential_wall_texture_piece(const StartPart& part) {
    if(!part.name)return false;
    const auto has=[&](const char* text){return std::strstr(part.name,text)!=nullptr;};
    return std::strcmp(part.name,"gable end base")==0 ||
        part.shape==BuildingPieceShape::GablePrism || has("house front wall") ||
        has("house rear wall") || has("house garden wall") ||
        has("house driveway wall");
}

inline bool residential_roof_texture_piece(const StartPart& part) {
    return part.name && std::strstr(part.name,"house pitched roof")!=nullptr;
}
inline glm::vec2 residential_world(const StartSite& s,glm::vec2 p) {
    return {s.origin.x+s.cos_yaw*p.x+s.sin_yaw*p.y,
            s.origin.z-s.sin_yaw*p.x+s.cos_yaw*p.y};
}
inline float residential_height(const StartSite& s,GroundSampler ground,float x,float z) {
    const auto p=residential_world(s,{x,z});return ground.at(p.x,p.y)-s.ground_m;
}
inline float residential_floor_top(std::size_t index,GroundSampler ground) {
    const auto& h=kResidentialHouses.at(index);float top=0;
    for(float x=-h.width_m*.5f;x<=h.width_m*.5f+.001f;x+=.5f)
        for(float z=-6;z<=6;z+=.5f)
            top=std::max(top,residential_height(h.site,ground,x,z));
    return top+.12f;
}
inline bool residential_ground_piece(const StartPart& p) {
    return p.name && (std::strcmp(p.name,"house interior floor")==0 ||
        std::strcmp(p.name,"house porch floor")==0 ||
        std::strcmp(p.name,"house front walk")==0 ||
        std::strcmp(p.name,"house side walk")==0 ||
        std::strcmp(p.name,"house driveway paving")==0);
}

inline std::vector<StartPart> bake_residential_house(std::size_t index,GroundSampler ground) {
    const auto& h=kResidentialHouses.at(index);const auto& site=h.site;
    const float floor=residential_floor_top(index,ground), hw=h.width_m*.5f;
    std::vector<StartPart> out;
    const auto add=[&](const char* name,float x,float z,float bottom,float w,float height,float d,
                       StartFinish finish,bool solid=false) {
        out.push_back({name,{x,z},bottom,w,height,d,finish,solid});
    };
    add("house brick foundation",0,0,0,h.width_m,floor-.10f,12,StartFinish::Brick,true);
    add("house interior floor",0,0,floor-.10f,h.width_m,.10f,12,StartFinish::WarmWall,true);
    // Street front has a physical recessed doorway and two framed windows.
    const BuildingOpening front[]={{"house living window",OpeningKind::Window,hw-3.6f,2.6f,.8f,1.6f,StartFinish::Glass,1,1,h.trim_finish},
        {"house front entrance",OpeningKind::Door,hw,1.65f,0,2.35f,h.trim_finish,0,0,h.trim_finish,!h.enterable},
        {"house kitchen window",OpeningKind::Window,hw+3.6f,2.6f,.8f,1.6f,StartFinish::Glass,1,1,h.trim_finish}};
    const BuildingOpening rear[]={{"house study window",OpeningKind::Window,hw-3.8f,2,.9f,1.45f,StartFinish::Glass,1,0,h.trim_finish},
        {"house bedroom window",OpeningKind::Window,hw+3.8f,2,.9f,1.45f,StartFinish::Glass,1,0,h.trim_finish}};
    const BuildingOpening side[]={{"house garden entrance",OpeningKind::Door,6,1.7f,0,2.35f,h.trim_finish,0,0,h.trim_finish,!h.enterable}};
    const BuildingWall walls[]={
        {"house front wall",{-hw,6},{hw,6},floor,h.wall_height_m,.24f,h.wall_finish,front,3},
        {"house rear wall",{-hw,-6},{hw,-6},floor,h.wall_height_m,.24f,h.wall_finish,rear,2},
        {"house garden wall",{-hw,-6},{-hw,6},floor,h.wall_height_m,.24f,h.wall_finish,side,1},
        {"house driveway wall",{hw,-6},{hw,6},floor,h.wall_height_m,.24f,h.wall_finish}};
    const BuildingRoof roof{"house pitched roof",{0,0},floor+h.wall_height_m,h.width_m,12,
        h.roof_rise_m,.18f,.55f,0,RoofStyle::Gable,
        index%2==0?RidgeAxis::AlongZ:RidgeAxis::AlongX,StartFinish::DarkRoof,h.wall_finish,
        .24f,h.wall_finish};
    auto shell=bake_building({site.name,walls,4,&roof,1});out.insert(out.end(),shell.begin(),shell.end());
    add("house interior ceiling",0,0,floor+h.wall_height_m-.12f,h.width_m-.3f,.10f,11.7f,StartFinish::White);
    add("house porch floor",0,7,floor-.10f,4,.10f,2,StartFinish::Concrete,true);
    add("house porch canopy",0,7,floor+2.65f,4.5f,.16f,2.8f,h.trim_finish);
    for(float x:{-1.85f,1.85f})add("house porch post",x,7.8f,floor,.14f,2.65f,.14f,h.trim_finish,true);
    add("house address plaque",1.25f,6.15f,floor+1.65f,.65f,.30f,.06f,h.trim_finish);
    add("house porch light lens",-1.15f,6.18f,floor+2.12f,.14f,.25f,.12f,StartFinish::Yellow);
    add("house chimney",hw-1.7f,-3,floor+h.wall_height_m,1,2.4f,1.05f,StartFinish::Brick);
    add("house chimney cap",hw-1.7f,-3,floor+h.wall_height_m+2.4f,1.2f,.12f,1.25f,StartFinish::Concrete);
    // One-metre paving strips follow measured terrain. Front walk eases from
    // the level porch to the sidewalk; narrow side path reaches the garden door.
    for(int step=0;step<10;++step) {
        const float z=8.5f+static_cast<float>(step),t=(z-8.f)/10.f;
        const float target=residential_height(site,ground,0,18.5f)+.12f;
        const float y=std::max(residential_height(site,ground,0,z)+.045f,
            floor*(1-t)+target*t);
        add("house front walk",0,z,y-.12f,1.8f,.12f,1.002f,StartFinish::Concrete);
    }
    for(int step=0;step<29;++step) {
        const float z=-10.5f+static_cast<float>(step);float y=0;
        for(float x:{8.4f,11.f,13.6f})for(float dz:{-.5f,0.f,.5f})
            y=std::max(y,residential_height(site,ground,x,z+dz));
        add("house driveway paving",11,z,y+.035f-.12f,5.2f,.12f,1.002f,StartFinish::Concrete);
    }
    for(int step=0;step<8;++step) {
        const float x=-hw-.5f-static_cast<float>(step),t=static_cast<float>(step)/7.f;
        const float y=std::max(residential_height(site,ground,x,0)+.045f,floor*(1-t)+
            (residential_height(site,ground,-hw-7.5f,0)+.05f)*t);
        add("house side walk",x,0,y-.12f,1.002f,.12f,1.8f,StartFinish::Concrete);
    }
    // Side carport varies with the roofline; the drive aisle stays 4.8m wide.
    const float carport=residential_height(site,ground,11,-6);
    if(index!=1u && index!=4u) {
        for(float x:{8.35f,13.65f})for(float z:{-9.f,-3.f}) {
            const float y=residential_height(site,ground,x,z);
            add("house carport post",x,z,y,.14f,3.0f,.14f,h.trim_finish,true);
        }
        add("house carport roof",11,-6,carport+3,5.8f,.18f,7.5f,StartFinish::DarkRoof);
    }
    // Garden boundary is rail-and-post, not a solid hedge hiding the yards.
    for(float x=-15;x<=15;x+=3) {
        const float y=residential_height(site,ground,x,-16.5f);
        add("house garden fence post",x,-16.5f,y,.12f,1.05f,.12f,StartFinish::WarmWall,true);
    }
    for(float x=-13.5f;x<=13.5f;x+=3) {
        const float y=residential_height(site,ground,x,-16.5f);
        for(float lift:{.40f,.85f})add("house garden fence rail",x,-16.5f,y+lift,3,.08f,.07f,StartFinish::WarmWall);
    }
    const float mail=residential_height(site,ground,-3,16.5f);
    add("house mailbox post",-3,16.5f,mail,.10f,1.1f,.10f,StartFinish::Steel,true);
    add("house mailbox",-3,16.5f,mail+1.1f,.42f,.35f,.65f,h.trim_finish);
    add("house rear garden bench",-3,-10,residential_height(site,ground,-3,-10)+.42f,2.3f,.12f,.65f,StartFinish::WarmWall,true);
    if(!h.enterable) return out;
    // Open-plan living/kitchen, with real 1.4–1.6m doors to study, bath and
    // bedroom. The side entrance connects the living room to the back yard.
    const BuildingOpening room_doors[]={
        {"house study doorway",OpeningKind::Door,4,1.6f,0,2.3f,h.trim_finish,0,0,StartFinish::White,false},
        {"house bath doorway",OpeningKind::Door,7,1.4f,0,2.3f,h.trim_finish,0,0,StartFinish::White,false},
        {"house bedroom doorway",OpeningKind::Door,10,1.6f,0,2.3f,h.trim_finish,0,0,StartFinish::White,false}};
    const BuildingWall inner[]={
        {"house interior room wall",{-7,-1},{7,-1},floor,2.9f,.14f,StartFinish::WarmWall,room_doors,3},
        {"house interior bath west wall",{-1.2f,-6},{-1.2f,-1},floor,2.9f,.14f,StartFinish::WarmWall},
        {"house interior bath east wall",{1.2f,-6},{1.2f,-1},floor,2.9f,.14f,StartFinish::WarmWall}};
    auto rooms=bake_building({"102 Sycamore interior",inner,3});out.insert(out.end(),rooms.begin(),rooms.end());
    add("house living sofa",-3.8f,4.5f,floor,3,.65f,.95f,StartFinish::TealDoor,true);
    add("house sofa back",-3.8f,4.94f,floor+.65f,3,.38f,.15f,StartFinish::TealDoor);
    add("house coffee table",-3.8f,2.4f,floor+.38f,1.6f,.12f,.85f,StartFinish::WarmWall,true);
    add("house living bookcase",-6.4f,2.8f,floor,.65f,1.75f,2.5f,StartFinish::WarmWall,true);
    for(float y:{.4f,.9f,1.4f})for(int book=0;book<8;++book)
        add("house book spine",-6.04f,1.8f+static_cast<float>(book)*.25f,floor+y,.06f,.28f,.17f,book%2==0?StartFinish::RedTrim:StartFinish::TealDoor);
    add("house kitchen cabinets",6.1f,1.7f,floor,1.4f,.92f,4.2f,StartFinish::WarmWall,true);
    add("house kitchen worktop",6.1f,1.7f,floor+.92f,1.5f,.08f,4.3f,StartFinish::Concrete);
    add("house kitchen sink",6.1f,.5f,floor+1,.95f,.03f,.65f,StartFinish::Steel);
    add("house kitchen stove",6.1f,2.5f,floor+1,1.1f,.03f,.9f,StartFinish::DarkRoof);
    add("house refrigerator",6.05f,4.7f,floor,1.25f,1.95f,1.05f,StartFinish::White,true);
    add("house dining table",3.2f,3.6f,floor+.72f,1.6f,.12f,1.5f,StartFinish::WarmWall,true);
    for(float z:{2.45f,4.75f}) {
        add("house dining chair seat",3.2f,z,floor+.43f,.55f,.10f,.55f,StartFinish::WarmWall,true);
        add("house dining chair back",3.2f,z+(z<3?-.24f:.24f),floor+.53f,.55f,.50f,.10f,StartFinish::WarmWall);
    }
    add("house study desk",-4.9f,-4.9f,floor+.72f,2.8f,.12f,1,StartFinish::WarmWall,true);
    add("house study desk cabinet",-5.85f,-4.9f,floor,.7f,.72f,.9f,StartFinish::WarmWall,true);
    add("house study lamp stem",-5.7f,-4.8f,floor+.84f,.08f,.40f,.08f,StartFinish::Steel);
    add("house study lamp shade",-5.7f,-4.8f,floor+1.24f,.35f,.20f,.35f,StartFinish::White);
    add("house study display cabinet",-6.35f,-2.8f,floor,.72f,1.5f,1.6f,StartFinish::WarmWall,true);
    add("house bedroom bed base",4.3f,-3.8f,floor,2.1f,.35f,2.8f,StartFinish::WarmWall,true);
    add("house bedroom mattress",4.3f,-3.8f,floor+.35f,2.1f,.22f,2.8f,StartFinish::White,true);
    add("house bedroom cover",4.3f,-3.3f,floor+.57f,2.12f,.07f,1.8f,StartFinish::TealDoor);
    add("house bedroom pillow",4.3f,-4.8f,floor+.57f,1.8f,.15f,.55f,StartFinish::White);
    add("house bedroom wardrobe",6.35f,-2.4f,floor,.75f,2.15f,1.9f,StartFinish::WarmWall,true);
    add("house bath tub",0,-5.05f,floor,1.85f,.50f,1.3f,StartFinish::White,true);
    add("house bath basin",-.65f,-2.7f,floor+.78f,.65f,.18f,.55f,StartFinish::White,true);
    add("house bath toilet",.6f,-3.55f,floor,.6f,.5f,.75f,StartFinish::White,true);
    // 102's detail layer stays non-solid: these are thin finishes or pieces
    // inside existing furniture footprints, never new obstacles in the aisles.
    const auto detail=[&](const char* name,float x,float z,float y,float w,float height,float d,
                          StartFinish finish=StartFinish::White) {
        add(name,x,z,floor+y,w,height,d,finish);
    };
    // Painted skirting and picture rails. Split every low run at real doors.
    for(float y:{.02f,2.72f}) {
        const float height=y<1.f?.16f:.065f;
        for(float x:{-3.92f,3.92f})detail("house painted trim",x,5.84f,y,5.92f,height,.055f);
        detail("house painted trim",0,-5.84f,y,13.7f,height,.055f);
        detail("house painted trim",6.84f,0,y,.055f,height,11.7f);
        for(float z:{-3.47f,3.47f})detail("house painted trim",-6.84f,z,y,.055f,height,4.72f);
        for(float z:{-.88f,-1.12f}) {
            for(const auto span:std::array<glm::vec2,4>{{{-5.4f,2.9f},{-1.85f,.55f},{1.85f,.55f},{5.4f,2.9f}}})
                detail("house painted trim",span.x,z,y,span.y,height,.045f);
        }
        for(float x:{-1.31f,-1.09f,1.09f,1.31f})
            detail("house painted trim",x,-3.5f,y,.045f,height,4.7f);
    }
    // Curtains frame glass without closing off the daylight or the view out.
    for(float z:{5.73f,-5.73f})for(float cx:{-3.7f,3.7f}) {
        detail("house curtain rod",cx,z,2.54f,2.95f,.045f,.06f,StartFinish::WarmWall);
        for(float curtain_side:{-1.f,1.f})for(int pleat=0;pleat<3;++pleat)
            detail("house curtain fold",cx+curtain_side*(1.08f+static_cast<float>(pleat)*.09f),
                z+(pleat%2==0?.02f:-.02f),.77f,.11f,1.75f,.065f,StartFinish::TealDoor);
        detail("house painted window sill",cx,z,.73f,2.95f,.075f,.25f);
    }
    detail("house living rug border",-3.8f,2.65f,.006f,4.15f,.012f,3.35f,StartFinish::RedTrim);
    detail("house living rug field",-3.8f,2.65f,.019f,3.82f,.008f,3.02f,StartFinish::TealDoor);
    for(float x:{-5.5f,-2.1f})detail("house living rug stripe",x,2.65f,.028f,.055f,.004f,2.85f,StartFinish::White);
    detail("house bedroom rug border",3.85f,-3.6f,.006f,3.5f,.012f,4.15f,StartFinish::RedTrim);
    detail("house bedroom rug field",3.85f,-3.6f,.019f,3.25f,.008f,3.9f,StartFinish::WarmWall);
    detail("house entry mat",0,5.25f,.008f,1.35f,.015f,.72f,StartFinish::DarkRoof);
    // Sofa cushions, arms and timber legs make the existing blocks read as furniture.
    for(float x:{-5.18f,-2.42f})detail("house sofa arm",x,4.5f,.55f,.24f,.33f,.96f,StartFinish::TealDoor);
    for(float x:{-4.68f,-3.8f,-2.92f})detail("house sofa seat cushion",x,4.43f,.65f,.82f,.12f,.70f,StartFinish::TealDoor);
    detail("house sofa scatter cushion",-4.7f,4.69f,.77f,.48f,.40f,.18f,StartFinish::RedTrim);
    detail("house sofa folded throw",-2.85f,4.28f,.78f,.50f,.035f,.42f,StartFinish::White);
    for(float x:{-4.42f,-3.18f})for(float z:{2.1f,2.7f})
        detail("house timber coffee leg",x,z,.03f,.07f,.35f,.07f,StartFinish::WarmWall);
    detail("house paper newspaper",-3.9f,2.4f,.505f,.67f,.015f,.44f);
    for(int line=0;line<5;++line)detail("house newspaper ink",-3.9f,2.26f+static_cast<float>(line)*.066f,.522f,.55f,.002f,.016f,StartFinish::DarkRoof);
    detail("house ceramic cup",-3.28f,2.55f,.50f,.12f,.16f,.12f);
    detail("house coffee surface",-3.28f,2.55f,.662f,.085f,.004f,.085f,StartFinish::DarkRoof);
    for(float y:{.35f,.85f,1.35f})detail("house timber bookcase shelf",-6.32f,2.8f,y,.72f,.055f,2.48f,StartFinish::WarmWall);
    // Painted cabinet fronts, enamel cooker, small kettle and crockery.
    for(float z:{.05f,1.15f,2.25f,3.35f}) {
        detail("house kitchen cabinet panel",5.384f,z,.15f,.028f,.65f,.99f,StartFinish::TealDoor);
        detail("house kitchen cabinet handle",5.35f,z+.29f,.66f,.055f,.045f,.16f,StartFinish::Steel);
    }
    detail("house kitchen tile backsplash",6.83f,1.7f,1.02f,.03f,.65f,4.3f);
    for(float z:{-.2f,.4f,1.f,1.6f,2.2f,2.8f,3.4f})
        detail("house kitchen tile seam",6.808f,z,1.04f,.01f,.60f,.013f,StartFinish::TealDoor);
    detail("house kitchen sink bowl",6.1f,.5f,1.035f,.70f,.012f,.42f,StartFinish::DarkRoof);
    detail("house kitchen tap upright",6.5f,.5f,1.04f,.06f,.28f,.06f,StartFinish::Steel);
    detail("house kitchen tap spout",6.37f,.5f,1.27f,.30f,.055f,.055f,StartFinish::Steel);
    for(float x:{5.82f,6.35f})for(float z:{2.25f,2.77f})
        detail("house kitchen burner",x,z,1.034f,.29f,.015f,.29f,StartFinish::Steel);
    detail("house kitchen enamel kettle",6.35f,2.77f,1.05f,.24f,.22f,.24f);
    detail("house kitchen kettle handle",6.35f,2.77f,1.27f,.19f,.12f,.035f,StartFinish::DarkRoof);
    detail("house kitchen bread board",6.f,1.46f,1.01f,.7f,.03f,.48f,StartFinish::WarmWall);
    detail("house kitchen bread loaf",6.f,1.46f,1.04f,.44f,.16f,.25f,StartFinish::WarmWall);
    detail("house refrigerator handle",5.38f,4.96f,.93f,.08f,.42f,.065f,StartFinish::Steel);
    detail("house refrigerator seam",5.417f,4.7f,1.46f,.02f,.018f,.98f,StartFinish::Steel);
    for(float x:{2.57f,3.83f})for(float z:{3.02f,4.18f})
        detail("house timber dining leg",x,z,0,.085f,.72f,.085f,StartFinish::WarmWall);
    detail("house dining cloth runner",3.2f,3.6f,.845f,.44f,.014f,1.49f,StartFinish::White);
    for(float z:{3.14f,4.06f}) {
        detail("house ceramic plate",3.2f,z,.863f,.35f,.025f,.32f);
        detail("house ceramic cup",3.66f,z,.84f,.12f,.16f,.12f);
    }
    for(float z:{2.45f,4.75f})for(float dx:{-.20f,.20f})for(float dz:{-.20f,.20f})
        detail("house timber chair leg",3.2f+dx,z+dz,0,.055f,.43f,.055f,StartFinish::WarmWall);
    // A paper-and-radio study, no modern screen. Details sit on the old desk.
    detail("house study writing blotter",-4.55f,-4.9f,.842f,1.15f,.012f,.64f,StartFinish::TealDoor);
    detail("house paper letter",-4.55f,-4.9f,.857f,.45f,.008f,.32f);
    detail("house timber desk leg",-3.7f,-4.9f,0,.085f,.72f,.8f,StartFinish::WarmWall);
    detail("house study radio case",-6.35f,-2.8f,1.5f,.6f,.43f,.80f,StartFinish::WarmWall);
    detail("house study radio grille",-6.04f,-2.9f,1.57f,.02f,.26f,.43f,StartFinish::DarkRoof);
    for(float z:{-3.05f,-2.95f,-2.85f,-2.75f})detail("house radio grille bar",-6.02f,z,1.57f,.015f,.26f,.02f,StartFinish::WarmWall);
    detail("house radio tuning dial",-6.02f,-2.51f,1.62f,.025f,.09f,.10f,StartFinish::White);
    detail("house study lamp base",-5.7f,-4.8f,.84f,.32f,.035f,.32f,StartFinish::Steel);
    detail("house study lamp light lens",-5.7f,-4.8f,1.23f,.30f,.015f,.30f,StartFinish::Yellow);
    // Bed joinery, folded linen and wardrobe door seams stay in their footprints.
    detail("house timber bed headboard",4.3f,-5.16f,.28f,2.1f,.91f,.10f,StartFinish::WarmWall);
    for(float x:{3.6f,4.3f,5.f})detail("house bed headboard inset",x,-5.10f,.63f,.52f,.41f,.02f,StartFinish::TealDoor);
    detail("house bedroom folded quilt",4.3f,-2.6f,.65f,2.0f,.08f,.43f,StartFinish::RedTrim);
    for(float z:{-2.86f,-1.94f}) {
        detail("house timber wardrobe panel",5.963f,z,.12f,.03f,1.9f,.82f,StartFinish::WarmWall);
        detail("house wardrobe handle",5.935f,z+(z<-2.4f?.32f:-.32f),1.f,.05f,.18f,.045f,StartFinish::Steel);
    }
    // Bath gets a tiled surface, visible tub well, fittings and towel.
    detail("house bath tile floor",0,-3.5f,.004f,2.1f,.012f,4.65f);
    for(int row=0;row<10;++row)for(int col=0;col<4;++col)if((row+col)%2==0)
        detail("house bath tile inset",-.78f+static_cast<float>(col)*.52f,-1.42f-static_cast<float>(row)*.45f,
            .017f,.49f,.003f,.42f,StartFinish::TealDoor);
    detail("house bath tub well",0,-5.05f,.502f,1.52f,.012f,.97f,StartFinish::PoolWater);
    detail("house bath basin well",-.65f,-2.7f,.965f,.46f,.008f,.36f,StartFinish::Steel);
    detail("house bath basin pedestal",-.65f,-2.7f,0,.20f,.78f,.24f);
    detail("house bath cistern",.6f,-3.85f,.36f,.59f,.49f,.20f);
    detail("house bath mirror frame",-1.088f,-2.7f,1.2f,.055f,.88f,.70f,StartFinish::WarmWall);
    detail("house bath mirror",-1.053f,-2.7f,1.26f,.015f,.76f,.58f,StartFinish::Steel);
    detail("house bath towel rail",1.07f,-2.0f,1.17f,.065f,.035f,.64f,StartFinish::Steel);
    detail("house bath folded towel",1.02f,-2.0f,.69f,.05f,.50f,.45f,StartFinish::RedTrim);
    // Framed landscape above the bookcase, on a solid wall (not the window).
    detail("house timber picture frame",-6.80f,2.8f,1.95f,.07f,.65f,1.1f,StartFinish::WarmWall);
    detail("house picture sky",-6.75f,2.8f,2.01f,.018f,.53f,.98f,StartFinish::White);
    detail("house picture hills",-6.733f,2.8f,2.01f,.018f,.23f,.98f,StartFinish::TealDoor);
    detail("house picture cottage",-6.717f,2.65f,2.08f,.018f,.14f,.19f,StartFinish::RedTrim);
    // Proper fixture housings at the existing warm light locations, plus bath.
    for(glm::vec2 p:std::array<glm::vec2,5>{{{-3,1},{3,1},{-4,-3},{4,-3},{0,-3.5f}}}) {
        detail("house ceiling light mount",p.x,p.y,2.96f,.24f,.06f,.24f,StartFinish::Steel);
        detail("house ceiling light rim",p.x,p.y,2.88f,.60f,.08f,.60f,StartFinish::White);
    }
    detail("house interior light lens",0,-3.5f,2.85f,.5f,.05f,.5f,StartFinish::Yellow);
    for(glm::vec2 p:std::array<glm::vec2,4>{{{-3,1},{3,1},{-4,-3},{4,-3}}})
        add("house interior light lens",p.x,p.y,floor+2.85f,.5f,.05f,.5f,StartFinish::Yellow);
    return out;
}
} // namespace apricot::city
