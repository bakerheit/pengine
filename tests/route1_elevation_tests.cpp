#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include "city/map.h"
#include "city/roads.h"
#include "city/spines.h"
#include "core/fixed_step.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
#include "road/ribbon.h"
#include "traffic/crowd.h"
#include "test_assert.h"
using namespace apricot;

void hazards_respect_height() {
    RoadSpine road;
    road.id=1; road.points={{0,0},{0,1000}};
    RoadGraph graph;
    graph.build({road},{},{});
    LaneGraph lanes;
    lanes.build(graph,{});
    Crowd initial;
    initial.build(lanes,1234,AmbientTuning{},CrowdTuning{});
    initial.refresh(0,{0,500});
    const VehicleAgent* picked=nullptr;
    for(const auto& car:initial.vehicles())
        if(car.dist_along_m>100 && car.dist_along_m<800 && car.speed_mps>3) {picked=&car;break;}
    REQUIRE(picked!=nullptr);
    const auto key=picked->lane_key;
    const auto slot=picked->slot;
    VehicleState player;
    player.position=picked->pos+picked->fwd*8.0f;
    auto speed=[&](const Crowd& crowd) {
        for(const auto& car:crowd.vehicles()) if(car.lane_key==key && car.slot==slot) return car.speed_mps;
        return -1.0f;
    };
    Crowd control=initial, same=initial, above=initial, parked=initial, foot=initial;
    control.rebuild_buckets();control.step_vehicles(0);
    same.rebuild_buckets();same.step_vehicles(0,&player);
    REQUIRE(speed(same)<speed(control));
    player.position.y+=12;
    above.rebuild_buckets();above.step_vehicles(0,&player);
    REQUIRE(speed(above)==speed(control));
    parked.set_parked_vehicle_poses({player.position});
    parked.rebuild_buckets();parked.step_vehicles(0);
    REQUIRE(speed(parked)==speed(control));
    OnFootTrafficHazard pedestrian;
    pedestrian.position={player.position.x,player.position.z};
    pedestrian.height_m=player.position.y;
    foot.rebuild_buckets();foot.step_vehicles(0,nullptr,&pedestrian);
    REQUIRE(speed(foot)==speed(control));
}

