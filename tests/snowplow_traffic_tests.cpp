#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include "app/snowplow_service.h"
#include "city/map.h"
#include "city/spines.h"
#include "physics/terrain_collider.h"
#include "road/ribbon.h"
#include "terrain/heightmap.h"
#include "traffic/crowd.h"
#include "traffic_harness.h"
#include "test_assert.h"

using namespace apricot;
namespace {
using Identity = std::pair<uint64_t,uint32_t>;
CrowdTuning tuning() {
    CrowdTuning t;
    t.max_peds = 0;
    t.vehicle_activate_m = 400.0f;
    t.vehicle_retire_m = 500.0f;
    return t;
}
void tick(Crowd& crowd, int64_t step, glm::vec2 focus, const CrowdTuning& t) {
    if (step % t.refresh_every_steps == 0) crowd.refresh(step,focus);
    crowd.rebuild_buckets();
    crowd.step_vehicles(step);
}
void dispatch_and_determinism() {
    traffic_harness::Network a,b;
    traffic_harness::build_network(a,6,160.0f);
    // Spine reorder geometry is separately measured as non-identical by the
    // existing traffic suite. Test the supported exact spawn-order contract.
    traffic_harness::build_network(b,6,160.0f);
    CrowdTuning ta=tuning(),tb=ta;
    tb.reverse_scan_order=true;
    AmbientTuning ambient;
    ambient.vehicle_spacing_m=52.0f;
    Crowd first,second;
    first.build(a.lanes,traffic_harness::kMapSeed,ambient,ta);
    second.build(b.lanes,traffic_harness::kMapSeed,ambient,tb);
    const glm::vec2 focus{400,400};
    first.refresh(0,focus);second.refresh(0,focus);
    REQUIRE(!first.vehicles().empty());
    REQUIRE(first.snowplow_unit_count()==0);
    REQUIRE(first.population_hash()==second.population_hash());
    std::map<Identity,TrafficVehicleKind> initial;
    for (const auto& v:first.vehicles()) initial[{v.lane_key,v.slot}]=traffic_vehicle_kind(v);
    first.set_snowplow_service(true);second.set_snowplow_service(true);
    first.refresh(0,focus);second.refresh(0,focus);
    REQUIRE(first.snowplow_unit_count()>0);
    REQUIRE(first.snowplow_unit_count()<=kMaxSnowplowFleet);
    REQUIRE(first.population_hash()==second.population_hash());
    std::map<Identity,glm::vec3> previous;
    for (const auto& v:first.vehicles()) {
        if (v.snowplow_unit) {
            REQUIRE(traffic_vehicle_kind(v)==TrafficVehicleKind::Snowplow);
            REQUIRE(v.mode==AgentMode::Integrating);
            REQUIRE(!v.police_unit);
            VehicleAgent taken;
            REQUIRE(!first.take_vehicle(v.lane_key,v.slot,taken));
            previous[{v.lane_key,v.slot}]=v.pos;
        } else {
            const auto old=initial.find({v.lane_key,v.slot});
            if (old!=initial.end()) REQUIRE(old->second==traffic_vehicle_kind(v));
        }
    }
    float distance=0.0f;
    for (int64_t step=1;step<=2400;++step) {
        tick(first,step,focus,ta);tick(second,step,focus,tb);
        REQUIRE(first.population_hash()==second.population_hash());
        REQUIRE(first.snowplow_unit_count()<=kMaxSnowplowFleet);
        for (const auto& v:first.vehicles()) if (v.snowplow_unit) {
            REQUIRE(v.cruise_mps<=kSnowplowWorkSpeedMps);
            REQUIRE(std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.pos.z));
            const Identity id{v.lane_key,v.slot};
            const auto old=previous.find(id);
            if (old!=previous.end()) distance+=glm::distance(old->second,v.pos);
            previous[id]=v.pos;
        }
    }
    REQUIRE(distance>60.0f);
    const std::size_t active=first.snowplow_unit_count();
    first.set_snowplow_service(false);
    first.refresh(2400,focus);
    REQUIRE(first.snowplow_unit_count()==active);
    first.refresh(2400,{10000,10000});
    REQUIRE(first.snowplow_unit_count()==0);
    first.refresh(2400,focus);
    REQUIRE(first.snowplow_unit_count()==0);
    // The same streets can receive a fresh fleet in every later storm, even
    // though every departed agent identity is permanently retired by Crowd.
    for (int storm=0;storm<3;++storm) {
        first.set_snowplow_service(true);
        first.refresh(2400,focus);
        REQUIRE(first.snowplow_unit_count()>0);
        REQUIRE(first.snowplow_unit_count()<=kMaxSnowplowFleet);
        for (const auto& v:first.vehicles()) if (v.snowplow_unit)
            REQUIRE(previous.find({v.lane_key,v.slot})==previous.end());
        first.set_snowplow_service(false);
        first.refresh(2400,{10000,10000});
        REQUIRE(first.snowplow_unit_count()==0);
    }
    std::printf("snowplows: %.1f m aggregate travel over 20 s, reversed spawn scan hashes match\n",distance);
    apricot_test::pass("dry traffic stays intact; bounded snow fleet moves deterministically and never morphs");
}
void authored_dispatch() {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    LaneGraph lanes;
    roads.build(city::map_spines(),{},ground.sampler());
    lanes.build(roads,ground.sampler());
    const RoadCollision road_collision = build_road_collision(
        bake_ribbons(roads, ground.sampler()));
    TerrainCollider bare(city::kMapSeed), snowy(city::kMapSeed);
    bare.set_road_collision(road_collision);
    snowy.set_road_collision(road_collision);
    SnowClearanceField clearance;
    SnowplowService service;
    constexpr float snow_depth = 0.25f;
    snowy.set_snow_collision_depth(static_cast<float>(
        snowpack_collision_from_depth(snow_depth).depth_m));
    snowy.set_snow_clearance(&clearance, snow_depth);
    CrowdTuning t=tuning();
    AmbientTuning ambient;
    Crowd crowd;
    crowd.build(lanes,city::kMapSeed,ambient,t);
    crowd.set_snowplow_service(true);
    const glm::vec2 focus{950,200};
    crowd.refresh(0,focus);
    service.step(crowd.vehicles(), clearance, snow_depth);
    REQUIRE(crowd.snowplow_unit_count()>0);
    std::map<Identity,glm::vec3> previous;
    float distance=0;
    std::size_t moved=0;
    for (const auto& v:crowd.vehicles()) if (v.snowplow_unit) {
        previous[{v.lane_key,v.slot}]=v.pos;
        REQUIRE_NEAR(v.pos.y,lanes.pose(v.lane,v.dist_along_m).position.y,0.001f);
        const Lane& lane=lanes.lane(v.lane);
        REQUIRE(lane.cls!=RoadClass::Dirt && lane.cls!=RoadClass::Alley);
    }
    for (int64_t step=1;step<=3600;++step) {
        clearance.advance(1, 0, 1.0f / 120.0f, 0.25f);
        tick(crowd,step,focus,t);
        service.step(crowd.vehicles(), clearance, snow_depth);
        for (const auto& v:crowd.vehicles()) if (v.snowplow_unit) {
            const Identity id{v.lane_key,v.slot};
            auto old=previous.find(id);
            if (old!=previous.end()) {
                const float travel=glm::distance(old->second,v.pos);
                distance+=travel;
                if (travel>0.01f) ++moved;
            }
            previous[id]=v.pos;
        }
    }
    REQUIRE(distance>30.0f);
    REQUIRE(moved>100);
    REQUIRE(service.cleared_distance_m() > 30.0f);
    REQUIRE(clearance.strips().size() > 4);
    std::size_t cleared_road_contacts = 0, untouched_road_contacts = 0;
    for (const auto& strip : clearance.strips()) {
        const glm::vec3 centre = (strip.a + strip.b) * 0.5f;
        const auto base = bare.probe_down(centre + glm::vec3{0, 3, 0}, 4);
        REQUIRE(base.hit);
        // Service endpoints come from real lane poses. Pin their alignment
        // against actual baked road collision, not a hand-authored blade pose.
        REQUIRE(std::fabs(base.point.y - centre.y) < SnowClearanceField::kHeightTolerance);
        const float local = clearance.depth_at(centre.x, base.point.y,
                                                centre.z, snow_depth);
        REQUIRE(local < 0.02f);
        REQUIRE_NEAR(clearance.depth_at(centre.x, base.point.y + 8,
                                       centre.z, snow_depth), snow_depth, 1e-6);
        const auto cleared = snowy.probe_down(centre + glm::vec3{0, 3, 0}, 4);
        REQUIRE(cleared.hit);
        REQUIRE_NEAR(cleared.point.y, base.point.y, 1e-4);
        REQUIRE_NEAR(cleared.snow_depth_m, local, 1e-5);
        if (base.road && cleared.road) ++cleared_road_contacts;

        const glm::vec2 direction{strip.b.x - strip.a.x, strip.b.z - strip.a.z};
        if (glm::length(direction) < 0.1f) continue;
        const glm::vec2 right = glm::normalize(glm::vec2{-direction.y, direction.x});
        const glm::vec3 outside = centre + glm::vec3{right.x, 0, right.y} *
            (strip.half_width + 0.5f);
        const auto outside_base = bare.probe_down(outside + glm::vec3{0, 3, 0}, 4);
        if (!outside_base.hit || !outside_base.road ||
            clearance.depth_at(outside.x, outside_base.point.y, outside.z,
                               snow_depth) < snow_depth) continue;
        const auto outside_snow = snowy.probe_down(outside + glm::vec3{0, 3, 0}, 4);
        REQUIRE(outside_snow.hit && outside_snow.road);
        REQUIRE_NEAR(outside_snow.point.y, outside_base.point.y + 0.15f, 1e-4);
        REQUIRE_NEAR(outside_snow.snow_depth_m, snow_depth, 1e-6);
        ++untouched_road_contacts;
    }
    REQUIRE(cleared_road_contacts > 4);
    REQUIRE(untouched_road_contacts > 0);
    apricot_test::pass("real city fleet movement clears baked road contacts while adjacent road snow remains");
    std::printf("authored Pinatty snow fleet: %.1f m aggregate travel over 30 s, %zu units active\n",distance,crowd.snowplow_unit_count());
    apricot_test::pass("real city lanes dispatch moving snowplows on baked road elevation");
}
void blade_collision_footprint() {
    VehicleAgent a,b;
    a.snowplow_unit=true;
    a.fwd=b.fwd={0,0,-1};
    b.pos={0,0,-5.65f};
    a.speed_mps=3;
    const TrafficVehicleFootprint truck=traffic_vehicle_footprint(traffic_vehicle_kind(a));
    REQUIRE_NEAR(truck.half_width_m*2,kSnowplowBladeWidthM,0.001f);
    REQUIRE(truck.half_length_m>kSnowplowBladeForwardM);
    // The blade overlaps where the legacy 5 m body alone would miss contact.
    REQUIRE(resolve_traffic_body_collision(a,b,tuning()).collided);
    apricot_test::pass("snowplow blade participates in real traffic body collision");
}
}
int main() {
    dispatch_and_determinism();
    authored_dispatch();
    blade_collision_footprint();
    return 0;
}
