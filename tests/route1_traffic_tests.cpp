#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "city/map.h"
#include "city/roads.h"
#include "city/spines.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
#include "terrain/heightmap.h"
#include "traffic/crowd.h"
#include "test_assert.h"

using namespace apricot;

namespace {

const city::Road& road_by_id(uint32_t id) {
    for (const city::Road& road : city::kRoads)
        if (road.id == id) return road;
    REQUIRE(false);
    return city::kRoads[0];
}

uint32_t spine_id(const RoadGraph& roads, const LaneGraph& lanes, LaneRef lane) {
    return roads.edge(lanes.lane(lane).edge).spine_id;
}

void halloway_ramps_use_the_right_side_of_route_one() {
    const city::Road& wb_entry = road_by_id(13);
    const city::Road& wb_exit = road_by_id(14);
    const city::Road& eb_entry = road_by_id(15);
    const city::Road& eb_exit = road_by_id(16);

    REQUIRE(std::strcmp(wb_entry.name, "Halloway westbound entry") == 0);
    REQUIRE(std::strcmp(wb_exit.name, "Halloway westbound exit") == 0);
    REQUIRE(std::strcmp(eb_entry.name, "Halloway eastbound entry") == 0);
    REQUIRE(std::strcmp(eb_exit.name, "Halloway eastbound exit") == 0);
    REQUIRE(wb_entry.path[wb_entry.count - 1].x < wb_entry.path[0].x);
    REQUIRE(wb_exit.path[wb_exit.count - 1].x < wb_exit.path[0].x);
    REQUIRE(eb_entry.path[eb_entry.count - 1].x > eb_entry.path[0].x);
    REQUIRE(eb_exit.path[eb_exit.count - 1].x > eb_exit.path[0].x);

    // Westbound traffic keeps north of the deck centreline; eastbound keeps
    // south. These world-space checks catch a label-only direction swap.
    REQUIRE(wb_entry.path[0].z < 490.0f);
    REQUIRE(wb_exit.path[wb_exit.count - 1].z < 490.0f);
    REQUIRE(eb_entry.path[0].z > 490.0f);
    REQUIRE(eb_exit.path[eb_exit.count - 1].z > 490.0f);

    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler());

    std::map<uint32_t, LaneRef> ramp_lane;
    for (uint32_t edge = 0; edge < roads.edge_count(); ++edge) {
        const uint32_t id = roads.edge(edge).spine_id;
        if (id < 13 || id > 16) continue;
        const std::vector<LaneRef> refs = lanes.lanes_of_edge(edge);
        REQUIRE(refs.size() == 1);
        ramp_lane[id] = refs.front();
    }
    REQUIRE(ramp_lane.size() == 4);

    for (const uint32_t entry_id : {13u, 15u}) {
        bool feeds_freeway = false;
        LaneRef auxiliary = kInvalidLane;
        for (const TurnLink& link : lanes.outgoing(ramp_lane.at(entry_id))) {
            feeds_freeway |= road_by_id(spine_id(roads, lanes, link.to)).cls ==
                             city::RoadClass::Freeway;
            if (road_by_id(spine_id(roads, lanes, link.to)).cls ==
                city::RoadClass::Freeway) auxiliary = link.to;
        }
        REQUIRE(feeds_freeway);
        REQUIRE(auxiliary != kInvalidLane);
        int feeders = 0;
        for (LaneRef from = 0; from < lanes.lane_count(); ++from)
            for (const TurnLink& link : lanes.outgoing(from))
                feeders += link.to == auxiliary ? 1 : 0;
        REQUIRE_MSG(feeders == 1,
                    "an entry ramp must exclusively feed its newborn auxiliary lane",
                    "Halloway ramp merge");
    }
    for (const uint32_t exit_id : {14u, 16u}) {
        bool fed_by_freeway = false;
        for (LaneRef from = 0; from < lanes.lane_count(); ++from) {
            for (const TurnLink& link : lanes.outgoing(from)) {
                if (link.to != ramp_lane.at(exit_id)) continue;
                fed_by_freeway |= road_by_id(spine_id(roads, lanes, from)).cls ==
                                  city::RoadClass::Freeway;
            }
        }
        REQUIRE(fed_by_freeway);
    }
    apricot_test::pass("Halloway ramps enter and exit Route 1 on the right");
}

