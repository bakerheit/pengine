// Runtime traffic contracts: the visible signal clock and the driver clock are
// one function, the ambient population promotes into real moving cars near the
// player, and those cars are solid to the player vehicle.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include <glm/glm.hpp>

#include "physics/vehicle.h"
#include "core/fixed_step.h"
#include "city/map.h"
#include "city/spines.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "terrain/heightmap.h"
#include "road_fixture.h"
#include "test_assert.h"
#include "traffic/ambient.h"
#include "traffic/crowd.h"

using namespace apricot;
using apricot_test::pass;

namespace {

void pedestrian_paths_follow_pavements_and_junction_mouths() {
    RoadGraph roads;
    roads.build(make_grid_spines(3, 70.0f), RoadGraphParams{}, GroundSampler{});
    LaneGraph lanes;
    lanes.build(roads, GroundSampler{});
    PedestrianPaths paths;
    paths.build(lanes, 2.2f);
    int corners = 0, crossings = 0;
    for (LaneRef lr = 0; lr < lanes.lane_count(); ++lr) {
        for (uint32_t side = 0; side < 2; ++side) {
            const uint32_t id = paths.for_lane(lr, side);
            REQUIRE(paths.valid(id));
            const PedWalkPath& p = paths.path(id);
            REQUIRE(!p.links.empty());
            const Lane& lane = lanes.lane(p.lane);
            const glm::vec3 middle = p.line.pose(p.line.length * 0.5f).position;
            const LaneProjection projection = lanes.project_onto(p.lane, {middle.x, middle.z});
            REQUIRE(std::fabs(std::fabs(projection.lateral_m + lane.lateral_offset_m)
                              - (lane.width_m * 0.5f + 2.2f)) < 0.01f);
            for (const PedWalkLink& link : p.links) {
                REQUIRE(paths.valid(link.to));
                const PedWalkPath& target = paths.path(link.to);
                REQUIRE(glm::length(p.line.points.back() - link.line.points.front()) < 0.0001f);
                REQUIRE(glm::length(target.line.points.front() - link.line.points.back()) < 0.0001f);
                REQUIRE(lane.junction_to == lanes.lane(target.lane).junction_from);
                if (link.crossing) {
                    ++crossings;
                    REQUIRE(lane.edge == lanes.lane(target.lane).edge);
                    const glm::vec3 delta = link.line.points.back() - link.line.points.front();
                    REQUIRE(std::fabs(glm::dot(glm::normalize(delta), p.line.pose(p.line.length).tangent)) < 0.01f);
                    REQUIRE(std::fabs(link.line.length - (lane.width_m + 4.4f)) < 0.01f);
                } else if (link.line.length > 0.1f) {
                    ++corners;
                    // On this orthogonal grid every corner sample must stay
                    // outside both carriageways, including at the L vertex.
                    const glm::vec3 junction = lanes.junction(lane.junction_to).pos;
                    for (float d = 0; d <= link.line.length; d += 0.1f) {
                        const glm::vec3 q = link.line.pose(d).position - junction;
                        for (LaneRef arm : lanes.junction(lane.junction_to).outgoing) {
                            const glm::vec3 tangent = lanes.pose(arm, 0).tangent;
                            const float along = glm::dot(q, tangent);
                            const float lateral = std::fabs(q.x * tangent.z - q.z * tangent.x);
                            REQUIRE(along <= 0.01f || lateral >= lanes.lane(arm).width_m * 0.5f + 1.0f);
                        }
                    }
                }
            }
        }
    }
    REQUIRE(corners > 0);
    REQUIRE(crossings > 0);
    pass("pedestrian paths join exact endpoints and cross perpendicular at road mouths");
}

void pedestrian_bends_and_dead_ends_are_continuous() {
    auto spines = make_test_spines();
    RoadGraph roads;
    roads.build(spines, RoadGraphParams{}, GroundSampler{});
    LaneGraph lanes;
    lanes.build(roads, GroundSampler{});
    PedestrianPaths paths;
    paths.build(lanes, 2.2f);
    bool saw_bend = false, saw_dead_end = false, rejected_no_sidewalk = false;
    for (LaneRef lr = 0; lr < lanes.lane_count(); ++lr) {
        const uint32_t id = paths.for_lane(lr, 0);
        if (!road_class_def(lanes.lane(lr).cls).sidewalks) {
            REQUIRE(!paths.valid(id));
            rejected_no_sidewalk = true;
            continue;
        }
        REQUIRE(paths.valid(id));
        const PedWalkPath& p = paths.path(id);
        for (std::size_t i = 1; i + 1 < p.line.cum.size(); ++i) {
            const float d = p.line.cum[i];
            REQUIRE(glm::length(p.line.pose(d + 0.001f, 1.1f).position -
                                p.line.pose(d - 0.001f, 1.1f).position) < 0.004f);
            saw_bend = true;
        }
        if (lanes.junction(lanes.lane(lr).junction_to).degree == 1) {
            REQUIRE(p.links.size() == 1);
            const auto& link = p.links.front();
            REQUIRE(!link.crossing);
            REQUIRE(link.line.length < 0.0001f);
            const auto& reverse = paths.path(link.to);
            REQUIRE(glm::length(reverse.line.points.front() - p.line.points.back()) < 0.0001f);
            REQUIRE(glm::dot(reverse.line.pose(0).tangent, p.line.pose(p.line.length).tangent) < -0.99f);
            saw_dead_end = true;
        }
    }
    REQUIRE(saw_bend && saw_dead_end && rejected_no_sidewalk);
    pass("pedestrian bends are continuous and dead ends reverse on the same footway");
}

void pedestrian_crossings_clear_acute_junctions_and_reject_tiny_roads() {
    std::vector<RoadSpine> spines;
    uint32_t spine_id = 1;
    for (glm::vec2 end : {glm::vec2{120, 0}, glm::vec2{120, 70}, glm::vec2{-120, 0}}) {
        RoadSpine spine;
        spine.id = spine_id++;
        spine.points = {{0, 0}, end};
        spines.push_back(spine);
    }
    RoadGraph roads;
    roads.build(spines, RoadGraphParams{}, GroundSampler{});
    LaneGraph lanes;
    lanes.build(roads, GroundSampler{});
    PedestrianPaths paths;
    paths.build(lanes, 2.2f);
    int crossings = 0;
    for (LaneRef lr = 0; lr < lanes.lane_count(); ++lr) {
        for (uint32_t side = 0; side < 2; ++side) {
            const uint32_t id = paths.for_lane(lr, side);
            REQUIRE(paths.valid(id));
            const auto& p = paths.path(id);
            const auto& lane = lanes.lane(p.lane);
            for (const auto& link : p.links) {
                if (!link.crossing) continue;
                ++crossings;
                const auto& junction = lanes.junction(lane.junction_to);
                for (float d = 0; d < link.line.length; d += 0.1f) {
                    const glm::vec3 q = link.line.pose(d).position - junction.pos;
                    for (LaneRef arm : junction.outgoing) {
                        if (lanes.lane(arm).edge == lane.edge) continue;
                        const glm::vec3 t = lanes.pose(arm, 0).tangent;
                        REQUIRE(glm::dot(q, t) <= 0.01f ||
                            std::fabs(q.x * t.z - q.z * t.x) >= lanes.lane(arm).width_m * 0.5f + 1.0f);
                    }
                }
            }
            if (p.links.size() == 2) {
                std::set<uint32_t> choices;
                for (uint32_t decision = 0; decision < 256; ++decision)
                    choices.insert(paths.choose(id, 42, lane.key, side, decision));
                REQUIRE(choices.size() == 2);
            }
        }
    }
    REQUIRE(crossings > 0);
    spines.resize(1);
    spines[0].points = {{0, 0}, {10, 0}};
    roads.build(spines, RoadGraphParams{}, GroundSampler{});
    lanes.build(roads, GroundSampler{});
    paths.build(lanes, 2.2f);
    for (LaneRef lr = 0; lr < lanes.lane_count(); ++lr) {
        REQUIRE(!paths.valid(paths.for_lane(lr, 0)));
        REQUIRE(!paths.valid(paths.for_lane(lr, 1)));
    }
    pass("acute crossings clear adjacent roads and tiny roads cannot shrink junction setbacks");
}

void pedestrians_make_deterministic_progress_without_teleports() {
    RoadGraph roads;
    roads.build(make_grid_spines(3, 55.0f), RoadGraphParams{}, GroundSampler{});
    LaneGraph lanes;
    lanes.build(roads, GroundSampler{});
    AmbientTuning ambient;
    ambient.max_vehicle_slots = 0;
    ambient.ped_spacing_m = 14.0f;
    CrowdTuning tuning;
    tuning.ped_activate_m = 1000;
    tuning.ped_retire_m = 2000;
    Crowd a, b;
    a.build(lanes, 0x504544, ambient, tuning);
    tuning.reverse_scan_order = true;
    b.build(lanes, 0x504544, ambient, tuning);
    a.refresh(0, {55, 55});
    b.refresh(0, {55, 55});
    REQUIRE(a.peds().size() > 20);
    REQUIRE(a.population_hash() == b.population_hash());
    std::vector<glm::vec3> previous;
    std::vector<float> travelled(a.peds().size(), 0.0f);
    std::vector<std::pair<uint64_t, uint32_t>> identities;
    for (const auto& p : a.peds()) {
        previous.push_back(p.pos);
        identities.emplace_back(p.lane_key, p.slot);
    }
    for (int64_t step = 1; step <= 24000; ++step) {
        a.rebuild_buckets();
        b.rebuild_buckets();
        a.step_peds(step);
        b.step_peds(step);
        REQUIRE(a.population_hash() == b.population_hash());
        for (std::size_t i = 0; i < a.peds().size(); ++i) {
            const auto& p = a.peds()[i];
            REQUIRE(p.lane_key == identities[i].first && p.slot == identities[i].second);
            const float moved = glm::length(p.pos - previous[i]);
            // Bound includes forward motion, smoothed separation and tapering
            // of the lateral offset near a vertex; old endpoint jumps fail it.
            REQUIRE(moved <= (p.speed_mps * 1.4f + 0.91f) *
                             static_cast<float>(tuning.ped_sub_rate) * static_cast<float>(kSimDt) + 0.005f);
            travelled[i] += moved;
            previous[i] = p.pos;
        }
    }
    uint32_t decisions = 0;
    for (std::size_t i = 0; i < a.peds().size(); ++i) {
        REQUIRE(travelled[i] > 30.0f);
        REQUIRE(a.peds()[i].walk_decisions > 0);
        decisions += a.peds()[i].walk_decisions;
    }
    REQUIRE(decisions > a.peds().size() * 2u);
    pass("all active pedestrians progress for 200 seconds with stable identities and scan-order determinism");
}

struct Network {
    RoadGraph roads;
    LaneGraph lanes;
};

Network build_network() {
    Network n;
    n.roads.build(make_test_spines(), RoadGraphParams{}, GroundSampler{});
    n.lanes.build(n.roads, GroundSampler{}, LaneBuildParams{});
    return n;
}

uint32_t junction_at(const LaneGraph& lanes, glm::vec2 p) {
    for (uint32_t i = 0; i < lanes.junction_count(); ++i) {
        const glm::vec3 q = lanes.junction(i).pos;
        if (glm::length(glm::vec2{q.x, q.z} - p) < 0.5f) return i;
    }
    return 0xFFFFFFFFu;
}

void signal_cycle_is_shared_and_opposed() {
    const Network n = build_network();
    const uint32_t junction = junction_at(n.lanes, {0.0f, 0.0f});
    REQUIRE(junction < n.lanes.junction_count());
    REQUIRE(n.lanes.junction_control(junction) == JunctionControl::Signal);

    LaneRef approach_a = kInvalidLane;
    LaneRef approach_b = kInvalidLane;
    for (LaneRef incoming : n.lanes.junction(junction).incoming) {
        (n.lanes.approach_group_a(junction, incoming) ? approach_a : approach_b) =
            incoming;
    }
    REQUIRE(n.lanes.valid(approach_a));
    REQUIRE(n.lanes.valid(approach_b));

    CrowdTuning tuning;
    tuning.signal_period_steps = 120;
    tuning.signal_yellow_steps = 20;

    REQUIRE(traffic_signal_phase(n.lanes, junction, approach_a, 0, tuning) ==
            TrafficSignalPhase::Green);
    REQUIRE(traffic_signal_phase(n.lanes, junction, approach_b, 0, tuning) ==
            TrafficSignalPhase::Red);
    REQUIRE(traffic_signal_phase(n.lanes, junction, approach_a, 40, tuning) ==
            TrafficSignalPhase::Yellow);
    REQUIRE(traffic_signal_phase(n.lanes, junction, approach_b, 40, tuning) ==
            TrafficSignalPhase::Red);
    REQUIRE(traffic_signal_phase(n.lanes, junction, approach_a, 60, tuning) ==
            TrafficSignalPhase::Red);
    REQUIRE(traffic_signal_phase(n.lanes, junction, approach_b, 60, tuning) ==
            TrafficSignalPhase::Green);
    REQUIRE(traffic_signal_phase(n.lanes, junction, approach_b, 100, tuning) ==
            TrafficSignalPhase::Yellow);
    pass("opposed approaches share one green-yellow-red signal clock");
}

void junction_turns_have_continuous_heading() {
    const Network n = build_network();
    bool saw_left = false;
    bool saw_right = false;
    for (LaneRef incoming = 0; incoming < n.lanes.lane_count(); ++incoming) {
        for (const TurnLink& link : n.lanes.outgoing(incoming)) {
            if (link.kind != TurnKind::Left && link.kind != TurnKind::Right)
                continue;
            const Lane& in_lane = n.lanes.lane(incoming);
            const Lane& out_lane = n.lanes.lane(link.to);
            const CrowdTuning tuning;
            float widest = 0.0f;
            for (LaneRef lane : n.lanes.junction(link.junction).incoming)
                widest = std::max(widest, n.lanes.lane(lane).width_m);
            for (LaneRef lane : n.lanes.junction(link.junction).outgoing)
                widest = std::max(widest, n.lanes.lane(lane).width_m);
            const float clearance = traffic_junction_clearance(
                widest, tuning.traffic_half_length_m,
                tuning.junction_storage_margin_m,
                std::max(tuning.stop_line_m, tuning.junction_clear_m));
            const float entry_m = std::min(clearance,
                                            in_lane.length_m * 0.45f);
            const float exit_m = std::min(clearance,
                                           out_lane.length_m * 0.45f);
            const TrafficTurnCurve curve = traffic_turn_curve(
                n.lanes, incoming, link.to, entry_m, exit_m);
            REQUIRE(curve.length_m > 1.0f);

            const LanePose expected_start = n.lanes.pose(
                incoming, in_lane.length_m - entry_m);
            const LanePose expected_finish = n.lanes.pose(link.to, exit_m);
            const LanePose start = traffic_turn_pose(curve, 0.0f);
            const LanePose finish = traffic_turn_pose(
                curve, curve.length_m);
            REQUIRE(glm::length(start.position - expected_start.position) <
                    0.001f);
            REQUIRE(glm::length(finish.position - expected_finish.position) <
                    0.001f);
            REQUIRE(glm::dot(start.tangent, expected_start.tangent) > 0.999f);
            REQUIRE(glm::dot(finish.tangent, expected_finish.tangent) >
                    0.999f);

            float max_heading_step = 0.0f;
            LanePose previous = start;
            for (int sample = 1; sample <= 48; ++sample) {
                const LanePose pose = traffic_turn_pose(
                    curve, curve.length_m * static_cast<float>(sample) / 48.0f);
                const float angle = std::acos(glm::clamp(
                    glm::dot(previous.tangent, pose.tangent), -1.0f, 1.0f));
                max_heading_step = std::max(max_heading_step, angle);
                previous = pose;
            }
            const float total_heading = std::acos(glm::clamp(
                glm::dot(start.tangent, finish.tangent), -1.0f, 1.0f));
            REQUIRE(total_heading > 0.7f);
            REQUIRE(max_heading_step < 0.12f);
            saw_left |= link.kind == TurnKind::Left;
            saw_right |= link.kind == TurnKind::Right;
            if (saw_left && saw_right) break;
        }
        if (saw_left && saw_right) break;
    }
    REQUIRE(saw_left);
    REQUIRE(saw_right);
    pass("left and right junction paths rotate through a smooth curve");
}

void stop_signs_require_a_real_dwell() {
    TrafficStopDecision d = traffic_stop_decision(
        8.0f, 7.0f, 0, 30, 90, 0.45f);
    REQUIRE(d.hold);
    REQUIRE(d.wait_steps == 0);

    d = traffic_stop_decision(0.2f, 1.0f, d.wait_steps, 30, 90, 0.45f);
    REQUIRE(d.hold);
    REQUIRE(d.wait_steps == 0);

    d = traffic_stop_decision(0.2f, 0.0f, d.wait_steps, 30, 90, 0.45f);
    REQUIRE(d.hold);
    REQUIRE(d.wait_steps == 30);
    d = traffic_stop_decision(0.2f, 0.0f, d.wait_steps, 30, 90, 0.45f);
    REQUIRE(d.hold);
    d = traffic_stop_decision(0.2f, 0.0f, d.wait_steps, 30, 90, 0.45f);
    REQUIRE(!d.hold);
    REQUIRE(d.completed);
    REQUIRE(d.wait_steps == 90);
    pass("a stop sign requires reaching the line, settling, and dwelling");
}

void junction_right_of_way_has_one_winner() {
    TrafficApproachView a;
    a.valid = true;
    a.priority = 3;
    a.eta_seconds = 1.0f;
    a.arrival_step = 100;
    a.lane_key = 10;

    TrafficApproachView b = a;
    b.priority = 1;
    b.lane_key = 20;
    REQUIRE(!traffic_approach_yields(a, b, false, 0.3f));
    REQUIRE(traffic_approach_yields(b, a, false, 0.3f));

    b.priority = a.priority;
    b.eta_seconds = 0.4f;
    REQUIRE(traffic_approach_yields(a, b, false, 0.3f));
    REQUIRE(!traffic_approach_yields(b, a, false, 0.3f));

    b.eta_seconds = a.eta_seconds;
    b.committed = true;
    REQUIRE(traffic_approach_yields(a, b, false, 0.3f));

    b.committed = false;
    b.arrival_step = 120;
    REQUIRE(!traffic_approach_yields(a, b, true, 0.3f));
    REQUIRE(traffic_approach_yields(b, a, true, 0.3f));

    b.arrival_step = a.arrival_step;
    REQUIRE(!traffic_approach_yields(a, b, true, 0.3f));
    REQUIRE(traffic_approach_yields(b, a, true, 0.3f));
    pass("priority, arrival, commitment, and identity select one junction winner");
}

void intersection_entry_requires_downstream_storage() {
    constexpr float clear = 8.0f;
    constexpr float car = 4.4f;
    constexpr float gap = 5.0f;
    constexpr float margin = 1.0f;
    constexpr float required = clear + car + gap + margin;

    REQUIRE(traffic_exit_has_storage(std::numeric_limits<float>::infinity(),
                                     clear, car, gap, margin));
    REQUIRE(!traffic_exit_has_storage(required - 0.01f, clear, car, gap,
                                      margin));
    REQUIRE(traffic_exit_has_storage(required, clear, car, gap, margin));
    REQUIRE(traffic_same_committed_movement(4u, 9u, 4u, 9u));
    REQUIRE(!traffic_same_committed_movement(4u, 9u, 5u, 9u));
    REQUIRE(!traffic_same_committed_movement(4u, 9u, 4u, 10u));
    REQUIRE_NEAR(traffic_junction_clearance(
                     22.0f, 2.25f, 1.0f, 8.0f),
                 14.25, 1e-6);
    REQUIRE_NEAR(traffic_junction_clearance(
                     6.0f, 2.25f, 1.0f, 8.0f),
                 8.0, 1e-6);
    pass("intersection entry requires real downstream storage");
}

void nearby_traffic_moves_and_is_solid() {
    const Network n = build_network();
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 24.0f;
    ambient.max_vehicle_slots = 12;

    CrowdTuning tuning;
    tuning.vehicle_activate_m = 260.0f;
    tuning.vehicle_retire_m = 340.0f;
    tuning.max_peds = 0;
    tuning.refresh_every_steps = 1;

    Crowd crowd;
    crowd.build(n.lanes, 0xA9C011ull, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});
    REQUIRE(crowd.vehicles().size() > 8);

