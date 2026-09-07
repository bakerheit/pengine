// Production-path police traffic contracts: ambient patrol identity is stable,
// fallback response is fixed-step throttled, and engaged cruisers follow real
// right-side lane links toward the wanted target.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "road_fixture.h"
#include "terrain/heightmap.h"
#include "test_assert.h"
#include "traffic/ambient.h"
#include "traffic/crowd.h"

using namespace apricot;
using apricot_test::pass;

namespace {

struct Network {
    RoadGraph roads;
    LaneGraph lanes;
};

Network build_network() {
    Network network;
    network.roads.build(make_grid_spines(12, 90.0f), RoadGraphParams{},
                        GroundSampler{});
    LaneBuildParams lane_params;
    lane_params.drive_on_right = true;
    network.lanes.build(network.roads, GroundSampler{}, lane_params);
    return network;
}

CrowdTuning police_tuning(bool reverse_scan) {
    CrowdTuning tuning;
    tuning.vehicle_activate_m = 260.0f;
    tuning.vehicle_retire_m = 340.0f;
    tuning.max_peds = 0;
    tuning.reverse_scan_order = reverse_scan;
    tuning.police.patrol_fraction = 0.12f;
    return tuning;
}

using Identity = std::pair<uint64_t, uint32_t>;

std::map<Identity, bool> police_roles(const Crowd& crowd) {
    std::map<Identity, bool> roles;
    for (const VehicleAgent& agent : crowd.vehicles()) {
        roles[{agent.lane_key, agent.slot}] = agent.police_unit;
        REQUIRE(traffic_vehicle_kind(agent) ==
                (agent.police_unit ? TrafficVehicleKind::Police
                                   : traffic_vehicle_kind(agent.lane_key,
                                                          agent.slot)));
    }
    return roles;
}

bool route_is_contiguous(const LaneGraph& graph,
                         const VehicleAgent& agent) {
    if (agent.police_route.empty() ||
        agent.police_route_index >= agent.police_route.size() ||
        agent.police_route[agent.police_route_index] != agent.lane) {
        return false;
    }
    for (std::size_t i = 0; i + 1u < agent.police_route.size(); ++i) {
        const auto& outgoing = graph.outgoing(agent.police_route[i]);
        if (std::none_of(outgoing.begin(), outgoing.end(),
                         [&](const TurnLink& link) {
                             return link.to == agent.police_route[i + 1u];
                         })) {
            return false;
        }
    }
    return true;
}

void ambient_patrols_and_response_are_deterministic() {
    Network network = build_network();
    AmbientTuning ambient;
    const glm::vec2 target{495.0f, 495.0f};
    constexpr uint64_t kSeed = 0xBADC0FFEEull;

    Crowd forward;
    Crowd reverse;
    forward.build(network.lanes, kSeed, ambient, police_tuning(false));
    reverse.build(network.lanes, kSeed, ambient, police_tuning(true));
    forward.refresh(0, target);
    reverse.refresh(0, target);

    REQUIRE(forward.membership_hash() == reverse.membership_hash());
    const std::map<Identity, bool> initial_roles = police_roles(forward);
    REQUIRE(initial_roles == police_roles(reverse));
    REQUIRE(forward.vehicles().size() > 80u);
    const float patrol_share = static_cast<float>(forward.police_unit_count()) /
                               static_cast<float>(forward.vehicles().size());
    REQUIRE_MSG(patrol_share > 0.06f && patrol_share < 0.20f,
                "stable patrol draw drifted far from the authored 12% mix",
                "patrol share");

    auto tick = [&](Crowd& crowd, int64_t step, int level,
                    const std::vector<VisiblePoliceIdentity>& visible) {
        crowd.set_police_context(level, target, visible);
        if (step % 8 == 0) crowd.refresh(step, target);
        const std::map<Identity, bool> roles_before = police_roles(crowd);
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        REQUIRE(police_roles(crowd) == roles_before);
    };

    // LOS is false here, so only the dispatcher fallback can engage. Level 1
    // gets one immediate responder; a live escalation to level 5 gets exactly
    // one more without waiting for the old 1.4 s cadence.
    tick(forward, 0, 1, {});
    tick(reverse, 0, 1, {});
    REQUIRE(forward.police_pursuit_count() == 1u);
    REQUIRE(police_roles(forward) == initial_roles);
    REQUIRE(forward.population_hash() == reverse.population_hash());

    tick(forward, 1, 5, {});
    tick(reverse, 1, 5, {});
    REQUIRE(forward.police_pursuit_count() == 2u);
    REQUIRE(forward.population_hash() == reverse.population_hash());

    // Level 5 fallback cadence is 0.4 s = 48 fixed steps. No hidden frame-rate
    // counter may squeeze in another unit before that boundary.
    for (int64_t step = 2; step < 49; ++step) {
        tick(forward, step, 5, {});
        tick(reverse, step, 5, {});
        REQUIRE(forward.police_pursuit_count() == 2u);
        REQUIRE(forward.population_hash() == reverse.population_hash());
    }
    tick(forward, 49, 5, {});
    tick(reverse, 49, 5, {});
    REQUIRE(forward.police_pursuit_count() == 3u);
    REQUIRE(forward.population_hash() == reverse.population_hash());

    for (int64_t step = 50; step < 390; ++step) {
        tick(forward, step, 5, {});
        tick(reverse, step, 5, {});
        REQUIRE(forward.police_pursuit_count() <= 7u);
        REQUIRE(forward.population_hash() == reverse.population_hash());
    }
    REQUIRE(forward.police_pursuit_count() == 7u);
    REQUIRE(forward.police_pursuit_count() == reverse.police_pursuit_count());

    std::size_t routed = 0;
    for (const VehicleAgent& agent : forward.vehicles()) {
        if (!agent.police_pursuit) continue;
        REQUIRE(agent.police_unit);
        REQUIRE(traffic_vehicle_kind(agent) == TrafficVehicleKind::Police);
        REQUIRE(route_is_contiguous(network.lanes, agent));
        if (agent.police_route.size() > 1u) ++routed;
    }
    REQUIRE(routed > 0u);
    REQUIRE(forward.nearest_police_pursuer() != nullptr);

    // Engaged contact is range + LOS with no facing gate. Put the target on the
    // nearest responder, allow LOS, then clear wanted: cruisers go silent and
    // return to patrol without popping out of the active population.
    const VehicleAgent* nearest = forward.nearest_police_pursuer();
    const glm::vec2 contact_target{nearest->pos.x, nearest->pos.z};
    forward.set_police_context(5, contact_target, {});
    REQUIRE(!forward.player_in_police_view());
    const std::vector<VisiblePoliceIdentity> visible_nearest{
        {nearest->lane_key, nearest->slot}};
    forward.set_police_context(5, contact_target, visible_nearest);
    REQUIRE(forward.player_in_police_view());
    const std::size_t units_before_clear = forward.police_unit_count();
    forward.set_police_context(0, contact_target, visible_nearest);
    REQUIRE(forward.police_pursuit_count() == 0u);
    REQUIRE(!forward.player_in_police_view());
    forward.rebuild_buckets();
    forward.step_vehicles(391);
    REQUIRE(forward.police_pursuit_count() == 0u);
    REQUIRE(forward.police_unit_count() == units_before_clear);
    REQUIRE(!forward.player_in_police_view());

    std::printf("      %.1f%% ambient patrols, %zu routed responders\n",
                static_cast<double>(patrol_share * 100.0f), routed);
    pass("patrol identity, response cadence, routing, contact, and stand-down are deterministic");
}

void patrol_witnessing_requires_that_patrols_own_los() {
    Network network = build_network();
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 8.0f;
    CrowdTuning tuning = police_tuning(false);
    tuning.vehicle_activate_m = 20.0f;
    tuning.vehicle_retire_m = 32.0f;
    tuning.police.patrol_fraction = 1.0f;

    Crowd crowd;
    crowd.build(network.lanes, 0x51514Eull, ambient, tuning);
    const glm::vec2 activation_center{495.0f, 450.0f};
    int64_t step = 0;
    while (crowd.vehicles().empty() && step < 720) {
        crowd.refresh(step, activation_center);
        ++step;
    }
    REQUIRE(!crowd.vehicles().empty());

    const VehicleAgent& witness = crowd.vehicles().front();
    const glm::vec2 witness_pos{witness.pos.x, witness.pos.z};
    const glm::vec2 witness_fwd = glm::normalize(
        glm::vec2{witness.fwd.x, witness.fwd.z});
    const glm::vec2 target = witness_pos + witness_fwd * 8.0f;
    const VisiblePoliceIdentity witness_id{witness.lane_key, witness.slot};
    REQUIRE(police_can_witness(witness_pos, witness_fwd, target, true, true,
                               crowd.police_tuning()));

    // Every active car is within 20 m of activation_center, and this target is
    // only 8 m from one of them. No car can be in the 55 m fallback ring, so a
    // pursuit here can only be the organic witness conversion under test.
    crowd.set_police_context(1, target, {});
    crowd.rebuild_buckets();
    crowd.step_vehicles(step);
    REQUIRE(crowd.police_pursuit_count() == 0u);

    crowd.set_police_context(1, target, {witness_id});
    crowd.rebuild_buckets();
    crowd.step_vehicles(step + 1);
    REQUIRE(crowd.police_pursuit_count() == 1u);
    REQUIRE(crowd.player_in_police_view());

    crowd.set_police_context(0, target, {});
    REQUIRE(crowd.police_pursuit_count() == 0u);
    pass("only a patrol with its own clear world ray can witness the player");
}

void dispatcher_prefers_the_alpha_ring_then_uses_distant_patrols() {
    Network network = build_network();
    AmbientTuning ambient;
    CrowdTuning tuning = police_tuning(false);
    tuning.police.patrol_fraction = 1.0f;
    const glm::vec2 target{495.0f, 495.0f};

    Crowd crowd;
    crowd.build(network.lanes, 0xA17A51Aull, ambient, tuning);
    crowd.refresh(0, target);

    auto distance_to_target = [&](const VehicleAgent& agent) {
        return glm::length(glm::vec2{agent.pos.x, agent.pos.z} - target);
    };
    REQUIRE(std::any_of(crowd.vehicles().begin(), crowd.vehicles().end(),
                        [&](const VehicleAgent& agent) {
                            const float distance = distance_to_target(agent);
                            return agent.police_unit && distance >= 55.0f &&
                                   distance <= 135.0f;
                        }));

    crowd.set_police_context(1, target, {});
    crowd.rebuild_buckets();
    crowd.step_vehicles(0);
    REQUIRE(crowd.police_pursuit_count() == 1u);
    const VehicleAgent* first = crowd.nearest_police_pursuer();
    REQUIRE(first != nullptr);
    const Identity first_id{first->lane_key, first->slot};
    const float first_distance = distance_to_target(*first);
    REQUIRE(first_distance >= 55.0f && first_distance <= 135.0f);

    // Remove every other patrol around the preferred band. A live escalation
    // still owes the next unit immediately, so the only valid response is an
    // already-liveried patrol farther out in the active set.
    std::vector<Identity> remove_from_band;
    for (const VehicleAgent& agent : crowd.vehicles()) {
        if (agent.police_pursuit) continue;
        const float distance = distance_to_target(agent);
        if (distance > tuning.police.witness_range && distance <= 140.0f)
            remove_from_band.push_back({agent.lane_key, agent.slot});
    }
    for (const Identity& identity : remove_from_band) {
        VehicleAgent removed;
        REQUIRE(crowd.take_vehicle(identity.first, identity.second, removed));
        REQUIRE(removed.police_unit);
    }
    REQUIRE(std::any_of(crowd.vehicles().begin(), crowd.vehicles().end(),
                        [&](const VehicleAgent& agent) {
                            return agent.police_unit && !agent.police_pursuit &&
                                   distance_to_target(agent) > 140.0f;
                        }));

    const std::size_t roles_before = crowd.police_unit_count();
    crowd.set_police_context(2, target, {});
    crowd.rebuild_buckets();
    crowd.step_vehicles(1);
    REQUIRE(crowd.police_pursuit_count() == 2u);
    REQUIRE(crowd.police_unit_count() == roles_before);

    const VehicleAgent* fallback = nullptr;
    for (const VehicleAgent& agent : crowd.vehicles()) {
        if (!agent.police_pursuit ||
            Identity{agent.lane_key, agent.slot} == first_id) {
            continue;
        }
        fallback = &agent;
        break;
    }
    REQUIRE(fallback != nullptr);
    REQUIRE(fallback->police_unit);
    REQUIRE(distance_to_target(*fallback) > 135.0f);
    pass("dispatch prefers 55-135 m, then fills from a distant existing patrol");
}

}  // namespace

int main() {
    std::puts("police_runtime_tests");
    ambient_patrols_and_response_are_deterministic();
    patrol_witnessing_requires_that_patrols_own_los();
    dispatcher_prefers_the_alpha_ring_then_uses_distant_patrols();
    std::puts("PASS police_runtime_tests");
    return 0;
}
