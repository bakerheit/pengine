#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/restaurant_menu_boards.h"
#include "city/start_area.h"
#include "test_assert.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "city/map.h"

using namespace apricot;

namespace {
struct V { float x,z; };
V operator+(V a,V b) { return {a.x+b.x,a.z+b.z}; }
V operator-(V a,V b) { return {a.x-b.x,a.z-b.z}; }
V operator*(V a,float b) { return {a.x*b,a.z*b}; }
float dot(V a,V b) { return a.x*b.x+a.z*b.z; }
struct Rect { V centre,side,forward; float half_width,half_length; };
Rect part_rect(const city::StartPart& p) {
    const float a=p.yaw_deg*3.14159265359f/180.f;
    return {{p.centre.x,p.centre.z},{std::cos(a),-std::sin(a)},
            {std::sin(a),std::cos(a)},p.width_m*.5f,p.depth_m*.5f};
}
bool overlap(const Rect& a,const Rect& b) {
    for (const V axis : {a.side,a.forward,b.side,b.forward}) {
        const float ra=std::fabs(dot(a.side,axis))*a.half_width+
                       std::fabs(dot(a.forward,axis))*a.half_length;
        const float rb=std::fabs(dot(b.side,axis))*b.half_width+
                       std::fabs(dot(b.forward,axis))*b.half_length;
        if (std::fabs(dot(a.centre-b.centre,axis))>=ra+rb) return false;
    }
    return true;
}
const city::StartPart& named(const std::vector<city::StartPart>& parts,const char* name) {
    for(const auto& p:parts) if(std::strcmp(p.name,name)==0) return p;
    REQUIRE_MSG(false,"missing Quickbite component",name);
    return parts.front();
}
void clear_pose(const std::vector<city::StartPart>& parts,V centre,float heading) {
    // A 5.4 x 2.2m passenger car plus 30cm clearance on every side. Sample
    // rectangles, not centreline points, so corner overhang is tested too.
    const Rect car{centre,{std::cos(heading),-std::sin(heading)},
                   {std::sin(heading),std::cos(heading)},1.4f,3.0f};
    for(const auto& p:parts) {
        if(!p.solid || p.bottom_m>=2.35f) continue;
        REQUIRE_MSG(!overlap(car,part_rect(p)),"drive-through swept car hits a solid",p.name);
    }
    if (centre.z>=-15.9f) {
        for(const float sx : {-1.f,1.f}) for(const float sz : {-1.f,1.f}) {
            const V corner=car.centre+car.side*(sx*car.half_width)+car.forward*(sz*car.half_length);
            REQUIRE(corner.x>=-32.f && corner.x<=32.f);
            REQUIRE(corner.z<=19.f);
        }
    }
}
glm::vec3 site_point(V p,float y) {
    const auto& site=city::kFastFoodSite;
    return {site.origin.x+site.cos_yaw*p.x+site.sin_yaw*p.z,y,
            site.origin.z-site.sin_yaw*p.x+site.cos_yaw*p.z};
}
TerrainCollider quickbite_collider(const std::vector<city::StartPart>& parts) {
    TerrainCollider collider(city::kMapSeed);
    const auto& site=city::kFastFoodSite;
    for(const auto& p:parts) if(p.solid) {
        REQUIRE(p.pitch_deg==0.f && p.roll_deg==0.f);
        collider.add_static_oriented_box(site_point({p.centre.x,p.centre.z},
            site.ground_m+p.bottom_m+p.height_m*.5f),
            {p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},
            std::atan2(site.sin_yaw,site.cos_yaw)+p.yaw_deg*3.14159265359f/180.f);
    }
    return collider;
}
void actual_vehicle_collision_matches_visible_walls(const std::vector<city::StartPart>& parts) {
    const auto collider=quickbite_collider(parts);
    const VehicleTuning tuning;
    std::size_t checked=0;
    float worst_push=0.f;
    for(float x=-22.f;x<=10.f;x+=.5f) for(float z=-10.f;z<=-6.f;z+=.25f) {
        const auto p=site_point({x,z},0.f);
        auto car=spawn_vehicle(tuning,collider,p.x,p.z,0.f);
        bool clear=true;
        for(const auto& box:collider.static_boxes()) {
            if(box.bounds.max.y<=car.position.y+tuning.chassis_floor ||
               box.bounds.min.y>=car.position.y+tuning.chassis_roof) continue;
            const auto local=box.local_point(car.position);
            const auto bounds=box.collision_bounds();
            const float dx=local.x-std::clamp(local.x,bounds.min.x,bounds.max.x);
            const float dz=local.z-std::clamp(local.z,bounds.min.z,bounds.max.z);
            if(dx*dx+dz*dz<(tuning.chassis_collision_radius+.02f)*(tuning.chassis_collision_radius+.02f)) clear=false;
        }
        if(!clear) continue;
        const auto next=step_vehicle(car,tuning,{},collider,1.f/120.f);
        const float push=glm::length(glm::vec2{next.position.x-car.position.x,next.position.z-car.position.z});
        if(push>worst_push) {
            worst_push=push;
            if(push>.01f) std::printf("Quickbite false collision at local %.2f %.2f: %.3fm push\n",x,z,push);
        }
        ++checked;
    }
    REQUIRE(checked>100u);
    REQUIRE_MSG(worst_push<.01f,"vehicle hits a rotated wall's invisible bounding-box corner","Quickbite actual collider");
    std::printf("  Quickbite collider: %zu clear forecourt poses, max push %.5fm\n",checked,worst_push);
    // Exercise the real vehicle collision path around both drive-through
    // legs and the return arc, not only the local rectangle sweep above.
    const auto drive_pose=[&](V local,float heading) {
        const auto p=site_point(local,0.f);
        auto car=spawn_vehicle(tuning,collider,p.x,p.z,heading+
            std::atan2(city::kFastFoodSite.sin_yaw,city::kFastFoodSite.cos_yaw));
        car.velocity=vehicle_forward(car)*5.f;
        const auto next=step_vehicle(car,tuning,{},collider,1.f/120.f);
        REQUIRE(next.impact_count==0u);
        REQUIRE(glm::length(glm::vec2{next.position.x-car.position.x,next.position.z-car.position.z})<.05f);
    };
    for(int i=0;i<=128;++i) {
        const float z=-24.f+static_cast<float>(i)*.25f;
        drive_pose({city::kQuickbiteEntryLaneX,z},3.14159265359f);
        drive_pose({city::kQuickbitePickupLaneX,z},0.f);
    }
    for(int i=0;i<=180;++i) {
        const float a=static_cast<float>(i)*3.14159265359f/180.f;
        drive_pose({city::kQuickbiteTurnCentre.x+city::kQuickbiteTurnRadiusM*std::cos(a),
                    city::kQuickbiteTurnCentre.z+city::kQuickbiteTurnRadiusM*std::sin(a)},a+3.14159265359f);
    }
    // A real rotated wall must still stop a car, while an 18cm kerb under
    // its floor stays a suspension contact rather than a horizontal barrier.
    const auto wall_contact=[&](float height) {
        TerrainCollider test(city::kMapSeed);
        const float yaw=std::atan2(city::kFastFoodSite.sin_yaw,city::kFastFoodSite.cos_yaw);
        const glm::vec3 normal{std::sin(yaw),0.f,std::cos(yaw)};
        test.add_static_oriented_box({0.f,12.f+height*.5f,0.f},{10.f,height*.5f,.1f},yaw);
        const auto start=normal*(tuning.chassis_collision_radius+.08f);
        auto car=spawn_vehicle(tuning,test,start.x,start.z,yaw);
        car.velocity=-normal*6.f;
        const auto next=step_vehicle(car,tuning,{},test,1.f/120.f);
        return next;
    };
    REQUIRE(wall_contact(3.f).impact_count==1u);
    REQUIRE(wall_contact(.18f).impact_count==0u);
}
void clear_character_path(const std::vector<city::StartPart>& parts,V a,V b) {
    for(int step=0;step<=100;++step) {
        const float t=static_cast<float>(step)/100.f;
        const V centre=a+(b-a)*t;
        // 80cm body envelope plus15cm clearance each side, checked against
        // the same oriented pieces that produce runtime obstacles.
        const Rect walker{centre,{1,0},{0,1},.55f,.55f};
        for(const auto& p:parts) {
            if(!p.solid || p.bottom_m+p.height_m<=.205f || p.bottom_m>=2.05f) continue;
            REQUIRE_MSG(!overlap(walker,part_rect(p)),"interior character route obstructed",p.name);
        }
    }
}
}

