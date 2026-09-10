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
#include "core/fixed_step.h"
#include "physics/vehicle.h"
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
        const auto& outgoing = graph.outgoing(agent.police_route[i], true);
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
    // Seed a sparse dispatcher fixture directly. Occupied police cruisers
    // cannot be transferred to the player now that their officer is visible.
    auto& active = const_cast<std::vector<VehicleAgent>&>(crowd.vehicles());
    active.erase(std::remove_if(active.begin(), active.end(), [&](const auto& agent) {
        return std::find(remove_from_band.begin(), remove_from_band.end(),
                         Identity{agent.lane_key, agent.slot}) != remove_from_band.end();
    }), active.end());
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

VehicleAgent pursuit_car(const LaneGraph& lanes, LaneRef lane, float station) {
    VehicleAgent car;
    car.lane = lane;
    car.lane_key = lanes.lane(lane).key;
    car.slot = 17;
    car.dist_along_m = car.last_dist_m = station;
    car.speed_mps = 8.0f;
    car.cruise_mps = 11.0f;
    car.mode = AgentMode::Integrating;
    car.police_unit = car.police_pursuit = true;
    const auto pose = lanes.pose(lane, station);
    car.pos = pose.position;
    car.fwd = pose.tangent;
    return car;
}

void pursuit_returns_for_a_target_behind_and_stops_to_approach() {
    Network network;
    network.roads.build(make_grid_spines(5, 160.0f), {}, {});
    network.lanes.build(network.roads, {});
    const auto start = network.lanes.nearest_lane_along({275, 322}, {1, 0});
    REQUIRE(start.valid());
    const LaneRef reverse = network.lanes.opposing(start.lane);
    REQUIRE(network.lanes.valid(reverse));
    REQUIRE(std::none_of(network.lanes.outgoing(start.lane).begin(),
        network.lanes.outgoing(start.lane).end(), [&](const TurnLink& t) {
            return t.to == reverse;
        }));
    const auto back = network.lanes.project_onto(start.lane, {210, 322});
    const auto return_route = network.lanes.plan_route(start, back, true);
    REQUIRE(return_route.size() > 2u);
    REQUIRE(return_route.front() == start.lane);
    REQUIRE(return_route.back() == start.lane);

    Crowd crowd;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    crowd.build(network.lanes, 905, {}, tuning);
    auto& cars = const_cast<std::vector<VehicleAgent>&>(crowd.vehicles());
    cars = {pursuit_car(network.lanes, start.lane, start.dist_along_m)};
    glm::vec2 target{210, 318};
    bool turned = false;
    bool exited = false;
    float biggest_step = 0.0f;
    glm::vec3 previous = cars.front().pos;
    for (int64_t step = 0; step < 7200; ++step) {
        // The suspect initially drives away in the opposite direction, then
        // pulls over once the cruiser has actually reversed its heading.
        const glm::vec2 velocity = turned ? glm::vec2{0} : glm::vec2{-3, 0};
        target += velocity * static_cast<float>(kSimDt);
        crowd.set_police_context(1, target);
        crowd.set_police_officer_context(false, false, velocity);
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        const auto& cop = cars.front();
        biggest_step = std::max(biggest_step, glm::distance(previous, cop.pos));
        previous = cop.pos;
        REQUIRE(crowd.stats().ai_collisions == 0);
        if (cop.lane == reverse && cop.fwd.x < -0.9f) turned = true;
        if (cop.officer.phase == PoliceOfficerPhase::Pursuing) {
            REQUIRE(turned);
            REQUIRE(glm::distance(glm::vec2{cop.pos.x, cop.pos.z}, target) < 32.0f);
            REQUIRE(cop.speed_mps < 0.08f);
            exited = true;
            std::printf("      turnaround + pull-over approach: %.2f s, %.2f m away\n",
                double(step) * kSimDt,
                double(glm::distance(glm::vec2{cop.pos.x, cop.pos.z}, target)));
            break;
        }
    }
    REQUIRE(turned);
    REQUIRE(exited);
    REQUIRE(biggest_step < 0.3f);
    pass("pursuer turns at the next junction, closes in, and exits after the suspect stops");
}

