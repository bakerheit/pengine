#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include "city/map.h"
#include "city/spines.h"
#include "physics/vehicle.h"
#include "road/ribbon.h"
#include "test_assert.h"
using namespace apricot;
namespace {
float top_at(const RoadMesh& mesh,glm::vec2 point) {
    float top=-std::numeric_limits<float>::infinity();
    for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,
            c=mesh.vertices[mesh.indices[i+2]].position;
        const glm::dvec2 ab{b.x-a.x,b.z-a.z},ac{c.x-a.x,c.z-a.z},p{point.x-a.x,point.y-a.z};
        const auto cross=[](glm::dvec2 u,glm::dvec2 v){return u.x*v.y-u.y*v.x;};
        const double d=cross(ab,ac);if(std::fabs(d)<1e-8)continue;
        const double u=cross(p,ac)/d,v=cross(ab,p)/d;
        if(u>=-1e-6 &&v>=-1e-6 &&u+v<=1.000001)
            top=std::max(top,a.y+static_cast<float>(u)*(b.y-a.y)+static_cast<float>(v)*(c.y-a.y));
    }
    return top;
}
void retained_right_angle_and_ordinary_junctions() {
    for(RoadClass main:{RoadClass::Freeway,RoadClass::Arterial}) {
        const std::vector<RoadSpine> spines{
            {.points={{-100,0},{100,0}},.cls=main,.id=1},
            {.points={{0,0},{0,100}},.cls=RoadClass::Street,.id=2}};
        RoadGraph graph;graph.build(spines,{},{});const auto bake=bake_ribbons(graph,{});
        const float trim=road_class_def(main).carriageway_width_m*.5f+RibbonParams{}.junction_margin_m;
        float max=0;
        for(const auto& v:bake.layer(RoadLayer::Plate).vertices)
            max=std::max(max,std::max(std::fabs(v.position.x),std::fabs(v.position.z)));
        REQUIRE_NEAR(max,trim,.0001f);
        REQUIRE_NEAR(top_at(bake.layer(RoadLayer::Carriageway),{0,trim+.01f}),kDrapeEpsM,.0001f);
        REQUIRE_NEAR(top_at(bake.layer(RoadLayer::Plate),{0,trim-.01f}),kDrapeEpsM,.0001f);
    }
    apricot_test::pass("right-angle freeway and ordinary arterial T junctions retain original trim and joined asphalt");
}
void actual_freeway_surface(const RoadGraph& graph,const RibbonBake& bake,const TerrainCollider& collider) {
    const glm::vec2 origin{1450,300},along=glm::normalize(glm::vec2{250,-400}),across{-along.y,along.x};
    float maximum_step=0;
    for(float lane=-11;lane<=11;lane+=1) {
        float prev=0;bool have_previous=false;
        // Stay on the straight approach; nearer the node the freeway bends.
        for(float t=12;t<=70;t+=.25f) {
            const auto p=origin+along*t+across*lane;
            const auto hit=collider.probe_down({p.x,25,p.y},40);
            REQUIRE(hit.hit && hit.road);
            if(have_previous)maximum_step=std::max(maximum_step,std::fabs(hit.point.y-prev));
            prev=hit.point.y;have_previous=true;
        }
    }
    REQUIRE(maximum_step<.006f);
    const glm::vec2 formerly_overlapped{1451.65f,280.39f};
    REQUIRE(!std::isfinite(top_at(bake.layer(RoadLayer::Walk),formerly_overlapped)));
    const float asphalt=std::max(top_at(bake.layer(RoadLayer::Carriageway),formerly_overlapped),
        top_at(bake.layer(RoadLayer::Plate),formerly_overlapped));
    REQUIRE_NEAR(collider.probe_down({formerly_overlapped.x,25,formerly_overlapped.y},40).point.y,asphalt,.0001f);
    // The Kestrel approach still joins the junction and retains its sidewalk.
    const auto side=glm::normalize(glm::vec2{-50,-140});
    const glm::vec2 side_normal{-side.y,side.x};
    for(float t=3;t<=65;t+=.5f) {
        const auto p=origin+side*t;
        REQUIRE(collider.probe_down({p.x,25,p.y},40).road);
    }
    REQUIRE(std::isfinite(top_at(bake.layer(RoadLayer::Walk),origin+side*40.f+side_normal*8.5f)));
    // No distant miter or giant plate is allowed by the angle-aware trim.
    for(const auto& mesh:bake.layers)for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,
            c=mesh.vertices[mesh.indices[i+2]].position,p=(a+b+c)/3.f;
        if(glm::length(glm::vec2{p.x,p.z}-origin)>100.f)continue;
        REQUIRE(std::max({glm::length(a-b),glm::length(b-c),glm::length(c-a)})<90.f);
    }
    for(const auto& edge:graph.edges())if(edge.spine_id==7u || edge.spine_id==128u)
        REQUIRE(edge.length_m>100.f);
    std::printf("  Route1/Kestrel: max25cm sample step %.5fm; former12cm sidewalk overlap removed\n",maximum_step);
    apricot_test::pass("actual freeway lanes and Kestrel approach have continuous drawn/collision surfaces and retained sidewalks");
}
void fast_vehicle_passes(const TerrainCollider& collider) {
    const glm::vec2 origin{1450,300},along=glm::normalize(glm::vec2{250,-400}),across{-along.y,along.x};
    VehicleTuning tuning;tuning.service_brake_grip_boost=1;
    float max_vertical_speed=0;
    for(float lane:{-11.f,-9.f,-7.f,7.f,9.f,11.f})for(float direction:{-1.f,1.f}) {
        const auto start=origin+along*(direction<0?65.f:12.f)+across*lane;
        auto car=spawn_vehicle(tuning,collider,start.x,start.y,
            std::atan2(-along.x*direction,-along.y*direction));
        for(int tick=0;tick<340;++tick) {
            const float progress=glm::dot(glm::vec2{car.position.x,car.position.z}-origin,along);
            if((direction<0 && progress<12)||(direction>0 && progress>65))break;
            car.velocity.x=along.x*direction*25;car.velocity.z=along.y*direction*25;
            car=step_vehicle(car,tuning,{},collider,1.f/120.f);
            REQUIRE(car.impact_count==0u);
            REQUIRE(glm::dot(vehicle_up(car),glm::vec3{0,1,0})>.997f);
            for(const auto& wheel:car.wheels)REQUIRE(wheel.grounded);
            max_vertical_speed=std::max(max_vertical_speed,std::fabs(car.velocity.y));
        }
        const float progress=glm::dot(glm::vec2{car.position.x,car.position.z}-origin,along);
        REQUIRE(direction<0?progress<12:progress>65);
    }
    REQUIRE(max_vertical_speed<.4f);
    std::printf("  12 real25m/s lane passes: max vertical speed %.3fm/s; all tyres grounded\n",max_vertical_speed);
    apricot_test::pass("real high-speed suspension crosses all six tested lanes in both directions without curb kicks");
}
void crossing_bridge_keeps_both_sidewalk_levels() {
    const std::vector<RoadSpine> spines{
        {.points={{-50,0},{50,0}},.cls=RoadClass::Street,.id=1},
        {.points={{0,-50},{0,50}},.cls=RoadClass::Street,
         .structure=RoadStructure::Bridge,.deck_y_m=10,.id=2}};
    RoadGraph graph;graph.build(spines,{.split_crossings=false},{});
    const auto bake=bake_ribbons(graph,{});
    REQUIRE_NEAR(top_at(bake.layer(RoadLayer::Walk),{0,8.5f}),kDrapeEpsM+kKerbHeightM,.001f);
    REQUIRE_NEAR(top_at(bake.layer(RoadLayer::Walk),{8.5f,0}),10+kDrapeEpsM+kKerbHeightM,.001f);
    apricot_test::pass("grade-separated bridge and ground sidewalks remain intact where their footprints cross");
}

