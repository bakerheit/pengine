#include <cassert>
#include <cmath>
#include <random>

#include "game/traffic_ai.h"
#include "world/road_grid.h"

using namespace pengine;

namespace {

void load_test_cells(RoadGraph& graph) {
    graph.add_cell({0, 0});
    graph.add_cell({1, 0});
    graph.add_cell({0, 1});
    graph.add_cell({-1, 0});
    graph.add_cell({0, -1});
}

void test_lane_geometry() {
    RoadGraph graph;
    load_test_cells(graph);
    TrafficLaneGraph lanes(graph);

    LaneId east{0, 0, GridDir::East};
    TrafficLane lane = lanes.lane(east, 2.f);
    assert(lanes.lane_loaded(east));
    assert(std::abs(lane.length - ROAD_PITCH) < 0.001f);
    assert(std::abs(lane.stop_distance - (ROAD_PITCH - 6.f)) < 0.001f);
    assert(std::abs(lane.start.x - 0.f) < 0.001f);
    assert(std::abs(lane.start.z + 2.f) < 0.001f);
    assert(std::abs(lane.end.x - ROAD_PITCH) < 0.001f);
}

void test_turn_links() {
    RoadGraph graph;
    load_test_cells(graph);
    TrafficLaneGraph lanes(graph);

    std::array<TrafficTurnLink, 4> links;
    int n = lanes.outgoing(LaneId{0, 0, GridDir::East}, links);
    assert(n >= 3);

    bool saw_straight = false;
    bool saw_left = false;
    bool saw_right = false;
    bool saw_uturn = false;
    for (int k = 0; k < n; ++k) {
        TrafficTurnKind kind = links[static_cast<std::size_t>(k)].kind;
        saw_straight = saw_straight || kind == TrafficTurnKind::Straight;
        saw_left = saw_left || kind == TrafficTurnKind::Left;
        saw_right = saw_right || kind == TrafficTurnKind::Right;
        saw_uturn = saw_uturn || kind == TrafficTurnKind::UTurn;
    }
    assert(saw_straight);
    assert(saw_left);
    assert(saw_right);
    assert(!saw_uturn);
}

void test_route_and_driver_math() {
    RoadGraph graph;
    load_test_cells(graph);
    TrafficLaneGraph lanes(graph);
    std::mt19937 rng{123u};

    TrafficRoute route = lanes.make_route(LaneId{0, 0, GridDir::East}, 6, rng);
    assert(route.lanes.size() >= 2);
    for (const LaneId& lane : route.lanes)
        assert(lanes.lane_loaded(lane));

    DriverProfile cautious = make_driver_profile(DriverProfileKind::Cautious);
    DriverProfile aggressive =
        make_driver_profile(DriverProfileKind::AggressiveLite);
    assert(cautious.headway > aggressive.headway);
    assert(traffic_follow_speed_for_gap(2.f, cautious) == 0.f);
    assert(traffic_follow_speed_for_gap(20.f, aggressive)
           > traffic_follow_speed_for_gap(20.f, cautious));
    assert(traffic_should_stop_for_yellow(30.f, 8.f, cautious));
    assert(!traffic_should_stop_for_yellow(30.f, 8.f, aggressive));
    assert(!traffic_profile_may_pass_jam(cautious, 60.f));
    assert(!traffic_profile_may_pass_jam(
        make_driver_profile(DriverProfileKind::Impatient), 1.f));
    assert(traffic_profile_may_pass_jam(
        make_driver_profile(DriverProfileKind::Impatient), 4.f));
    assert(traffic_profile_may_pass_jam(aggressive, 3.f));
}

} // namespace

int main() {
    test_lane_geometry();
    test_turn_links();
    test_route_and_driver_math();
    return 0;
}