    crowd.rebuild_buckets();
    crowd.step_vehicles(0);
    const uint64_t before = crowd.population_hash();
    for (int64_t step = 1; step <= 24; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
    }
    REQUIRE(crowd.population_hash() != before);

    const VehicleAgent& traffic = crowd.vehicles().front();
    glm::vec3 fwd = glm::normalize(glm::vec3{traffic.fwd.x, 0.0f, traffic.fwd.z});
    const glm::vec3 right{-fwd.z, 0.0f, fwd.x};

    VehicleState player;
    player.position = traffic.pos;
    const float player_yaw = std::atan2(-fwd.x, -fwd.z);
    player.orientation = glm::angleAxis(
        player_yaw, glm::vec3{0.0f, 1.0f, 0.0f});
    player.velocity = traffic.fwd * traffic.speed_mps - right * 10.0f;
    const glm::vec3 old_position = player.position;
    const glm::vec3 old_traffic_position = traffic.pos;
    REQUIRE(crowd.resolve_player_collision(player));
    REQUIRE(glm::length(player.position - old_position) > 0.5f);
    REQUIRE(glm::length(traffic.pos - old_traffic_position) > 0.5f);
    REQUIRE(glm::length(traffic.collision_offset_xz) > 0.5f);
    REQUIRE(player.impact_count >= 1u);
    REQUIRE(player.health < 100.0f);
    REQUIRE(player.last_impact_speed > 2.5f);
    REQUIRE(vehicle_damage_total(player.body_damage) > 0.0f);
    REQUIRE(vehicle_damage_total(traffic.body_damage) > 0.0f);
    pass("nearby traffic moves, separates the player, and reports crash damage");
}

