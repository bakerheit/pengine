#include "game/weapon_hit.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "traffic/crowd.h"
#include "road_fixture.h"
#include "test_assert.h"
#include <algorithm>
#include <limits>

using namespace apricot;

namespace {
constexpr uint64_t seed=0x50454421ull;
float elevated_ground(const void*,float,float) { return 1000.f; }

struct Fixture {
    RoadGraph roads;
    LaneGraph lanes;
    Crowd crowd;
    CrowdTuning tuning;
    explicit Fixture(bool reversed=false) {
        auto spines=make_grid_spines(4,62.f);
        if (reversed) std::reverse(spines.begin(),spines.end());
        const GroundSampler ground{elevated_ground,nullptr};
        roads.build(spines,RoadGraphParams{},ground);
        lanes.build(roads,ground);
        AmbientTuning ambient;
        ambient.max_vehicle_slots=0;
        tuning.ped_activate_m=400.f;
        tuning.ped_retire_m=600.f;
        crowd.build(lanes,seed,ambient,tuning);
        crowd.refresh(0,{93.f,93.f});
        crowd.rebuild_buckets();
        crowd.step_peds(0);
        REQUIRE(crowd.peds().size()>40);
    }
};

const PedAgent& find_ped(const Crowd& crowd,const PedShotHit& identity) {
    for (const PedAgent& ped:crowd.peds())
        if (ped.lane_key==identity.lane_key && ped.slot==identity.slot) return ped;
    REQUIRE(false);
    return crowd.peds().front();
}

struct ShotLine {
    glm::vec3 origin{},direction{};
    float range=0.f;
    PedShotHit first{};
    uint64_t rear_key=0;
    uint32_t rear_slot=0;
};

ShotLine choose_two_people(const Crowd& crowd) {
    for (const PedAgent& first:crowd.peds()) {
        for (const PedAgent& rear:crowd.peds()) {
            const glm::vec3 delta=rear.pos-first.pos;
            const float distance=glm::length(delta);
            if (distance<3.f || distance>50.f) continue;
            const glm::vec3 direction=delta/distance;
            const glm::vec3 origin=first.pos+glm::vec3{0.f,1.05f,0.f}-direction*2.f;
            const auto query=crowd.raycast_ped(origin,direction,distance+3.f);
            if (query.hit && query.lane_key==first.lane_key && query.slot==first.slot)
                return {origin,direction,distance+3.f,query,rear.lane_key,rear.slot};
        }
    }
    REQUIRE(false);
    return {};
}

void silhouette_math() {
    REQUIRE_NEAR(weapon_ped_hit_distance({0,1,-2},{0,0,1},{0,0,0},10.f),1.6f,1e-6);
    REQUIRE_NEAR(weapon_ped_hit_distance({.4f,1,-2},{0,0,1},{0,0,0},10.f),1.6f,1e-6);
    REQUIRE(weapon_ped_hit_distance({.401f,1,-2},{0,0,1},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1.86f,-2},{0,0,1},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,-.01f,-2},{0,0,1},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,-1},{0,0,0},10.f)<0.f);
    REQUIRE_NEAR(weapon_ped_hit_distance({0,1,0},{0,0,1},{0,0,0},10.f),0.f,1e-6);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,1},{0,0,0},1.6f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,0},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,2},{0,0,0},10.f)<0.f);
    REQUIRE(weapon_ped_hit_distance({0,1,-2},{0,0,1},{0,0,0},
            std::numeric_limits<float>::quiet_NaN())<0.f);
    apricot_test::pass("standing silhouette, parallel boundaries, range ties and invalid rays");
}

