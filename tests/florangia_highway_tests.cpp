#include <algorithm>
#include <cmath>
#include <cstdio>

#include "city/florangia_roads.h"
#include "city/florangia_airport.h"
#include "city/map.h"
#include "city/roads.h"
#include "city/spines.h"
#include "physics/terrain_collider.h"
#include "road/lane_graph.h"
#include "road/ribbon.h"
#include "terrain/heightmap.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool node_has(const RoadGraph& roads, uint32_t node, uint32_t spine_id) {
    for (const uint32_t edge : roads.node(node).edges)
        if (roads.edge(edge).spine_id == spine_id) return true;
    return false;
}

struct Network {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    RibbonBake bake;
    TerrainCollider collider{city::kMapSeed};
    LaneGraph lanes;

    Network() {
        roads.build(city::map_spines(), {}, ground.sampler());
        bake = bake_ribbons(roads, ground.sampler());
        collider.set_road_collision(build_road_collision(bake));
        lanes.build(roads, ground.sampler(), {.drive_on_right = true});
    }
};

const city::Road& authored_road(uint32_t id) {
    for (const city::Road& road : city::kRoads)
        if (road.id == id) return road;
    REQUIRE(false);
    return city::kRoads[0];
}

uint32_t airport_junction(const Network& n) {
    uint32_t found = 0;
    int matches = 0;
    for (uint32_t node = 0; node < n.roads.node_count(); ++node) {
        if (node_has(n.roads, node, city::kFlorangiaHighwayRoadId) &&
            node_has(n.roads, node, city::kFlorangiaAirportSpurRoadId)) {
            found = node;
            ++matches;
        }
    }
    REQUIRE(matches == 1);
    return found;
}

void authored_route_is_dry_and_truck_grade() {
    const city::Road& highway = authored_road(city::kFlorangiaHighwayRoadId);
    const city::Road& spur = authored_road(city::kFlorangiaAirportSpurRoadId);
    REQUIRE(highway.cls == city::RoadClass::Arterial);
    REQUIRE(highway.count == static_cast<int>(city::kFlorangiaHighwayPath.size()));
    REQUIRE(highway.length_m() > 4000.0f);
    REQUIRE(highway.max_grade() < 0.01f);
    REQUIRE(highway.shapes_ground);
    REQUIRE(spur.shapes_ground);

    float weakest_mask = 1.0f;
    float worst_bed_error = 0.0f;
    for (int segment = 0; segment + 1 < highway.count; ++segment) {
        for (int sample = 0; sample <= 20; ++sample) {
            const float t = static_cast<float>(sample) / 20.0f;
            const float x = highway.path[segment].x +
                            (highway.path[segment + 1].x - highway.path[segment].x) * t;
            const float z = highway.path[segment].z +
                            (highway.path[segment + 1].z - highway.path[segment].z) * t;
            const float bed = highway.path[segment].y +
                              (highway.path[segment + 1].y - highway.path[segment].y) * t;
            weakest_mask = std::min(weakest_mask,
                                    florangia_mask(city::kMapSeed, x, z));
            const float access_dx = x - city::kFlorangiaAirportAccessNode.x;
            const float access_dz = z - city::kFlorangiaAirportAccessNode.z;
            // The spur's own 6.5 m Grade composes last inside its short
            // junction apron. Outside that shared surface the highway profile
            // must remain exact.
            if (access_dx * access_dx + access_dz * access_dz > 80.0f * 80.0f) {
                worst_bed_error = std::max(
                    worst_bed_error,
                    std::fabs(height_at(city::kMapSeed, x, z) - bed));
            }
        }
    }
    REQUIRE_MSG(weakest_mask > 0.55f,
                "Florangia Highway leaves dependable dry land",
                "state highway placement");
    REQUIRE_MSG(worst_bed_error < 0.002f,
                "Florangia terrain blend overwrote the highway Grade corridor",
                "state highway elevation");
    REQUIRE_NEAR(height_at(city::kMapSeed,
                           city::kFlorangiaAirportAccessNode.x,
                           city::kFlorangiaAirportAccessNode.z),
                 city::kFlorangiaAirportRoadBedM, 0.002f);
    std::printf("  highway %.0f m; weakest land mask %.3f; max grade %.2f%%; "
                "worst bed error %.4f m\n",
                highway.length_m(), weakest_mask, highway.max_grade() * 100.0f,
                worst_bed_error);
    apricot_test::pass("Florangia Highway follows dry land on a truck-safe grade");
}