struct FlowResult {
    std::size_t clears = 0;
    std::size_t local_collision_reactions = 0;
    int longest_local_stop_steps = 0;
};

FlowResult exercise_junction(const RoadGraph& roads, const LaneGraph& lanes,
                             glm::vec2 focus,
                             const std::set<uint32_t>* observed_spines = nullptr) {
    uint32_t target = 0;
    float nearest = 1.0e9f;
    for (uint32_t j = 0; j < lanes.junction_count(); ++j) {
        const glm::vec3 p = lanes.junction(j).pos;
        const float d = glm::distance(glm::vec2{p.x, p.z}, focus);
        if (d < nearest) {
            nearest = d;
            target = j;
        }
    }
    REQUIRE(nearest < 0.25f);

    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 34.0f;
    ambient.max_vehicle_slots = 32;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    Crowd crowd;
    crowd.build(lanes, city::kMapSeed, ambient, tuning);

    using Identity = std::pair<uint64_t, uint32_t>;
    std::map<Identity, LaneRef> previous_lane;
    std::map<Identity, int> stopped_steps;
    std::set<Identity> cleared;
    FlowResult result;
    constexpr int kSteps = 120 * 90;
    constexpr float kLocalRadiusM = 70.0f;

    for (int step = 0; step < kSteps; ++step) {
        if (step % tuning.refresh_every_steps == 0) crowd.refresh(step, focus);
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        for (const VehicleAgent& car : crowd.vehicles()) {
            const Identity id{car.lane_key, car.slot};
            const auto old = previous_lane.find(id);
            if (old != previous_lane.end() && old->second != car.lane &&
                lanes.valid(old->second) &&
                lanes.lane(old->second).junction_to == target)
                cleared.insert(id);
            previous_lane[id] = car.lane;

            const uint32_t current_spine =
                roads.edge(lanes.lane(car.lane).edge).spine_id;
            if (observed_spines &&
                observed_spines->count(current_spine) == 0u) {
                stopped_steps[id] = 0;
                continue;
            }

            const float distance = glm::distance(
                glm::vec2{car.pos.x, car.pos.z}, focus);
            if (distance > kLocalRadiusM) {
                stopped_steps[id] = 0;
                continue;
            }
            if (vehicle_engine_failed(car.mechanical)) {
                stopped_steps[id] = 0;
                continue;
            }
            if (car.speed_mps < 0.2f)
                ++stopped_steps[id];
            else
                stopped_steps[id] = 0;
            if (stopped_steps[id] > result.longest_local_stop_steps) {
                result.longest_local_stop_steps = stopped_steps[id];
            }
            if (car.collision_recovery_seconds > 0.0f ||
                glm::length(car.collision_offset_xz) > 0.04f ||
                glm::length(car.collision_velocity_xz) > 0.04f) {
                ++result.local_collision_reactions;
            }
        }
    }
    result.clears = cleared.size();
    return result;
}

void saltings_and_fishermans_keep_moving() {
    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler());

    const FlowResult saltings = exercise_junction(roads, lanes, {-872.0f, 248.0f});
    const FlowResult fisher = exercise_junction(roads, lanes,
                                                 {-803.51f, 289.31f});
    std::printf("  Saltings clears %zu, local reactions %zu, longest stop %.2f s\n",
                saltings.clears, saltings.local_collision_reactions,
                saltings.longest_local_stop_steps / 120.0);
    std::printf("  Fisherman's clears %zu, local reactions %zu, longest stop %.2f s\n",
                fisher.clears, fisher.local_collision_reactions,
                fisher.longest_local_stop_steps / 120.0);
    REQUIRE(saltings.clears >= 30);
    REQUIRE(fisher.clears >= 30);
    REQUIRE(saltings.local_collision_reactions == 0);
    REQUIRE(fisher.local_collision_reactions == 0);
    REQUIRE(saltings.longest_local_stop_steps < 15 * 120);
    REQUIRE(fisher.longest_local_stop_steps < 15 * 120);
    REQUIRE(traffic_vehicle_spacing_m(34.0f, 1.0f, 26.0f) >= 48.0f);
    apricot_test::pass("Saltings and Fisherman's clear without a local pileup");
}