void nearest_hit_and_world_cover() {
    Fixture fixture;
    const ShotLine line=choose_two_people(fixture.crowd);
    const uint64_t before=fixture.crowd.population_hash();
    REQUIRE(fixture.crowd.raycast_ped(line.origin,line.direction,line.range).hit);
    REQUIRE(fixture.crowd.population_hash()==before);
    TerrainCollider world(seed);
    REQUIRE(!world.raycast(line.origin,line.direction,line.range).hit);
    const glm::vec3 centre=line.origin+line.direction*.8f;
    AABB wall;
    wall.expand(centre-glm::vec3{.2f});
    wall.expand(centre+glm::vec3{.2f});
    world.add_static_box(wall);
    const auto obstruction=world.raycast(line.origin,line.direction,line.range);
    REQUIRE(obstruction.hit && obstruction.prop);
    REQUIRE(obstruction.distance<line.first.distance);
    REQUIRE(!fixture.crowd.shoot_ped(line.origin,line.direction,obstruction.distance,1).hit);
    REQUIRE(fixture.crowd.population_hash()==before);
    REQUIRE(!fixture.crowd.shoot_ped(line.origin,line.direction,line.first.distance,1).hit);

    const auto hit=fixture.crowd.shoot_ped(line.origin,line.direction,line.range,1);
    REQUIRE(hit.hit && hit.lane_key==line.first.lane_key && hit.slot==line.first.slot);
    REQUIRE_NEAR(glm::length(hit.point-(line.origin+line.direction*hit.distance)),0.f,1e-5);
    std::size_t downed=0;
    for (const PedAgent& ped:fixture.crowd.peds()) {
        if (ped.activity==PedActivity::Downed) ++downed;
        if (ped.lane_key==line.rear_key && ped.slot==line.rear_slot)
            REQUIRE(!ped_is_floored(ped.activity));
    }
    REQUIRE(downed==1);
    const PedAgent& victim=find_ped(fixture.crowd,hit);
    REQUIRE(victim.activity_steps>=fixture.tuning.ped_life.downed_min_steps);
    REQUIRE(victim.activity_steps<=fixture.tuning.ped_life.downed_max_steps);
    REQUIRE(victim.speed_mps==0.f);
    REQUIRE(victim.impact_speed_mps>0.f);
    REQUIRE(victim.impact_from_bullet);
    REQUIRE_NEAR(glm::length(victim.impact_dir_xz),1.f,1e-6);
    const int64_t down_time=victim.activity_steps;
    REQUIRE(!fixture.crowd.shoot_ped(line.origin,line.direction,hit.distance+.01f,2).hit);
    REQUIRE(find_ped(fixture.crowd,hit).activity_steps==down_time);
    apricot_test::pass("real crowd nearest hit, real wall occlusion, one victim and no relaunch");
}

void deterministic_hit_and_recovery() {
    Fixture first,second(true);
    const ShotLine line=choose_two_people(first.crowd);
    const auto a=first.crowd.shoot_ped(line.origin,line.direction,line.range,1);
    const auto b=second.crowd.shoot_ped(line.origin,line.direction,line.range,1);
    REQUIRE(a.hit && b.hit && a.lane_key==b.lane_key && a.slot==b.slot);
    const glm::vec3 original=find_ped(first.crowd,a).pos;
    bool saw_rising=false,saw_recovered=false,saw_displaced=false;
    for (int64_t step=2;step<1500;++step) {
        first.crowd.rebuild_buckets();
        second.crowd.rebuild_buckets();
        first.crowd.step_peds(step);
        second.crowd.step_peds(step);
        const auto& x=find_ped(first.crowd,a);
        const auto& y=find_ped(second.crowd,b);
        REQUIRE(x.activity==y.activity && x.activity_steps==y.activity_steps);
        REQUIRE(x.impact_from_bullet && y.impact_from_bullet);
        REQUIRE_NEAR(glm::length(x.pos-y.pos),0.f,1e-5);
        REQUIRE_NEAR(glm::length(x.impact_velocity-y.impact_velocity),0.f,1e-5);
        if (x.activity==PedActivity::Downed) {
            REQUIRE(x.impact_offset.y<=.01f);
            REQUIRE(glm::length(glm::vec2{x.impact_offset.x,x.impact_offset.z})<.5f);
        }
        if (glm::length(x.pos-original)>.01f) saw_displaced=true;
        if (x.activity==PedActivity::Rising) {
            saw_rising=true;
            REQUIRE_NEAR(glm::length(x.impact_velocity),0.f,1e-6);
            const auto direct=first.crowd.raycast_ped(
                x.pos+glm::vec3{0.f,1.f,-.6f},{0,0,1},.3f);
            REQUIRE(!direct.hit || direct.lane_key!=a.lane_key || direct.slot!=a.slot);
        }
        if (saw_rising && !ped_is_floored(x.activity)) saw_recovered=true;
    }
    REQUIRE(saw_displaced && saw_rising && saw_recovered);
    VehicleState car;
    car.position=find_ped(first.crowd,a).pos;
    car.velocity={0.f,0.f,3.f};
    first.crowd.rebuild_buckets();
    first.crowd.step_peds(1500,&car);
    REQUIRE(find_ped(first.crowd,a).activity==PedActivity::Downed);
    REQUIRE(!find_ped(first.crowd,a).impact_from_bullet);
    apricot_test::pass("spawn-order-independent hit and existing downed/rising/recovery path");
}
} // namespace

int main() {
    silhouette_math();
    nearest_hit_and_world_cover();
    deterministic_hit_and_recovery();
    return apricot_test::done("weapon_hit_tests");
}