void player_uses_the_full_chassis_footprint() {
    const Network n = build_network();
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 34.0f;
    CrowdTuning tuning;
    tuning.vehicle_activate_m = 260.0f;
    tuning.vehicle_retire_m = 340.0f;
    tuning.max_peds = 0;

    Crowd crowd;
    crowd.build(n.lanes, 0xF007B0D1ull, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});
    REQUIRE(!crowd.vehicles().empty());

    const VehicleAgent& target = crowd.vehicles().front();
    const uint64_t lane_key = target.lane_key;
    const uint32_t slot = target.slot;
    const glm::vec3 fwd = glm::normalize(
        glm::vec3{target.fwd.x, 0.0f, target.fwd.z});

    VehicleState player;
    constexpr float kPlayerHalfLength = 2.445f;
    const float initial_centre_gap =
        tuning.traffic_half_length_m + kPlayerHalfLength - 0.15f;
    player.position = target.pos - fwd * initial_centre_gap;
    const float yaw = std::atan2(-fwd.x, -fwd.z);
    player.orientation = glm::angleAxis(
        yaw, glm::vec3{0.0f, 1.0f, 0.0f});
    player.velocity = fwd * (target.speed_mps + 8.0f);

    // The old centre-circle collider did not touch here: almost a metre of
    // visible hood could enter the traffic body before its 0.92 m radius met
    // the traffic box. Full OBBs overlap by 15 cm and must resolve now.
    REQUIRE(crowd.resolve_player_collision(player));
    const auto hit = std::find_if(
        crowd.vehicles().begin(), crowd.vehicles().end(),
        [&](const VehicleAgent& agent) {
            return agent.lane_key == lane_key && agent.slot == slot;
        });
    REQUIRE(hit != crowd.vehicles().end());
    const float resolved_centre_gap = std::fabs(
        glm::dot(player.position - hit->pos, fwd));
    REQUIRE(resolved_centre_gap > initial_centre_gap + 0.10f);
    REQUIRE(player.impact_count >= 1u);
    pass("player collision reaches the visible hood and trunk tips");
}