void ostend_junctions_keep_moving() {
    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler());
    const FlowResult apron = exercise_junction(roads, lanes, {-1453.31f, -522.05f});
    const FlowResult berth = exercise_junction(roads, lanes, {-1950.0f, -600.0f});
    std::printf("  Apron clears %zu, local reactions %zu, longest stop %.2f s\n",
                apron.clears, apron.local_collision_reactions,
                apron.longest_local_stop_steps / 120.0);
    std::printf("  Berth 2 clears %zu, local reactions %zu, longest stop %.2f s\n",
                berth.clears, berth.local_collision_reactions,
                berth.longest_local_stop_steps / 120.0);
    REQUIRE(apron.clears >= 20);
    REQUIRE(berth.clears >= 10);
    REQUIRE(apron.local_collision_reactions == 0);
    REQUIRE(berth.local_collision_reactions == 0);
    REQUIRE(apron.longest_local_stop_steps < 25 * 120);
    REQUIRE(berth.longest_local_stop_steps < 15 * 120);
    apricot_test::pass("Apron Spine and Berth 2 clear without a local pileup");
}

void halloway_auxiliary_zones_keep_moving() {
    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler());
    const std::set<uint32_t> halloway_spines{
        11u, 13u, 14u, 15u, 16u, 18u, 19u, 29u, 43u, 44u, 45u, 46u};
    const FlowResult west = exercise_junction(
        roads, lanes, {-340.0f, 472.353f}, &halloway_spines);
    const FlowResult east = exercise_junction(
        roads, lanes, {220.0f, 506.957f}, &halloway_spines);
    std::printf("  Halloway west clears %zu, reactions %zu, longest stop %.2f s\n",
                west.clears, west.local_collision_reactions,
                west.longest_local_stop_steps / 120.0);
    std::printf("  Halloway east clears %zu, reactions %zu, longest stop %.2f s\n",
                east.clears, east.local_collision_reactions,
                east.longest_local_stop_steps / 120.0);
    REQUIRE(west.clears >= 20);
    REQUIRE(east.clears >= 20);
    REQUIRE(west.local_collision_reactions == 0);
    REQUIRE(east.local_collision_reactions == 0);
    REQUIRE(west.longest_local_stop_steps < 20 * 120);
    REQUIRE(east.longest_local_stop_steps < 20 * 120);
    apricot_test::pass("both Halloway auxiliary zones clear without a pileup");
}