void sycamore_spine_sidewalk_overlap(const RibbonBake& bake,const TerrainCollider& collider) {
    // User screenshot: the closed Sycamore loop rejoins the Spine at (950,200).
    // Its almost-parallel return leg used to draw a long raised X over the lanes.
    for(float x:{942.f,946.f,950.f,954.f,958.f})for(float z=190;z<=325;z+=1.f) {
        const glm::vec2 p{x,z};
        REQUIRE(!std::isfinite(top_at(bake.layer(RoadLayer::Walk),p)));
        const float asphalt=std::max(top_at(bake.layer(RoadLayer::Carriageway),p),
                                     top_at(bake.layer(RoadLayer::Plate),p));
        REQUIRE(std::isfinite(asphalt));
        const auto hit=collider.probe_down({x,25,z},40);
        REQUIRE(hit.hit && hit.road);
        REQUIRE_NEAR(hit.point.y,asphalt,.001f);
    }
    // Keep the usable outer pavement, rather than deleting neighborhood walks.
    for(float z:{230.f,260.f,300.f})
        REQUIRE(std::isfinite(top_at(bake.layer(RoadLayer::Walk),{937.f,z})));
    VehicleTuning tuning;tuning.service_brake_grip_boost=1;
    for(float x:{943.f,947.f,953.f,957.f})for(float direction:{-1.f,1.f}) {
        auto car=spawn_vehicle(tuning,collider,x,direction>0?185.f:330.f,
                               direction>0?3.14159265359f:0.f);
        for(int tick=0;tick<900;++tick) {
            if(direction>0?car.position.z>330.f:car.position.z<185.f)break;
            car.velocity.x=0;car.velocity.z=direction*25.f;
            car=step_vehicle(car,tuning,{},collider,1.f/120.f);
            REQUIRE(car.impact_count==0u);
            for(const auto& wheel:car.wheels)REQUIRE(wheel.grounded);
            REQUIRE(std::fabs(car.velocity.y)<.4f);
        }
        REQUIRE(direction>0?car.position.z>330.f:car.position.z<185.f);
    }
    apricot_test::pass("actual Sycamore/Spine X removed from mesh and collision; outer walks retained and eight fast driving passes clear");
}

}
int main(){retained_right_angle_and_ordinary_junctions();crossing_bridge_keeps_both_sidewalk_levels();const TerrainGround ground{city::kMapSeed};RoadGraph graph;
    graph.build(city::map_spines(),{},ground.sampler());const auto bake=bake_ribbons(graph,ground.sampler());
    TerrainCollider collider(city::kMapSeed);collider.set_road_collision(build_road_collision(bake));
    actual_freeway_surface(graph,bake,collider);fast_vehicle_passes(collider);
    sycamore_spine_sidewalk_overlap(bake,collider);
    return apricot_test::done("sycamore_freeway_tests");}