int main() {
    const auto parts=city::bake_building(city::kFastFoodPlan);
    actual_vehicle_collision_matches_visible_walls(parts);
    REQUIRE(city::kQuickbiteTurnRadiusM>=6.5f);
    REQUIRE_NEAR(city::kQuickbiteEntryLaneX-city::kQuickbitePickupLaneX,
                 2*city::kQuickbiteTurnRadiusM,1e-5f);
    REQUIRE(city::kQuickbiteOrderZ>city::kQuickbitePickupZ+5.f);
    for(int i=0;i<=128;++i) {
        const float z=-24.f+static_cast<float>(i)*.25f;
        clear_pose(parts,{city::kQuickbiteEntryLaneX,z},0.f);
        clear_pose(parts,{city::kQuickbitePickupLaneX,z},3.14159265359f);
    }
    for(int i=0;i<=180;++i) {
        const float a=static_cast<float>(i)*3.14159265359f/180.f;
        clear_pose(parts,{city::kQuickbiteTurnCentre.x+city::kQuickbiteTurnRadiusM*std::cos(a),
                          city::kQuickbiteTurnCentre.z+city::kQuickbiteTurnRadiusM*std::sin(a)},-a);
    }
    // The separate parking mouth remains usable despite the roadside pylon.
    for(int i=0;i<=56;++i) clear_pose(parts,{-25.f,-24.f+static_cast<float>(i)*.25f},0.f);
    // A real quarter-circle from the parking mouth into the 6.2m aisle,
    // followed by the whole parking-row approach, including front overhang.
    for(int i=0;i<=90;++i) {
        const float a=static_cast<float>(i)*3.14159265359f/180.f;
        clear_pose(parts,{-19.5f-5.5f*std::cos(a),-16.3f+5.5f*std::sin(a)},a);
    }
    for(int i=0;i<=100;++i)
        clear_pose(parts,{-19.5f+static_cast<float>(i)*.25f,-10.8f},3.14159265359f*.5f);
    const auto& menu=named(parts,"quickbite drive-through menu sign face");
    const auto& brand=named(parts,"quickbite brand sign face");
    const auto& road=named(parts,"quickbite road sign face");
    REQUIRE_NEAR(menu.width_m,city::kDriveThroughMenuBoard.width_m,1e-5f);
    REQUIRE_NEAR(menu.height_m,city::kDriveThroughMenuBoard.height_m,1e-5f);
    REQUIRE_NEAR(menu.width_m/menu.height_m,
                 static_cast<float>(city::kDriveThroughMenuBoard.canvas_width_px)/
                     static_cast<float>(city::kDriveThroughMenuBoard.canvas_height_px),
                 1e-5f);
    const auto& drive_backing=named(parts,"drive through menu board");
    REQUIRE_NEAR(drive_backing.width_m,
                 city::menu_board_backing_width_m(city::kDriveThroughMenuBoard),1e-5f);
    REQUIRE_NEAR(drive_backing.height_m,
                 city::menu_board_backing_height_m(city::kDriveThroughMenuBoard),1e-5f);
    REQUIRE_NEAR(menu.yaw_deg,90.f,1e-5f);
    REQUIRE_NEAR(brand.width_m/brand.height_m,2.25f,1e-5f);
    REQUIRE_NEAR(road.width_m/road.height_m,2.25f,1e-5f);
    // The roof baker places a parapet centred on the overhang edge. Its
    // thickness extends farther forward than the nominal roof dimensions.
    // Test the cooked pieces: the old sign sat behind that final front edge.
    float roof_front=0.f;
    for(const auto& p:parts) if(std::strcmp(p.name,"restaurant flat roof")==0)
        roof_front=std::min(roof_front,p.centre.z-p.depth_m*.5f);
    REQUIRE(brand.centre.z+brand.depth_m*.5f<roof_front-.20f);
    const auto& backing=named(parts,"Quickbite front sign");
    REQUIRE(brand.centre.z<backing.centre.z-backing.depth_m*.5f);
    const auto& awning=named(parts,"restaurant entrance awning");
    REQUIRE(backing.bottom_m>=awning.bottom_m+awning.height_m);
    REQUIRE_NEAR(brand.yaw_deg,180.f,1e-5f);
    REQUIRE_NEAR(road.yaw_deg,180.f,1e-5f);
    REQUIRE(menu.centre.z>named(parts,"drive through window").centre.z);
    // Pedestrians have a full 2.5m axis from the public sidewalk to the door,
    // outside both drive-through legs and continuing through the open entrance.
    const Rect walk{{-5.f,-14.4f},{1,0},{0,1},1.25f,6.6f};
    for(const auto& p:parts) if(p.solid && p.bottom_m<2.1f)
        REQUIRE_MSG(!overlap(walk,part_rect(p)),"solid blocks restaurant walk",p.name);
    std::size_t arrows=0,curve=0;
    for(const auto& p:parts) {
        const bool paint=std::strstr(p.name,"arrow") || std::strstr(p.name,"stripe") ||
                         std::strstr(p.name,"return outer edge") || std::strstr(p.name,"return inner edge");
        if(paint && p.height_m<.05f) { REQUIRE(!p.solid); REQUIRE(p.bottom_m+p.height_m<.14f); }
        arrows+=std::strstr(p.name,"arrow stem")!=nullptr;
        curve+=std::strstr(p.name,"return outer edge")!=nullptr;
    }
    REQUIRE(!city::kFastFoodFrontOpenings[1].leaf);
    const auto& floor=named(parts,"quickbite interior floor");
    const auto& ceiling=named(parts,"quickbite interior ceiling");
    REQUIRE_NEAR(floor.bottom_m+floor.height_m,.20f,1e-5f);
    REQUIRE(named(parts,"restaurant foundation").bottom_m+
            named(parts,"restaurant foundation").height_m<floor.bottom_m+floor.height_m);
    REQUIRE(ceiling.bottom_m-(floor.bottom_m+floor.height_m)>3.0f);
    clear_character_path(parts,{-5.f,-7.f},{-5.f,3.9f});
    clear_character_path(parts,{-5.f,0.f},{-12.8f,0.f});
    clear_character_path(parts,{-12.8f,-2.8f},{-12.8f,6.0f});
    clear_character_path(parts,{-5.f,1.6f},{4.0f,1.6f});
    clear_character_path(parts,{-5.f,3.4f},{7.3f,3.4f});
    clear_character_path(parts,{7.3f,3.0f},{7.3f,8.15f});
    clear_character_path(parts,{7.3f,8.15f},{-16.7f,8.15f});
    std::size_t booths=0,tables=0,interior_menus=0,interior_backings=0,menu_hangers=0;
    for(const auto& p:parts) {
        booths+=std::strcmp(p.name,"quickbite interior booth seat")==0;
        tables+=std::strcmp(p.name,"quickbite interior table top")==0;
        if(std::strcmp(p.name,"quickbite interior menu sign face left")==0 ||
           std::strcmp(p.name,"quickbite interior menu sign face right")==0) {
            ++interior_menus;
            REQUIRE_NEAR(p.width_m,city::kInteriorMenuBoard.width_m,1e-5f);
            REQUIRE_NEAR(p.height_m,city::kInteriorMenuBoard.height_m,1e-5f);
            REQUIRE_NEAR(p.width_m/p.height_m,
                         static_cast<float>(city::kInteriorMenuBoard.canvas_width_px)/
                             static_cast<float>(city::kInteriorMenuBoard.canvas_height_px),
                         1e-5f);
            REQUIRE_NEAR(p.yaw_deg,180.f,1e-5f);
        }
        if(std::strcmp(p.name,"quickbite interior menu backing")==0) {
            ++interior_backings;
            REQUIRE_NEAR(p.width_m,city::menu_board_backing_width_m(
                             city::kInteriorMenuBoard),1e-5f);
            REQUIRE_NEAR(p.height_m,city::menu_board_backing_height_m(
                             city::kInteriorMenuBoard),1e-5f);
        }
        menu_hangers+=std::strcmp(p.name,"quickbite interior menu hanger")==0;
    }
    const auto& left_menu=named(parts,"quickbite interior menu sign face left");
    const auto& right_menu=named(parts,"quickbite interior menu sign face right");
    // The diner-facing view reverses this local X axis: greater X is screen-left.
    REQUIRE(left_menu.centre.x>right_menu.centre.x);
    REQUIRE(booths==6u && tables==5u && interior_menus==2u &&
            interior_backings==2u && menu_hangers==0u);
    REQUIRE(arrows==3u);
    REQUIRE(curve>=12u);
    return apricot_test::done("quickbite_tests");
}