void halloway_ramp_terminals_are_aligned_junctions() {
    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler());

    uint32_t north_ramp_terminal = UINT32_MAX;
    uint32_t south_ramp_terminal = UINT32_MAX;
    uint32_t ring_terminal = UINT32_MAX;
    for (uint32_t node = 0; node < roads.node_count(); ++node) {
        bool westbound_entry = false;
        bool westbound_exit = false;
        bool eastbound_entry = false;
        bool eastbound_exit = false;
        bool ring_west = false;
        bool ring_east = false;
        bool north_arm = false;
        for (const uint32_t edge : roads.node(node).edges) {
            const uint32_t spine = roads.edge(edge).spine_id;
            westbound_entry |= spine == 13u;
            westbound_exit |= spine == 14u;
            eastbound_entry |= spine == 15u;
            eastbound_exit |= spine == 16u;
            ring_west |= spine == 54u;
            ring_east |= spine == 55u;
            north_arm |= spine == 50u;
        }
        if (westbound_entry && westbound_exit && north_arm)
            north_ramp_terminal = node;
        if (eastbound_entry && eastbound_exit && north_arm)
            south_ramp_terminal = node;
        if (ring_west && ring_east && north_arm) ring_terminal = node;
    }
    REQUIRE(north_ramp_terminal != UINT32_MAX);
    REQUIRE(south_ramp_terminal != UINT32_MAX);
    REQUIRE(ring_terminal != UINT32_MAX);
    REQUIRE(roads.node(north_ramp_terminal).edges.size() == 4u);
    REQUIRE(roads.node(south_ramp_terminal).edges.size() == 4u);
    REQUIRE(roads.node(ring_terminal).edges.size() == 4u);
    REQUIRE(lanes.junction_control(north_ramp_terminal) == JunctionControl::Signal);
    REQUIRE(lanes.junction_control(south_ramp_terminal) == JunctionControl::Signal);
    REQUIRE(lanes.junction_control(ring_terminal) == JunctionControl::Signal);

    const glm::vec2 north_ramp = roads.node(north_ramp_terminal).pos;
    const glm::vec2 south_ramp = roads.node(south_ramp_terminal).pos;
    const glm::vec2 ring = roads.node(ring_terminal).pos;
    REQUIRE_NEAR(north_ramp.x, -45.714f, 0.01f);
    REQUIRE_NEAR(north_ramp.y, 440.0f, 0.01f);
    REQUIRE_NEAR(south_ramp.x, -46.207f, 0.01f);
    REQUIRE_NEAR(south_ramp.y, 550.0f, 0.01f);
    REQUIRE_NEAR(ring.x, -56.552f, 0.01f);
    REQUIRE_NEAR(ring.y, 650.0f, 0.01f);
    REQUIRE_MSG(glm::distance(north_ramp, south_ramp) >= 105.0f,
                "the paired ramp signals need clear spacing",
                "Halloway terminal spacing");
    REQUIRE_MSG(glm::distance(south_ramp, ring) >= 100.0f,
                "the ramp and Ring signals need a full city block between them",
                "Halloway terminal spacing");

    const city::Road& north_entry = road_by_id(13u);
    const city::Road& north_exit = road_by_id(14u);
    const glm::vec2 entry_run{
        north_entry.path[1].x - north_entry.path[0].x,
        north_entry.path[1].z - north_entry.path[0].z};
    const glm::vec2 exit_run{
        north_exit.path[north_exit.count - 1].x -
            north_exit.path[north_exit.count - 2].x,
        north_exit.path[north_exit.count - 1].z -
            north_exit.path[north_exit.count - 2].z};
    REQUIRE(glm::dot(glm::normalize(entry_run), glm::normalize(exit_run)) >
            0.999f);
    REQUIRE(std::fabs(entry_run.y) < 0.01f);
    REQUIRE(std::fabs(exit_run.y) < 0.01f);

    const std::set<uint32_t> north_spines{13u, 14u, 50u};
    const std::set<uint32_t> south_spines{15u, 16u, 50u};
    const FlowResult north_flow = exercise_junction(
        roads, lanes, north_ramp, &north_spines);
    const FlowResult south_flow = exercise_junction(
        roads, lanes, south_ramp, &south_spines);
    std::printf("  Halloway north ramp terminal clears %zu, reactions %zu, longest stop %.2f s\n",
                north_flow.clears, north_flow.local_collision_reactions,
                north_flow.longest_local_stop_steps / 120.0);
    std::printf("  Halloway south ramp terminal clears %zu, reactions %zu, longest stop %.2f s\n",
                south_flow.clears, south_flow.local_collision_reactions,
                south_flow.longest_local_stop_steps / 120.0);
    REQUIRE(north_flow.clears >= 20);
    REQUIRE(south_flow.clears >= 20);
    REQUIRE(north_flow.local_collision_reactions == 0);
    REQUIRE(south_flow.local_collision_reactions == 0);
    REQUIRE(north_flow.longest_local_stop_steps < 30 * 120);
    REQUIRE(south_flow.longest_local_stop_steps < 30 * 120);
    apricot_test::pass("both ramp pairs meet North Arm as aligned junctions");
}