void damaged_player_nose_does_not_keep_an_invisible_bumper() {
    const Network n = build_network();
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 34.0f;
    CrowdTuning tuning;
    tuning.vehicle_activate_m = 260.0f;
    tuning.vehicle_retire_m = 340.0f;
    tuning.max_peds = 0;

    Crowd crowd;
    crowd.build(n.lanes, 0xD4A6EDull, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});
    REQUIRE(!crowd.vehicles().empty());

    const VehicleAgent& target = crowd.vehicles().front();
    const glm::vec3 fwd = glm::normalize(
        glm::vec3{target.fwd.x, 0.0f, target.fwd.z});
    const TrafficVehicleFootprint target_footprint =
        traffic_vehicle_footprint(
            traffic_vehicle_kind(target.lane_key, target.slot));
    constexpr float kPlayerHalfWidth = 1.045f;
    constexpr float kPlayerHalfLength = 2.445f;

    VehicleState player;
    const float yaw = std::atan2(-fwd.x, -fwd.z);
    player.orientation = glm::angleAxis(
        yaw, glm::vec3{0.0f, 1.0f, 0.0f});
    player.velocity = fwd * (target.speed_mps + 8.0f);
    player.body_damage.zones[kDamageFrontCenter] = 1.0f;
    const VehicleDamageCollisionFootprint crushed =
        vehicle_damage_collision_footprint(
            player.body_damage, kPlayerHalfWidth, kPlayerHalfLength,
            {0.0f, -1.0f});
    REQUIRE(crushed.front_extent_m < kPlayerHalfLength - 0.50f);

    // This overlaps the old pristine rectangle by 5 cm, but leaves nearly
    // half a metre between the target and the visibly crushed centre bumper.
    const float old_contact_gap = target_footprint.half_length_m +
                                  kPlayerHalfLength - 0.05f;
    player.position = target.pos - fwd * old_contact_gap;
    REQUIRE(!crowd.resolve_player_collision(
        player, kPlayerHalfWidth, kPlayerHalfLength));

    // Once the damaged bumper itself reaches the other body, contact resumes.
    const float damaged_contact_gap = target_footprint.half_length_m +
                                      crushed.front_extent_m - 0.05f;
    player.position = target.pos - fwd * damaged_contact_gap;
    REQUIRE(crowd.resolve_player_collision(
        player, kPlayerHalfWidth, kPlayerHalfLength));
    REQUIRE(player.impact_count >= 1u);
    pass("a crushed player nose no longer collides across visible empty space");
}

void traffic_brakes_before_hitting_the_player() {
    const Network n = build_network();
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 28.0f;
    CrowdTuning tuning;
    tuning.vehicle_activate_m = 280.0f;
    tuning.vehicle_retire_m = 360.0f;
    tuning.max_peds = 0;

    Crowd crowd;
    crowd.build(n.lanes, 0xCAFE1234ull, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});

    const VehicleAgent* picked = nullptr;
    for (const VehicleAgent& agent : crowd.vehicles()) {
        const Lane& lane = n.lanes.lane(agent.lane);
        if (agent.speed_mps > 3.0f &&
            lane.length_m - agent.dist_along_m > 35.0f) {
            picked = &agent;
            break;
        }
    }
    REQUIRE(picked != nullptr);
    const uint64_t lane_key = picked->lane_key;
    const uint32_t slot = picked->slot;
    const float before = picked->speed_mps;
    glm::vec3 fwd = glm::normalize(
        glm::vec3{picked->fwd.x, 0.0f, picked->fwd.z});

    VehicleState player;
    player.position = picked->pos + fwd * 8.0f;
    player.velocity = glm::vec3{0.0f};
    crowd.rebuild_buckets();
    crowd.step_vehicles(0, &player);

    const auto after = std::find_if(
        crowd.vehicles().begin(), crowd.vehicles().end(),
        [&](const VehicleAgent& agent) {
            return agent.lane_key == lane_key && agent.slot == slot;
        });
    REQUIRE(after != crowd.vehicles().end());
    REQUIRE(after->speed_mps < before);
    REQUIRE(crowd.stats().player_hazards > 0u);
    Crowd parked_test;
    parked_test.build(n.lanes,0xCAFE1234ull,ambient,tuning);
    parked_test.refresh(0,{0,0});
    parked_test.set_parked_vehicle_positions({{player.position.x,player.position.z}});
    parked_test.rebuild_buckets();parked_test.step_vehicles(0);
    const auto parked_after=std::find_if(parked_test.vehicles().begin(),parked_test.vehicles().end(),
        [&](const VehicleAgent& v) { return v.lane_key==lane_key && v.slot==slot; });
    REQUIRE(parked_after!=parked_test.vehicles().end());
    REQUIRE(parked_after->speed_mps<before);
    pass("traffic treats the player's car as a predicted hazard before contact");
}