void highway_stays_out_of_the_runway_protection_area() {
    const city::Road& highway = authored_road(city::kFlorangiaHighwayRoadId);
    constexpr float runway_min_x = city::kFlorangiaAirportSite.origin.x - 500.0f;
    constexpr float runway_max_x = city::kFlorangiaAirportSite.origin.x + 500.0f;
    constexpr float runway_centre_z = city::kFlorangiaAirportSite.origin.z - 105.0f;
    constexpr float runway_min_z = runway_centre_z - 24.0f;
    constexpr float runway_max_z = runway_centre_z + 24.0f;
    float closest = 1.0e9f;
    for (int segment = 0; segment + 1 < highway.count; ++segment) {
        for (int sample = 0; sample <= 100; ++sample) {
            const float t = static_cast<float>(sample) / 100.0f;
            const float x = highway.path[segment].x +
                            (highway.path[segment + 1].x - highway.path[segment].x) * t;
            const float z = highway.path[segment].z +
                            (highway.path[segment + 1].z - highway.path[segment].z) * t;
            const float dx = x < runway_min_x ? runway_min_x - x
                           : x > runway_max_x ? x - runway_max_x : 0.0f;
            const float dz = z < runway_min_z ? runway_min_z - z
                           : z > runway_max_z ? z - runway_max_z : 0.0f;
            closest = std::min(closest, std::sqrt(dx * dx + dz * dz) -
                                            highway.ribbon_half_m());
        }
    }
    REQUIRE_MSG(closest > 250.0f,
                "highway ribbon enters Florangia runway protection area",
                "airport/highway separation");
    std::printf("  highway asphalt stays %.0f m clear of runway 08-26\n", closest);
    apricot_test::pass("highway skirts the airport west and south without crossing the runway");
}

void airport_access_is_one_signalised_connected_tee(const Network& n) {
    const uint32_t junction = airport_junction(n);
    const RoadNode& node = n.roads.node(junction);
    REQUIRE_NEAR(node.pos.x, city::kFlorangiaAirportAccessNode.x, 0.01f);
    REQUIRE_NEAR(node.pos.y, city::kFlorangiaAirportAccessNode.z, 0.01f);
    REQUIRE(node.edges.size() == 3);
    REQUIRE(n.lanes.junction_control(junction) == JunctionControl::Signal);

    LaneRef highway_in = kInvalidLane;
    LaneRef highway_out = kInvalidLane;
    LaneRef terminal_in = kInvalidLane;
    LaneRef terminal_out = kInvalidLane;
    for (LaneRef ref = 0; ref < n.lanes.lane_count(); ++ref) {
        const Lane& lane = n.lanes.lane(ref);
        const uint32_t id = n.roads.edge(lane.edge).spine_id;
        if (id == city::kFlorangiaHighwayRoadId) {
            if (lane.junction_to == junction) highway_in = ref;
            if (lane.junction_from == junction) highway_out = ref;
        } else if (id == city::kFlorangiaAirportSpurRoadId) {
            if (lane.junction_to == junction) terminal_in = ref;
            if (lane.junction_from == junction) terminal_out = ref;
        }
    }
    REQUIRE(n.lanes.valid(highway_in));
    REQUIRE(n.lanes.valid(highway_out));
    REQUIRE(n.lanes.valid(terminal_in));
    REQUIRE(n.lanes.valid(terminal_out));
    REQUIRE(!n.lanes.plan_route(highway_in, terminal_out).empty());
    REQUIRE(!n.lanes.plan_route(terminal_in, highway_out).empty());
    apricot_test::pass("airport spur is one two-way, signalised highway T junction");
}