void replanning_keeps_a_committed_exit() {
    Network network;
    network.roads.build(make_grid_spines(5, 160.0f), {}, {});
    network.lanes.build(network.roads, {});
    const auto start = network.lanes.nearest_lane_along({310, 322}, {1, 0});
    REQUIRE(start.valid());
    const auto& links = network.lanes.outgoing(start.lane);
    const auto straight = std::find_if(links.begin(), links.end(), [](const auto& t) {
        return t.kind == TurnKind::Straight;
    });
    REQUIRE(straight != links.end());
    Crowd crowd;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    crowd.build(network.lanes, 905, {}, tuning);
    auto car = pursuit_car(network.lanes, start.lane,
        network.lanes.length(start.lane) -
        traffic_junction_clearance(network.lanes, straight->junction, tuning));
    car.committed_junction = straight->junction;
    car.committed_approach_lane = car.lane;
    car.committed_exit_lane = straight->to;
    car.police_route = {car.lane, straight->to};
    auto& cars = const_cast<std::vector<VehicleAgent>&>(crowd.vehicles());
    cars = {car};
    bool cleared = false;
    for (int64_t step = 0; step < 1200; ++step) {
        crowd.set_police_context(1, glm::vec2{280, (step / 30) % 2 ? 400 : 240});
        crowd.set_police_officer_context(false, false, {0, 6});
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        const auto& cop = cars.front();
        if (cop.committed_junction != 0xFFFFFFFFu)
            REQUIRE(cop.committed_exit_lane == straight->to);
        if (cop.lane == straight->to && cop.committed_junction == 0xFFFFFFFFu) {
            cleared = true;
            break;
        }
    }
    REQUIRE(cleared);
    pass("moving target cannot replace the exit of an admitted cruiser");
}

void turnaround_yields_to_an_oncoming_car_in_either_update_order() {
    Network network;
    network.roads.build(make_grid_spines(5, 160.0f), {}, {});
    network.lanes.build(network.roads, {});
    const auto start = network.lanes.nearest_lane_along({310, 322}, {1, 0});
    const auto opposite = network.lanes.nearest_lane_along({330, 318}, {-1, 0});
    REQUIRE(start.valid() && opposite.valid());
    const LaneRef reverse = network.lanes.opposing(start.lane);
    const uint32_t junction = network.lanes.lane(start.lane).junction_to;
    const auto& links = network.lanes.outgoing(opposite.lane);
    REQUIRE(std::any_of(links.begin(), links.end(), [&](const TurnLink& t) {
        return t.to == reverse && t.kind == TurnKind::Straight;
    }));
    CrowdTuning tuning;
    tuning.max_peds = 0;
    const float clear = traffic_junction_clearance(network.lanes, junction, tuning);
    auto cop = pursuit_car(network.lanes, start.lane,
        network.lanes.length(start.lane) - clear);
    cop.speed_mps = 0;
    cop.police_route = {start.lane, reverse};
    cop.stop_junction = junction;
    cop.stop_wait_steps = 1000;
    cop.stop_completed = true;
    cop.stop_arrival_step = 0;
    auto oncoming = pursuit_car(network.lanes, opposite.lane,
        network.lanes.length(opposite.lane) - clear + 0.5f);
    oncoming.slot = 18;
    oncoming.police_unit = oncoming.police_pursuit = false;
    oncoming.committed_junction = junction;
    oncoming.committed_approach_lane = opposite.lane;
    oncoming.committed_exit_lane = reverse;
    for (uint32_t d = 0; d < 1000u; ++d) {
        if (network.lanes.choose_next(opposite.lane, 905, d) == reverse) {
            oncoming.decisions = d;
            break;
        }
    }
    REQUIRE(network.lanes.choose_next(opposite.lane, 905, oncoming.decisions) == reverse);
    Crowd forward, backward;
    forward.build(network.lanes, 905, {}, tuning);
    backward.build(network.lanes, 905, {}, tuning);
    const_cast<std::vector<VehicleAgent>&>(forward.vehicles()) = {cop, oncoming};
    const_cast<std::vector<VehicleAgent>&>(backward.vehicles()) = {oncoming, cop};
    bool yielded = false;
    bool turned = false;
    for (int64_t step = 1; step < 2400; ++step) {
        for (Crowd* crowd : {&forward, &backward}) {
            crowd->set_police_context(1, glm::vec2{220, 318});
            crowd->set_police_officer_context(false, false, {-3, 0});
            crowd->rebuild_buckets();
            crowd->step_vehicles(step);
            REQUIRE(crowd->stats().ai_collisions == 0);
        }
        const auto& a = forward.vehicles().front();
        const auto& b = backward.vehicles().back();
        REQUIRE(a.lane == b.lane && a.pos == b.pos && a.speed_mps == b.speed_mps);
        if (step == 1) {
            REQUIRE(a.lane == start.lane);
            REQUIRE(a.committed_junction == 0xFFFFFFFFu);
            REQUIRE(a.speed_mps < 0.001f);
            REQUIRE(forward.stats().junction_yields > 0);
            yielded = true;
        }
        if (a.lane == reverse && a.turn_from_lane == kInvalidLane) {
            turned = true;
            break;
        }
    }
    REQUIRE(yielded && turned);
    pass("turnaround yields to oncoming traffic and is identical in reversed update order");
}