int main() {
    hazards_respect_height();
    for(const auto& road:city::kRoads) {
        if(road.id==6 || road.id==12) REQUIRE(road.max_grade()<0.061f);
        if(road.one_way) REQUIRE(road.max_grade()<0.085f);
    }
    const TerrainGround ground{city::kMapSeed};
    RoadGraph graph;
    graph.build(city::map_spines(), {}, ground.sampler());
    LaneGraph lanes;
    lanes.build(graph, ground.sampler());
    const RibbonBake bake = bake_ribbons(graph, ground.sampler());
    const RoadCollision collision = build_road_collision(bake);
    TerrainCollider collider{city::kMapSeed};
    collider.set_road_collision(collision);
    const auto boxes = collider.static_boxes().size();
    REQUIRE(boxes > 20);
    collider.set_road_collision(collision);
    REQUIRE(collider.static_boxes().size() == boxes);
    for (glm::vec2 crossing : {glm::vec2{-40,490}, glm::vec2{401.5f,518.7935f}}) {
        const auto lower = collider.probe_down({crossing.x,16.0f,crossing.y},10.0f);
        const auto upper = collider.probe_down({crossing.x,23.0f,crossing.y},2.0f);
        REQUIRE(lower.hit && lower.road);
        REQUIRE(upper.hit && upper.road);
        REQUIRE_NEAR(upper.point.y,22.06f,0.01f);
        REQUIRE(upper.point.y-0.9f-lower.point.y > 6.0f);
        const auto ceiling = collider.raycast({crossing.x,16.0f,crossing.y},{0,1,0},10.0f);
        REQUIRE(ceiling.hit && ceiling.normal.y < -0.9f);
    }
    for (const RoadNode& node : graph.nodes()) {
        bool highway=false,street=false;
        for(uint32_t ei:node.edges) {
            const auto id=graph.edge(ei).spine_id;
            highway |= id==11; street |= id==50 || id==17;
        }
        REQUIRE(!(highway && street));
    }
    int ramps=0;
    for(uint32_t ei=0;ei<graph.edge_count();++ei) {
        const auto& edge=graph.edge(ei);
        if(edge.spine_id<13 || edge.spine_id>16) continue;
        ++ramps;
        const auto refs=lanes.lanes_of_edge(ei);
        REQUIRE(refs.size()==1);
        const LaneRef ref=refs.front();
        REQUIRE(lanes.opposing(ref)==kInvalidLane);
        REQUIRE(!lanes.outgoing(ref).empty());
        float worst=0.0f;
        for(float dist=1;dist<lanes.length(ref);dist+=1.0f) {
            const auto p=lanes.pose(ref,dist);
            const auto hit=collider.probe_down(p.position+glm::vec3{0,0.5f,0},2);
            worst=std::max(worst,std::fabs(hit.point.y-p.position.y));
            if(std::fabs(hit.point.y-p.position.y)>0.15f)
                std::printf(" mismatch d %.1f x %.1f z %.1f lane %.2f hit %.2f terrain %.2f\n",
                    static_cast<double>(dist),static_cast<double>(p.position.x),
                    static_cast<double>(p.position.z),static_cast<double>(p.position.y),
                    static_cast<double>(hit.point.y),static_cast<double>(collider.height(p.position.x,p.position.z)));
            REQUIRE(hit.hit);
        }
        std::printf("ramp %u lane/mesh worst %.3f m\n",edge.spine_id,static_cast<double>(worst));
        REQUIRE(worst<0.15f);
        VehicleTuning tuning;
        tuning.service_brake_grip_boost=1;
        const auto start=lanes.pose(ref,2);
        const float yaw=std::atan2(-start.tangent.x,-start.tangent.z);
        auto car=spawn_vehicle(tuning,collider,start.position.x,start.position.z,yaw);
        car.position.y=start.position.y+static_ride_height(tuning);
        // spawn_vehicle samples the terrain height field. Elevated road tests
        // must also replace its terrain tilt with the actual deck normal or a
        // steep embankment below the bridge starts the chassis crooked enough
        // for its suspension rays to miss the slab.
        const auto start_hit=collider.probe_down(start.position+glm::vec3{0,2,0},4);
        REQUIRE(start_hit.hit && start_hit.road);
        const glm::quat heading=glm::angleAxis(yaw,glm::vec3{0,1,0});
        const glm::vec3 tilt_axis=glm::cross(glm::vec3{0,1,0},start_hit.normal);
        const float tilt_sin=glm::length(tilt_axis);
        car.orientation=tilt_sin>1e-6f
            ? glm::angleAxis(std::atan2(tilt_sin,start_hit.normal.y),
                             tilt_axis/tilt_sin)*heading
            : heading;
        bool arrived=false;
        float min_support=100;
        glm::vec3 min_support_position{0.0f};
        float min_support_lane_dist=0.0f;
        for(int step=0;step<18000;++step) {
            const glm::vec2 here{car.position.x,car.position.z};
            const auto on=lanes.project_onto(ref,here);
            if(on.dist_along_m>lanes.length(ref)-4) {arrived=true;break;}
            const auto target=lanes.pose(ref,on.dist_along_m+10);
            const auto dir=glm::normalize(glm::vec2{target.position.x,target.position.z}-here);
            const auto f=vehicle_forward(car);
            const auto forward=glm::normalize(glm::vec2{f.x,f.z});
            const float angle=std::atan2(forward.x*dir.y-forward.y*dir.x,glm::dot(forward,dir));
            InputFrame input;
            input.steer=std::clamp(angle*1.8f,-1.0f,1.0f);
            const float speed=glm::length(car.velocity);
            input.throttle=speed<7?0.55f:0;
            input.brake=speed>8?0.3f:0;
            car=step_vehicle(car,tuning,input,collider,static_cast<float>(kSimDt));
            const float support =
                car.position.y - lanes.pose(ref,on.dist_along_m).position.y;
            if (support < min_support) {
                min_support = support;
                min_support_position = car.position;
                min_support_lane_dist = on.dist_along_m;
            }
        }
        std::printf("ramp %u drive %s; minimum body above road %.3f at lane %.1f "
                    "world %.1f,%.1f,%.1f\n", edge.spine_id,
                    arrived?"ARRIVED":"FAILED",static_cast<double>(min_support),
                    static_cast<double>(min_support_lane_dist),
                    static_cast<double>(min_support_position.x),
                    static_cast<double>(min_support_position.y),
                    static_cast<double>(min_support_position.z));
        REQUIRE(arrived);
        REQUIRE(min_support>0.1f);
    }
    REQUIRE(ramps==4);
    Crowd crowd;
    AmbientTuning ambient;
    ambient.vehicle_spacing_m=48;
    CrowdTuning tuning;
    tuning.vehicle_activate_m=650;
    crowd.build(lanes,city::kMapSeed,ambient,tuning);
    std::set<uint32_t> seen;
    for(int64_t step=0;step<2400;++step) {
        if(step%60==0) crowd.refresh(step,{-40,490});
        crowd.rebuild_buckets(); crowd.step_vehicles(step);
        for(const auto& car:crowd.vehicles()) {
            if(!lanes.valid(car.lane)) continue;
            const auto id=graph.edge(lanes.lane(car.lane).edge).spine_id;
            if(id==11 || (id>=13 && id<=16) || id==50) seen.insert(id);
            if(id==11) REQUIRE(car.pos.y>20);
        }
    }
    REQUIRE(seen.count(11)==1 && seen.count(50)==1);
    REQUIRE(seen.size()>=4);
    std::printf("live traffic exercised %zu deck/ramp/lower-road spines\n",seen.size());
    apricot_test::pass("Route 1 underpasses, directed ramps, physical driving and traffic");
}