void traffic_cars_absorb_side_impact_and_recover() {
    const Network n = build_network();
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 34.0f;
    CrowdTuning tuning;
    tuning.vehicle_activate_m = 260.0f;
    tuning.vehicle_retire_m = 340.0f;
    tuning.max_peds = 0;

    Crowd crowd;
    crowd.build(n.lanes, 0x51DE1A7ull, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});
    REQUIRE(!crowd.vehicles().empty());

    const VehicleAgent& target = crowd.vehicles().front();
    const uint64_t lane_key = target.lane_key;
    const uint32_t slot = target.slot;
    const glm::vec3 fwd = glm::normalize(
        glm::vec3{target.fwd.x, 0.0f, target.fwd.z});
    const glm::vec3 right{-fwd.z, 0.0f, fwd.x};

    VehicleState player;
    player.position = target.pos + right * 1.50f + fwd * 1.20f;
    player.velocity = fwd * target.speed_mps - right * 12.0f;
    REQUIRE(crowd.resolve_player_collision(player));

    auto find_target = [&]() {
        return std::find_if(
            crowd.vehicles().begin(), crowd.vehicles().end(),
            [&](const VehicleAgent& agent) {
                return agent.lane_key == lane_key && agent.slot == slot;
            });
    };
    auto hit = find_target();
    REQUIRE(hit != crowd.vehicles().end());
    REQUIRE(glm::dot(hit->collision_velocity_xz,
                     glm::vec2{-right.x, -right.z}) > 3.0f);
    REQUIRE(std::fabs(hit->collision_yaw_velocity) > 0.1f);
    REQUIRE(hit->mode == AgentMode::Integrating);
    const glm::vec2 offset_after_hit = hit->collision_offset_xz;

    for (int64_t step = 1; step <= 120; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
    }
    hit = find_target();
    REQUIRE(hit != crowd.vehicles().end());
    REQUIRE(glm::length(hit->collision_offset_xz - offset_after_hit) > 1.0f);
    REQUIRE(glm::length(hit->collision_offset_xz) > 1.0f);
    const LanePose recovery_pose =
        n.lanes.pose(hit->lane, hit->dist_along_m);
    const glm::vec2 recovery_fwd = glm::normalize(
        glm::vec2{recovery_pose.tangent.x, recovery_pose.tangent.z});
    const glm::vec2 recovery_right{-recovery_fwd.y, recovery_fwd.x};
    const float lateral_error =
        glm::dot(hit->collision_offset_xz, recovery_right);
    REQUIRE(lateral_error * glm::dot(
        glm::vec2{hit->fwd.x, hit->fwd.z}, recovery_right) < -0.01f);
    const float displaced = glm::length(hit->collision_offset_xz);
    for (int64_t step = 121; step <= 960; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
    }
    hit = find_target();
    REQUIRE(hit != crowd.vehicles().end());
    REQUIRE(glm::length(hit->collision_offset_xz) < displaced * 0.15f);
    REQUIRE(std::fabs(hit->collision_yaw_rad) < 0.05f);
    pass("a side impact shoves traffic, then the driver steers back in");
}

void collision_recovery_moves_where_the_body_points() {
    RoadSpine spine;
    spine.id = 901u;
    spine.cls = RoadClass::Street;
    spine.points = {{-1200.0f, 0.0f}, {1200.0f, 0.0f}};
    RoadGraph roads;
    roads.build({spine}, RoadGraphParams{}, GroundSampler{});
    LaneGraph lanes;
    lanes.build(roads, GroundSampler{});
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 45.0f;
    CrowdTuning tuning;
    tuning.vehicle_activate_m = 180.0f;
    tuning.vehicle_retire_m = 300.0f;
    tuning.max_peds = 0;
    Crowd crowd;
    crowd.build(lanes, 0x51DE1A7ull, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});
    REQUIRE(!crowd.vehicles().empty());
    const VehicleAgent original = crowd.vehicles().front();
    // This collision fixture needs one unchanged body, not a player taking
    // possession of every other driver (occupied police cars stay locked).
    const_cast<std::vector<VehicleAgent>&>(crowd.vehicles()) = {original};
    REQUIRE(crowd.vehicles().size() == 1u);
    const glm::vec3 right{-original.fwd.z, 0.0f, original.fwd.x};
    VehicleState player;
    player.position = original.pos + right * 1.5f + original.fwd * 1.2f;
    player.velocity = original.fwd * original.speed_mps - right * 12.0f;
    Crowd replay = crowd;
    VehicleState replay_player = player;
    REQUIRE(crowd.resolve_player_collision(player));
    REQUIRE(replay.resolve_player_collision(replay_player));
    REQUIRE(crowd.population_hash() == replay.population_hash());
    Crowd blocked = crowd;
    std::vector<glm::vec2> parked_row;
    for (int i = -3; i <= 3; ++i) {
        const glm::vec3 p = original.pos + original.fwd * 12.0f +
                            right * (static_cast<float>(i) * 3.0f);
        parked_row.push_back({p.x, p.z});
    }
    blocked.set_parked_vehicle_positions(parked_row);
    int steered_steps = 0;
    float peak_body_slip_mps = 0.0f;
    for (int64_t step = 1; step <= 1200; ++step) {
        const VehicleAgent before = crowd.vehicles().front();
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        replay.rebuild_buckets();
        replay.step_vehicles(step);
        REQUIRE(crowd.population_hash() == replay.population_hash());
        REQUIRE(crowd.vehicles().size() == 1u);
        const VehicleAgent& after = crowd.vehicles().front();
        REQUIRE(after.lane_key == original.lane_key);
        REQUIRE(after.slot == original.slot);
        REQUIRE(after.lane == original.lane);
        const glm::vec2 displacement{after.pos.x - before.pos.x,
                                     after.pos.z - before.pos.z};
        REQUIRE(glm::length(displacement) < 0.20f);
        // Keep the physical skid out of this measurement. Once its impulse
        // settles, a recovering driver must travel in the body's direction.
        // The old recovery pointed one way while translating the other.
        if (step < 240 || std::fabs(after.collision_yaw_rad) < 0.04f) continue;
        const glm::vec2 body_forward{after.fwd.x, after.fwd.z};
        const glm::vec2 body_side{-body_forward.y, body_forward.x};
        const glm::vec2 powered_motion = displacement /
            static_cast<float>(kSimDt) - before.collision_velocity_xz;
        peak_body_slip_mps = std::max(peak_body_slip_mps,
                                      std::fabs(glm::dot(powered_motion, body_side)));
        REQUIRE_MSG(std::fabs(glm::dot(powered_motion, body_side)) < 0.12f,
                    "recovery slides sideways relative to the visible body", "heading");
        REQUIRE(glm::dot(powered_motion, body_forward) > 0.0f);
        if (std::fabs(before.collision_yaw_velocity) < 0.001f &&
            std::fabs(after.collision_steer_rad) > 0.005f) {
            const float visible_yaw_change =
                before.fwd.z * after.fwd.x - before.fwd.x * after.fwd.z;
            REQUIRE_MSG(visible_yaw_change * after.collision_steer_rad >= 0.0f,
                        "recovery wheels steer opposite the body's turn", "steering sign");
        }
        ++steered_steps;
    }
    REQUIRE(steered_steps > 60);
    REQUIRE(glm::length(crowd.vehicles().front().collision_offset_xz) < 0.5f);
    std::printf("      peak powered recovery sideslip %.4f m/s\n",
                static_cast<double>(peak_body_slip_mps));
    pass("collision recovery travels forward along its visible heading");

    int stationary_steps = 0;
    for (int64_t step = 1; step <= 1200; ++step) {
        const VehicleAgent before = blocked.vehicles().front();
        blocked.rebuild_buckets();
        blocked.step_vehicles(step);
        const VehicleAgent& after = blocked.vehicles().front();
        if (step < 600 || before.speed_mps > 0.0001f) continue;
        REQUIRE(glm::length(after.pos - before.pos) < 0.0001f);
        REQUIRE(std::fabs(after.collision_yaw_rad - before.collision_yaw_rad) < 0.0001f);
        REQUIRE_NEAR(after.collision_steer_rad, 0.0, 1e-6);
        ++stationary_steps;
    }
    REQUIRE(stationary_steps > 300);
    REQUIRE(glm::length(blocked.vehicles().front().collision_offset_xz) > 0.2f);
    const glm::vec3 stopped_position = blocked.vehicles().front().pos;
    blocked.set_parked_vehicle_positions({});
    for (int64_t step = 1201; step <= 2640; ++step) {
        blocked.rebuild_buckets();
        blocked.step_vehicles(step);
    }
    REQUIRE(glm::distance(blocked.vehicles().front().pos, stopped_position) > 15.0f);
    REQUIRE(glm::length(blocked.vehicles().front().collision_offset_xz) < 0.5f);
    pass("blocked recovery waits in place and drives back when the route clears");
    pass("collision and recovery replay exactly with stable vehicle identities");
}