void blocked_cruiser_approaches_a_stopped_suspect_on_foot() {
    Network network;
    network.roads.build(make_grid_spines(5, 160.0f), {}, {});
    network.lanes.build(network.roads, {});
    const auto start = network.lanes.nearest_lane_along({220, 322}, {1, 0});
    REQUIRE(start.valid());
    auto cop = pursuit_car(network.lanes, start.lane, start.dist_along_m);
    auto blocker = pursuit_car(network.lanes, start.lane, start.dist_along_m + 10.0f);
    blocker.police_pursuit = blocker.police_unit = false;
    blocker.slot = 18;
    cop.speed_mps = blocker.speed_mps = blocker.cruise_mps = 0.0f;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    Crowd crowd;
    crowd.build(network.lanes, 905, {}, tuning);
    auto& cars = const_cast<std::vector<VehicleAgent>&>(crowd.vehicles());
    cars = {cop, blocker};
    // Neither driver has a safe lateral escape in this queued-officer case.
    // The dedicated emergency suite exercises the open-shoulder alternative.
    std::vector<glm::vec3> parked;
    for (float d = 2.f; d < 50.f; d += 4.f)
        for (float side : {-4.f, 4.f})
            parked.push_back(network.lanes.pose(start.lane, start.dist_along_m+d, side).position);
    crowd.set_parked_vehicle_poses(parked);
    const auto target = network.lanes.pose(start.lane, start.dist_along_m + 42.0f).position;
    bool exited = false;
    for (int64_t step = 0; step < 1800; ++step) {
        crowd.set_police_context(1, target);
        // Waiting traffic alone must not make a cop abandon a moving chase.
        crowd.set_police_officer_context(false, false,
            step < 480 ? glm::vec2{4, 0} : glm::vec2{0});
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        const auto& police = cars.front();
        REQUIRE(crowd.stats().ai_collisions == 0);
        if (step < 480) REQUIRE(police.officer.phase == PoliceOfficerPhase::Seated);
        if (police.officer.phase == PoliceOfficerPhase::Pursuing) {
            REQUIRE(police.delay_seconds >= 2.0f);
            REQUIRE(police.speed_mps < 0.08f);
            REQUIRE(glm::distance(police.pos, target) > 32.0f);
            exited = true;
            break;
        }
    }
    REQUIRE(exited);
    pass("queued officer leaves the cruiser for a nearby stopped suspect, but keeps driving after a moving one");
}

void low_heat_pursuit_follows_instead_of_ramming() {
    Network network;
    network.roads.build(make_grid_spines(3, 600.0f), {}, {});
    network.lanes.build(network.roads, {});
    const auto start = network.lanes.nearest_lane_along({200, 602}, {1, 0});
    REQUIRE(start.valid());
    auto cop = pursuit_car(network.lanes, start.lane, start.dist_along_m);
    cop.speed_mps = 16.0f;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    Crowd crowd;
    crowd.build(network.lanes, 905, {}, tuning);
    auto& cars = const_cast<std::vector<VehicleAgent>&>(crowd.vehicles());
    cars = {cop};
    VehicleState player;
    const auto pose = network.lanes.pose(start.lane, start.dist_along_m + 35.0f);
    player.position = pose.position;
    player.velocity = pose.tangent * 8.0f;
    float closest = 1000.0f;
    for (int64_t step = 0; step < 1440; ++step) {
        player.position += player.velocity * static_cast<float>(kSimDt);
        crowd.set_police_context(1, player.position);
        crowd.set_police_officer_context(false, false, {8, 0});
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, &player);
        const auto& police = cars.front();
        const float gap = glm::dot(player.position - police.pos, pose.tangent);
        closest = std::min(closest, gap);
        REQUIRE(gap >= 5.5f);
        REQUIRE(police.officer.phase == PoliceOfficerPhase::Seated);
    }
    REQUIRE(closest < 15.0f);
    REQUIRE(cars.front().pos.x - cop.pos.x > 90.0f);
    REQUIRE(std::fabs(cars.front().speed_mps - 8.0f) < 1.0f);
    pass("low-heat cruiser closes on a moving suspect and matches speed without bumper contact");
}

}  // namespace

int main() {
    std::puts("police_runtime_tests");
    ambient_patrols_and_response_are_deterministic();
    patrol_witnessing_requires_that_patrols_own_los();
    dispatcher_prefers_the_alpha_ring_then_uses_distant_patrols();
    pursuit_returns_for_a_target_behind_and_stops_to_approach();
    replanning_keeps_a_committed_exit();
    turnaround_yields_to_an_oncoming_car_in_either_update_order();
    blocked_cruiser_approaches_a_stopped_suspect_on_foot();
    low_heat_pursuit_follows_instead_of_ramming();
    std::puts("PASS police_runtime_tests");
    return 0;
}