void right_side_lanes_and_collision_agree(const Network& n) {
    std::size_t highway_edges = 0;
    std::size_t lane_samples = 0;
    float worst_delta = 0.0f;
    for (uint32_t edge = 0; edge < n.roads.edge_count(); ++edge) {
        const RoadEdge& road = n.roads.edge(edge);
        if (road.spine_id != city::kFlorangiaHighwayRoadId) continue;
        ++highway_edges;
        const std::vector<LaneRef> refs = n.lanes.lanes_of_edge(edge);
        REQUIRE(refs.size() == 4);
        for (const LaneRef ref : refs) {
            const Lane& lane = n.lanes.lane(ref);
            REQUIRE(lane.lateral_offset_m > 0.0f);
            const LaneRef opposing = n.lanes.opposing(ref);
            REQUIRE(n.lanes.valid(opposing));
            REQUIRE(n.lanes.lane(opposing).forward != lane.forward);
            for (float d = 8.0f; d < n.lanes.length(ref) - 8.0f; d += 20.0f) {
                const LanePose pose = n.lanes.pose(ref, d);
                const TerrainCollider::GroundHit hit = n.collider.probe_down(
                    pose.position + glm::vec3{0.0f, 1.0f, 0.0f}, 2.0f);
                REQUIRE(hit.hit && hit.road);
                worst_delta = std::max(worst_delta,
                                       std::fabs(hit.point.y - pose.position.y));
                ++lane_samples;
            }
        }
    }
    REQUIRE(highway_edges == 2);
    REQUIRE(lane_samples > 500);
    std::printf("  %zu right-side lane samples; worst lane/collision delta %.4f m\n",
                lane_samples, worst_delta);
    REQUIRE(worst_delta < 0.10f);
    apricot_test::pass("four highway lanes use right-side travel on solid asphalt");
}

void airport_turn_sweeps_clear_the_junction_plate(const Network& n) {
    const uint32_t junction = airport_junction(n);
    std::size_t sweeps = 0;
    for (const LaneRef incoming : n.lanes.junction(junction).incoming) {
        const LanePose from = n.lanes.pose(incoming, n.lanes.length(incoming));
        for (const TurnLink& turn : n.lanes.outgoing(incoming)) {
            if (turn.junction != junction) continue;
            const LanePose to = n.lanes.pose(turn.to, 0.0f);
            for (int step = 0; step <= 12; ++step) {
                const float t = static_cast<float>(step) / 12.0f;
                const glm::vec3 centre = from.position + (to.position - from.position) * t;
                glm::vec3 tangent = glm::mix(from.tangent, to.tangent, t);
                tangent.y = 0.0f;
                tangent = glm::normalize(tangent);
                const glm::vec3 right{tangent.z, 0.0f, -tangent.x};
                // 4.8 x 1.8 m passenger-car envelope, sampled along the
                // traffic movement rather than checking the node as a point.
                for (const float longitudinal : {-2.4f, 2.4f}) {
                    for (const float lateral : {-0.9f, 0.9f}) {
                        const glm::vec3 p = centre + tangent * longitudinal +
                                            right * lateral;
                        const auto hit = n.collider.probe_down(
                            p + glm::vec3{0.0f, 1.0f, 0.0f}, 2.0f);
                        REQUIRE(hit.hit && hit.road);
                    }
                }
            }
            ++sweeps;
        }
    }
    REQUIRE(sweeps >= 6);
    std::printf("  %zu airport-junction vehicle envelopes clear asphalt\n", sweeps);
    apricot_test::pass("representative vehicle sweeps clear the airport junction");
}

}  // namespace

int main() {
    authored_route_is_dry_and_truck_grade();
    highway_stays_out_of_the_runway_protection_area();
    const Network network;
    airport_access_is_one_signalised_connected_tee(network);
    right_side_lanes_and_collision_agree(network);
    airport_turn_sweeps_clear_the_junction_plate(network);
    return apricot_test::done("florangia_highway_tests");
}