void halloway_capture_population_stays_clear() {
    constexpr uint64_t kCaptureSeed = 905u;
    const TerrainGround ground{kCaptureSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler());
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 48.0f;
    ambient.max_vehicle_slots = 16;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    Crowd crowd;
    crowd.build(lanes, kCaptureSeed, ambient, tuning);

    VehicleState player;
    player.position = {-245.0f, 22.0f, 459.0f};
    const glm::vec2 focus{player.position.x, player.position.z};
    const std::set<uint32_t> halloway_spines{
        11u, 13u, 14u, 15u, 16u, 18u, 19u, 29u, 43u, 44u, 45u, 46u};
    std::size_t reaction_ticks = 0;
    std::size_t player_contacts = 0;
    using Identity = std::pair<uint64_t, uint32_t>;
    Identity tracked{};
    bool tracked_selected = false;
    float tracked_start_x = 0.0f;
    struct MergeSample {
        int step = 0;
        glm::vec3 pos{0.0f};
        glm::vec3 fwd{0.0f};
        float speed_mps = 0.0f;
        uint32_t spine = 0;
        bool turning = false;
    };
    constexpr std::array<int, 7> kSampleSteps{0, 60, 120, 180, 240, 300, 359};
    std::vector<MergeSample> merge_samples;
    constexpr int kSteps = 360;
    for (int step = 0; step < kSteps; ++step) {
        if (step % tuning.refresh_every_steps == 0) crowd.refresh(step, focus);
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, &player);
        player_contacts += crowd.resolve_player_collision(player) ? 1u : 0u;
        if (step == 0) {
            for (const VehicleAgent& car : crowd.vehicles()) {
                const uint32_t spine = roads.edge(lanes.lane(car.lane).edge).spine_id;
                if (spine != 13u || (tracked_selected && car.pos.x >= tracked_start_x))
                    continue;
                tracked = {car.lane_key, car.slot};
                tracked_start_x = car.pos.x;
                tracked_selected = true;
            }
        }
        for (const VehicleAgent& car : crowd.vehicles()) {
            const uint32_t spine = roads.edge(lanes.lane(car.lane).edge).spine_id;
            if (tracked_selected && Identity{car.lane_key, car.slot} == tracked &&
                std::find(kSampleSteps.begin(), kSampleSteps.end(), step) !=
                    kSampleSteps.end()) {
                merge_samples.push_back({step, car.pos, car.fwd, car.speed_mps, spine,
                    car.turn_from_lane != kInvalidLane});
            }
            if (halloway_spines.count(spine) == 0u) continue;
            if (car.collision_recovery_seconds > 0.0f ||
                glm::length(car.collision_offset_xz) > 0.04f ||
                glm::length(car.collision_velocity_xz) > 0.04f)
                ++reaction_ticks;
        }
    }
    std::printf("  Halloway capture seed: %zu traffic reaction ticks, %zu player contacts\n",
                reaction_ticks, player_contacts);
    REQUIRE(tracked_selected);
    REQUIRE(merge_samples.size() == kSampleSteps.size());
    bool crossed_to_freeway = false;
    for (const MergeSample& sample : merge_samples) {
        std::printf("    merge step %3d: spine %u at %.2f %.2f, speed %.2f, heading %.3f %.3f%s\n",
            sample.step, sample.spine, sample.pos.x, sample.pos.z, sample.speed_mps,
            sample.fwd.x, sample.fwd.z, sample.turning ? ", turning" : "");
        crossed_to_freeway |= sample.spine != 13u;
    }
    REQUIRE(crossed_to_freeway);
    REQUIRE(merge_samples.back().pos.x < merge_samples.front().pos.x - 25.0f);
    REQUIRE(merge_samples.back().fwd.x < -0.95f);
    REQUIRE(reaction_ticks == 0);
    REQUIRE(player_contacts == 0);
    apricot_test::pass("the daylight capture population stays in its lanes");
}

}  // namespace

int main() {
    halloway_ramps_use_the_right_side_of_route_one();
    halloway_auxiliary_zones_keep_moving();
    halloway_ramp_terminals_are_aligned_junctions();
    halloway_capture_population_stays_clear();
    saltings_and_fishermans_keep_moving();
    ostend_junctions_keep_moving();
    return apricot_test::done("route1_traffic_tests");
}