void rear_impact_becomes_real_lane_progress() {
    const Network n = build_network();
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 34.0f;
    CrowdTuning tuning;
    tuning.vehicle_activate_m = 280.0f;
    tuning.vehicle_retire_m = 360.0f;
    tuning.max_peds = 0;

    Crowd hit_crowd;
    Crowd control_crowd;
    hit_crowd.build(n.lanes, 0xB00B1E5ull, ambient, tuning);
    control_crowd.build(n.lanes, 0xB00B1E5ull, ambient, tuning);
    hit_crowd.refresh(0, {0.0f, 0.0f});
    control_crowd.refresh(0, {0.0f, 0.0f});

    const VehicleAgent* target = nullptr;
    for (const VehicleAgent& agent : hit_crowd.vehicles()) {
        const Lane& lane = n.lanes.lane(agent.lane);
        if (lane.length_m - agent.dist_along_m > 35.0f) {
            target = &agent;
            break;
        }
    }
    REQUIRE(target != nullptr);
    const uint64_t lane_key = target->lane_key;
    const uint32_t slot = target->slot;
    const glm::vec3 fwd = glm::normalize(
        glm::vec3{target->fwd.x, 0.0f, target->fwd.z});

    VehicleState player;
    player.position = target->pos - fwd * 2.90f;
    player.velocity = fwd * (target->speed_mps + 12.0f);
    REQUIRE(hit_crowd.resolve_player_collision(player));

    hit_crowd.rebuild_buckets();
    control_crowd.rebuild_buckets();
    hit_crowd.step_vehicles(1);
    control_crowd.step_vehicles(1);

    auto find_target = [&](const Crowd& crowd) {
        return std::find_if(
            crowd.vehicles().begin(), crowd.vehicles().end(),
            [&](const VehicleAgent& agent) {
                return agent.lane_key == lane_key && agent.slot == slot;
            });
    };
    const auto hit = find_target(hit_crowd);
    const auto control = find_target(control_crowd);
    REQUIRE(hit != hit_crowd.vehicles().end());
    REQUIRE(control != control_crowd.vehicles().end());
    REQUIRE(hit->dist_along_m > control->dist_along_m + 0.02f);
    const glm::vec2 lane_fwd = glm::normalize(
        glm::vec2{hit->fwd.x, hit->fwd.z});
    REQUIRE(std::fabs(glm::dot(hit->collision_offset_xz, lane_fwd)) < 0.01f);
    pass("a rear hit advances the traffic route instead of springing backward");
}

void ai_traffic_bodies_do_not_ghost() {
    CrowdTuning tuning;
    VehicleAgent rear;
    // Start with a deep but still front-to-rear overlap. At exactly 3 m the
    // narrow sedan is farther out through its door than its bumper, so the
    // mathematically shortest SAT escape is sideways and this stops being a
    // rear-impact contract.
    rear.pos = {0.0f, 0.0f, 3.5f};
    rear.fwd = {0.0f, 0.0f, -1.0f};
    rear.speed_mps = 12.0f;
    VehicleAgent front;
    front.pos = {0.0f, 0.0f, 0.0f};
    front.fwd = rear.fwd;
    front.speed_mps = 2.0f;

    const float before = glm::distance(
        glm::vec2{rear.pos.x, rear.pos.z},
        glm::vec2{front.pos.x, front.pos.z});
    const TrafficBodyCollision contact =
        resolve_traffic_body_collision(rear, front, tuning);
    const float after = glm::distance(
        glm::vec2{rear.pos.x, rear.pos.z},
        glm::vec2{front.pos.x, front.pos.z});

    REQUIRE(contact.collided);
    REQUIRE(contact.penetration_m > 1.0f);
    REQUIRE(contact.closing_speed_mps > 9.0f);
    REQUIRE(after > before + 1.0f);
    REQUIRE(glm::dot(rear.collision_velocity_xz,
                     glm::vec2{0.0f, -1.0f}) < 0.0f);
    REQUIRE(glm::dot(front.collision_velocity_xz,
                     glm::vec2{0.0f, -1.0f}) > 0.0f);
    REQUIRE(rear.mode == AgentMode::Integrating);
    REQUIRE(front.mode == AgentMode::Integrating);
    REQUIRE(vehicle_damage_total(rear.body_damage) > 0.0f);
    REQUIRE(vehicle_damage_total(front.body_damage) > 0.0f);
    pass("AI traffic bodies separate and exchange impact velocity");
}

void traffic_damage_corner_mapping_is_not_mirrored() {
    constexpr float kHalfWidth = 0.95f;
    constexpr float kHalfLength = 2.25f;
    const glm::vec2 forwards[] = {
        {0.0f, -1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}};
    const glm::vec2 local_corners[] = {
        {-kHalfWidth, -kHalfLength}, {kHalfWidth, -kHalfLength},
        {-kHalfWidth, kHalfLength}, {kHalfWidth, kHalfLength}};

    // Pin the body-frame conversion at every corner and cardinal heading.
    // local +X is right and local +Z is rear, matching player vehicle damage.
    for (glm::vec2 forward : forwards) {
        const glm::vec2 right{-forward.y, forward.x};
        for (glm::vec2 local : local_corners) {
            const glm::vec2 world_offset =
                right * local.x - forward * local.y;
            const glm::vec3 mapped =
                traffic_body_local_contact(forward, world_offset);
            REQUIRE_NEAR(mapped.x, local.x, 1e-6);
            REQUIRE_NEAR(mapped.z, local.y, 1e-6);
        }
    }

    VehicleDamageState victim;
    const glm::vec3 rear_left = traffic_body_local_contact(
        {0.0f, -1.0f}, {-kHalfWidth, kHalfLength});
    apply_vehicle_impact(victim, rear_left, 30.0f,
                         kHalfWidth, kHalfLength);
    const float left_damage =
        victim.zones[kDamageRearLeft] +
        victim.zones[kDamageSideLeftRear];
    const float right_damage =
        victim.zones[kDamageRearRight] +
        victim.zones[kDamageSideRightRear];
    REQUIRE(left_damage > 0.0f);
    REQUIRE_NEAR(right_damage, 0.0, 1e-6);
    REQUIRE(victim.stamps[0].contact_xz.x < 0.0f);
    REQUIRE(victim.stamps[0].contact_xz.y > 0.0f);
    pass("AI victim rear-left contacts stay rear-left in damage space");
}

void intersection_contact_does_not_rearm_a_long_recovery_pause() {
    CrowdTuning tuning;
    VehicleAgent a;
    VehicleAgent b;
    a.pos = {0.0f, 0.0f, 0.0f};
    b.pos = a.pos;
    a.fwd = {0.0f, 0.0f, -1.0f};
    b.fwd = {1.0f, 0.0f, 0.0f};
    a.speed_mps = 0.5f;
    b.speed_mps = 0.5f;
    a.committed_junction = 7;
    b.committed_junction = 7;

    for (int i = 0; i < 8; ++i) {
        a.pos = {0.0f, 0.0f, 0.0f};
        b.pos = a.pos;
        const TrafficBodyCollision contact =
            resolve_traffic_body_collision(a, b, tuning);
        REQUIRE(contact.collided);
        REQUIRE(contact.closing_speed_mps <
                tuning.intersection_low_speed_yaw_threshold_mps);
        REQUIRE(a.collision_recovery_seconds <=
                tuning.intersection_collision_recovery_delay_s + 1e-5f);
        REQUIRE(b.collision_recovery_seconds <=
                tuning.intersection_collision_recovery_delay_s + 1e-5f);
    }
    REQUIRE_NEAR(a.collision_yaw_velocity, 0.0, 1e-6);
    REQUIRE_NEAR(b.collision_yaw_velocity, 0.0, 1e-6);
    pass("low-speed intersection contact cannot freeze or spin a committed car");
}

