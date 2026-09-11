#pragma once

// Shared rig for the lateral-maneuver suites (emergency_traffic_tests and
// civilian_maneuver_tests): one straight 600 m road of a chosen class, a Crowd
// with no ambient population, and a helper that seeds one car at a station.
// Agents are injected straight into the vehicle vector, so refresh() is never
// called and nothing but the seeded cars exists.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "core/fixed_step.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
#include "test_assert.h"
#include "traffic/crowd.h"

namespace maneuver_fixture {
using namespace apricot;

struct Fixture {
    RoadGraph roads;
    LaneGraph lanes;
    Crowd crowd;
    LaneRef lane = kInvalidLane;
    const TerrainCollider* world = nullptr;
    Fixture(RoadClass cls = RoadClass::Arterial, bool elevated = false) {
        RoadSpine road;
        road.id = 1; road.cls = cls; road.points = {{0,0}, {600,0}};
        GroundSampler ground;
        if (elevated) ground.fn = [](const void*, float, float) { return 1000.f; };
        roads.build({road}, {}, ground); lanes.build(roads, ground);
        lane = lanes.nearest_lane_along({200,2}, {1,0}).lane;
        REQUIRE(lanes.valid(lane));
        CrowdTuning tuning; tuning.max_peds = 0;
        tuning.police.patrol_fraction = 0;
        crowd.build(lanes, 905, {}, tuning);
    }
    VehicleAgent car(float station, uint32_t slot, bool police = false, LaneRef ref=kInvalidLane) {
        VehicleAgent v;
        v.lane = ref == kInvalidLane ? lane : ref;
        v.lane_key = lanes.lane(v.lane).key; v.slot = slot;
        v.dist_along_m = v.last_dist_m = station;
        v.speed_mps = v.cruise_mps = police ? 7.f : 5.f;
        v.mode = AgentMode::Integrating;
        v.police_unit = v.police_pursuit = police;
        const auto p = lanes.pose(v.lane, station); v.pos = p.position; v.fwd=p.tangent;
        return v;
    }
    auto& cars() { return const_cast<std::vector<VehicleAgent>&>(crowd.vehicles()); }
    // Every cruiser has the target in view: the police maneuvers under test
    // are contact moves, and since PENG-44 a unit that cannot see the
    // suspect searches instead of ramming.
    std::vector<VisiblePoliceIdentity> all_police() const {
        std::vector<VisiblePoliceIdentity> out;
        for (const auto& v : crowd.vehicles())
            if (v.police_unit) out.push_back({v.lane_key, v.slot});
        return out;
    }
    void tick(int64_t step, glm::vec2 target={400,2}, int wanted=1) {
        crowd.set_police_context(wanted, target, all_police());
        crowd.set_police_officer_context(false, false, {4,0}, world);
        crowd.rebuild_buckets(); crowd.step_vehicles(step);
    }
    // The ram is the one behaviour that needs the player's actual body in the
    // step, because its whole point is arriving inside it.
    void tick_with_player(int64_t step, const VehicleState& player, int wanted) {
        const glm::vec2 target{player.position.x, player.position.z};
        crowd.set_police_context(wanted, target, all_police());
        crowd.set_police_officer_context(false, false,
            {player.velocity.x, player.velocity.z}, world);
        crowd.rebuild_buckets(); crowd.step_vehicles(step, &player);
    }
};

}  // namespace maneuver_fixture