void signal_box_admits_a_bounded_number_of_cars() {
    const Network n = build_network();
    const uint32_t junction = junction_at(n.lanes, {0.0f, 0.0f});
    REQUIRE(junction < n.lanes.junction_count());

    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 18.0f;
    ambient.max_vehicle_slots = 20;
    CrowdTuning tuning;
    tuning.vehicle_activate_m = 320.0f;
    tuning.vehicle_retire_m = 400.0f;
    tuning.max_peds = 0;
    // Accelerate the watchdog clock in this stress fixture so its committed
    // clearance path is exercised without adding seconds to the suite.
    tuning.intersection_escape_after_steps = 12;

    Crowd crowd;
    crowd.build(n.lanes, 0x1A7E25EC7ull, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});
    REQUIRE(crowd.vehicles().size() > 20u);

    std::size_t max_claimed = 0;
    std::size_t candidate_pairs = 0;
    std::size_t contacts_resolved = 0;
    std::size_t box_holds = 0;
    std::size_t reroutes = 0;
    std::size_t max_long_stalls = 0;
    std::size_t escape_steps = 0;
    int64_t max_intersection_stall_steps = 0;
    bool saw_claim = false;
    using GreenMovement = std::tuple<int64_t, LaneRef, LaneRef>;
    std::map<GreenMovement, std::set<uint64_t>> green_platoons;
    for (int64_t step = 0; step <= tuning.signal_period_steps * 2; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        candidate_pairs += crowd.stats().ai_collision_pairs;
        contacts_resolved += crowd.stats().ai_collisions;
        box_holds += crowd.stats().junction_box_holds;
        reroutes += crowd.stats().jam_reroutes;
        max_long_stalls = std::max(max_long_stalls,
                                   crowd.stats().stalled_vehicles);
        escape_steps += crowd.stats().intersection_escapes;
        std::size_t claimed = 0;
        for (const VehicleAgent& agent : crowd.vehicles()) {
            if (agent.committed_junction == junction) {
                ++claimed;
                if (n.lanes.valid(agent.committed_approach_lane) &&
                    n.lanes.valid(agent.committed_exit_lane) &&
                    traffic_signal_phase(
                        n.lanes, junction, agent.committed_approach_lane,
                        step, tuning) == TrafficSignalPhase::Green) {
                    const int64_t cycle = step / tuning.signal_period_steps;
                    const GreenMovement movement{
                        cycle, agent.committed_approach_lane,
                        agent.committed_exit_lane};
                    green_platoons[movement].insert(
                        agent.lane_key ^
                        (static_cast<uint64_t>(agent.slot) << 32));
                }
            }
            max_intersection_stall_steps = std::max(
                max_intersection_stall_steps, agent.intersection_stall_steps);
        }
        max_claimed = std::max(max_claimed, claimed);
        saw_claim = saw_claim || claimed > 0u;
    }
    REQUIRE(saw_claim);
    std::size_t max_green_platoon = 0;
    for (const auto& entry : green_platoons)
        max_green_platoon = std::max(max_green_platoon, entry.second.size());
    // Claims now cover the whole geometry-derived box, so departing tails can
    // overlap later same-phase admissions. The count is telemetry rather than
    // a conflict test; collision and red-stop placement are pinned separately.
    std::printf("      max simultaneous central-junction claims: %zu\n",
                max_claimed);
    std::printf("      largest same-movement platoon in one green: %zu\n",
                max_green_platoon);
    std::printf("      AI broadphase pairs %zu, contacts resolved %zu\n",
                candidate_pairs, contacts_resolved);
    std::printf("      blocked-box holds %zu, jam reroutes %zu, max long stalls %zu\n",
                box_holds, reroutes, max_long_stalls);
    std::printf("      escape-throttle steps %zu, longest box stall %.2f s\n",
                escape_steps,
                static_cast<double>(max_intersection_stall_steps) / 120.0);
    REQUIRE(candidate_pairs > 0u);
    // This fixture injects no impact. Good junction negotiation should avoid
    // contacts; the explicit collision tests above exercise solver response.
    REQUIRE(contacts_resolved == 0u);
    REQUIRE(box_holds > 0u);
    REQUIRE(reroutes > 0u);
    REQUIRE(max_claimed <= 8u);
    REQUIRE(max_green_platoon >= 2u);
    REQUIRE(max_long_stalls < crowd.vehicles().size() / 4u);
    REQUIRE(max_intersection_stall_steps < tuning.signal_period_steps / 2);
    REQUIRE(escape_steps > 0u);
    pass("the signal box stays bounded even under stress-test density");
}

void junction_negotiation_keeps_the_network_moving() {
    const Network n = build_network();
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 24.0f;
    ambient.max_vehicle_slots = 12;

    CrowdTuning tuning;
    tuning.vehicle_activate_m = 300.0f;
    tuning.vehicle_retire_m = 380.0f;
    tuning.max_peds = 0;

    Crowd crowd;
    crowd.build(n.lanes, 0xBEEFBEEFull, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});
    REQUIRE(crowd.vehicles().size() > 8u);

    uint64_t decisions_before = 0;
    for (const VehicleAgent& agent : crowd.vehicles())
        decisions_before += agent.decisions;

    // Two complete signal periods catches the easy failure mode where each
    // approach yields to the other and the whole fixture remains parked.
    const int64_t end_step = tuning.signal_period_steps * 2;
    for (int64_t step = 0; step <= end_step; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
    }

    uint64_t decisions_after = 0;
    std::size_t moving = 0;
    for (const VehicleAgent& agent : crowd.vehicles()) {
        decisions_after += agent.decisions;
        if (agent.speed_mps > 1.0f) ++moving;
    }
    REQUIRE(decisions_after > decisions_before);
    REQUIRE(moving > crowd.vehicles().size() / 10u);
    pass("junction negotiation clears cars through multiple signal cycles");
}

void real_city_intersections_do_not_keep_stalled_claimants() {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), RoadGraphParams{}, ground.sampler());
    LaneGraph lanes;
    lanes.build(roads, ground.sampler(), LaneBuildParams{});

    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 48.0f;
    ambient.max_vehicle_slots = 16;
    CrowdTuning tuning;
    tuning.max_peds = 0;

    Crowd crowd;
    crowd.build(lanes, city::kMapSeed, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});
    REQUIRE(crowd.vehicles().size() > 100u);

    std::vector<float> clearances(lanes.junction_count(), 8.0f);
    for (uint32_t j = 0; j < lanes.junction_count(); ++j) {
        float widest = 0.0f;
        const LaneJunction& junction = lanes.junction(j);
        for (LaneRef lane : junction.incoming) {
            if (lanes.valid(lane))
                widest = std::max(widest, lanes.lane(lane).width_m);
        }
        for (LaneRef lane : junction.outgoing) {
            if (lanes.valid(lane))
                widest = std::max(widest, lanes.lane(lane).width_m);
        }
        clearances[j] = traffic_junction_clearance(
            widest, tuning.traffic_half_length_m,
            tuning.junction_storage_margin_m,
            std::max(tuning.stop_line_m, tuning.junction_clear_m));
    }

    uint64_t decisions_before = 0;
    for (const VehicleAgent& car : crowd.vehicles())
        decisions_before += car.decisions;

    int64_t longest_box_stall = 0;
    std::size_t escape_steps = 0;
    std::size_t contacts = 0;
    std::size_t red_stops_inside_box = 0;
    float closest_red_stop_slack = std::numeric_limits<float>::infinity();
    const int64_t end_step = tuning.signal_period_steps * 3;
    for (int64_t step = 0; step <= end_step; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        escape_steps += crowd.stats().intersection_escapes;
        contacts += crowd.stats().ai_collisions;
        for (const VehicleAgent& car : crowd.vehicles()) {
            longest_box_stall = std::max(
                longest_box_stall, car.intersection_stall_steps);
            if (!lanes.valid(car.lane) ||
                car.committed_junction != 0xFFFFFFFFu ||
                car.speed_mps >= 0.5f)
                continue;
            const Lane& lane = lanes.lane(car.lane);
            const uint32_t junction = lane.junction_to;
            if (junction >= clearances.size() ||
                lanes.junction_control(junction) != JunctionControl::Signal ||
                traffic_signal_phase(lanes, junction, car.lane, step,
                                     tuning) != TrafficSignalPhase::Red)
                continue;
            const float stop_slack = lane.length_m - car.dist_along_m -
                                     clearances[junction];
            closest_red_stop_slack = std::min(closest_red_stop_slack,
                                              stop_slack);
            if (stop_slack < -0.25f) ++red_stops_inside_box;
        }
    }

    uint64_t decisions_after = 0;
    for (const VehicleAgent& car : crowd.vehicles())
        decisions_after += car.decisions;
    std::printf("      real city: %zu cars, %llu lane transitions, %zu contacts, "
                "longest box stall %.2f s, %zu escape steps, red stop slack "
                "%.2f m\n",
                crowd.vehicles().size(),
                static_cast<unsigned long long>(decisions_after -
                                                decisions_before),
                contacts, static_cast<double>(longest_box_stall) / 120.0,
                escape_steps, closest_red_stop_slack);
    REQUIRE(decisions_after > decisions_before + 100u);
    REQUIRE(longest_box_stall < tuning.intersection_escape_after_steps * 2);
    REQUIRE(red_stops_inside_box == 0u);
    pass("the authored city clears committed traffic through three signal cycles");
}

void leaking_traffic_coasts_to_a_persistent_stop() {
    RoadSpine spine;spine.id=907;spine.cls=RoadClass::Street;
    spine.points={{-1200,0},{1200,0}};
    RoadGraph roads;roads.build({spine},RoadGraphParams{},GroundSampler{});
    LaneGraph lanes;lanes.build(roads,GroundSampler{});
    // This fixture exercises civilian ownership transfer after an engine
    // failure. Patrol cars retain their officer and are deliberately locked.
    CrowdTuning tuning;tuning.max_peds=0;tuning.police.patrol_fraction=0.0f;
    Crowd crowd;crowd.build(lanes,0xF1A1ull,AmbientTuning{},tuning);
    crowd.refresh(0,{0,0});
    REQUIRE(!crowd.vehicles().empty());
    const auto population=crowd.vehicles();
    for(std::size_t i=1;i<population.size();++i) {
        VehicleAgent removed;
        REQUIRE(crowd.take_vehicle(population[i].lane_key,population[i].slot,removed));
    }
    // Fixture starts at the end of an existing leak, then exercises the real
    // traffic update and possession transfer rather than waiting minutes.
    auto& car=const_cast<VehicleAgent&>(crowd.vehicles().front());
    car.mode=AgentMode::Integrating;car.speed_mps=5;
    car.body_damage.zones[kDamageFrontCenter]=1;
    car.mechanical.oil_remaining=.001f;car.mechanical.oil_lifetime_s=45;
    Crowd replay=crowd;
    bool saw_coast=false;
    for(int step=0;step<1200;++step) {
        crowd.rebuild_buckets();crowd.step_vehicles(step);
        replay.rebuild_buckets();replay.step_vehicles(step);
        REQUIRE(crowd.population_hash()==replay.population_hash());
        const auto& actual=crowd.vehicles().front();
        saw_coast|=vehicle_engine_failed(actual.mechanical) && actual.speed_mps>.5f;
    }
    REQUIRE(saw_coast);
    REQUIRE(vehicle_engine_failed(crowd.vehicles().front().mechanical));
    REQUIRE(crowd.vehicles().front().speed_mps<.01f);
    VehicleAgent taken;
    REQUIRE(crowd.take_vehicle(car.lane_key,car.slot,taken));
    REQUIRE(vehicle_engine_failed(taken.mechanical));
    REQUIRE(taken.mechanical.oil_remaining==0);
    REQUIRE(taken.mechanical.oil_lifetime_s==45);
    pass("leaking traffic coasts to a persistent deterministic engine-out stop");
}

}  // namespace

int main() {
    leaking_traffic_coasts_to_a_persistent_stop();
    pedestrian_paths_follow_pavements_and_junction_mouths();
    pedestrian_bends_and_dead_ends_are_continuous();
    pedestrian_crossings_clear_acute_junctions_and_reject_tiny_roads();
    pedestrians_make_deterministic_progress_without_teleports();
    std::printf("traffic_runtime_tests\n");
    {
        const Network n=build_network();
        Crowd crowd;
        AmbientTuning ambient;
        CrowdTuning tuning;
        tuning.police.patrol_fraction = 0.0f;  // civilian possession fixture
        crowd.build(n.lanes,0xCAFE1234ull,ambient,tuning);
        crowd.refresh(0,{0,0});
        REQUIRE(!crowd.vehicles().empty());
        const auto source=crowd.vehicles().front();
        const auto count=crowd.vehicles().size();
        VehicleAgent taken;
        REQUIRE(crowd.take_vehicle(source.lane_key,source.slot,taken));
        REQUIRE(crowd.vehicles().size()==count-1);
        REQUIRE(taken.pos==source.pos && taken.speed_mps==source.speed_mps);
        REQUIRE(!crowd.take_vehicle(source.lane_key,source.slot,taken));
        for (int step=0;step<360;++step) {
            if (step%30==0) crowd.refresh(step,{source.pos.x,source.pos.z});
            crowd.rebuild_buckets();crowd.step_vehicles(step);
            for (const auto& v:crowd.vehicles())
                REQUIRE(v.lane_key!=source.lane_key || v.slot!=source.slot);
        }
        pass("taken traffic identity does not respawn after refresh");
    }
    signal_cycle_is_shared_and_opposed();
    junction_turns_have_continuous_heading();
    stop_signs_require_a_real_dwell();
    junction_right_of_way_has_one_winner();
    intersection_entry_requires_downstream_storage();
    nearby_traffic_moves_and_is_solid();
    player_uses_the_full_chassis_footprint();
    damaged_player_nose_does_not_keep_an_invisible_bumper();
    traffic_brakes_before_hitting_the_player();
    traffic_cars_absorb_side_impact_and_recover();
    collision_recovery_moves_where_the_body_points();
    rear_impact_becomes_real_lane_progress();
    ai_traffic_bodies_do_not_ghost();
    traffic_damage_corner_mapping_is_not_mirrored();
    intersection_contact_does_not_rearm_a_long_recovery_pause();
    signal_box_admits_a_bounded_number_of_cars();
    junction_negotiation_keeps_the_network_moving();
    real_city_intersections_do_not_keep_stalled_claimants();
    return apricot_test::done("traffic_runtime_tests");
}
