#include <algorithm>
#include <cstdio>
#include <set>

#include "app/road_fixture_layout.h"
#include "city/spines.h"
#include "city/map.h"
#include "road_fixture.h"
#include "test_assert.h"

using namespace apricot;

namespace {

void signal_frames() {
    const glm::vec3 up{0, 1, 0};
    for (int i = 0; i < 32; ++i) {
        const float angle = static_cast<float>(i) * 0.19634954085f;
        const glm::vec3 dir{std::cos(angle), 0, std::sin(angle)};
        const glm::vec3 right = glm::cross(dir, up);
        const glm::vec3 junction{120, 8, -90};
        const auto layout = make_traffic_signal_layout(junction, dir, 7, 11,
            3.5f, kFixtureSideClearanceM, kFixtureSideClearanceM);
        const auto matrix = glm::mat3_cast(layout.rotation);
        REQUIRE_NEAR(glm::determinant(matrix), 1, 1e-5);
        REQUIRE(glm::dot(matrix * glm::vec3{0, 0, 1}, -dir) > 0.9999f);
        REQUIRE(glm::dot(matrix * glm::vec3{1, 0, 0}, right) > 0.9999f);
        REQUIRE(glm::dot(matrix * up, up) > 0.9999f);
        REQUIRE_NEAR(glm::dot(layout.pole_ground - junction, right),
                     7 + kFixtureSideClearanceM, 2e-5);
        REQUIRE_NEAR(glm::dot(layout.arm_end - junction, right), 3.5f, 2e-5);
        REQUIRE(glm::dot(layout.pole_ground - junction, dir) < -14.0f);
        REQUIRE(glm::dot(layout.arm_end - layout.pole_top, right) < 0.0f);
        REQUIRE_NEAR(glm::length(layout.arm_end - layout.pole_top),
                     layout.arm_length_m, 2e-5);
        // Incoming cars approach the lens front; the back faces departing cars.
        const glm::vec3 driver = junction - dir * 40.0f + right * 3.5f + up;
        REQUIRE(glm::dot(glm::normalize(driver - layout.arm_end), layout.facing) > 0.95f);

        const LanePose pose{junction + right * 3.5f, dir, right};
        for (bool right_side : {false, true}) {
            const auto lamp = make_street_lamp_layout(pose, 3.5f, 14, right_side);
            const float sign = right_side ? 1.0f : -1.0f;
            REQUIRE_NEAR(glm::dot(lamp.pole_ground - junction, dir), 0, 2e-5);
            REQUIRE_NEAR(glm::dot(lamp.pole_ground - junction, right),
                         sign * (7 + kFixtureSideClearanceM), 2e-5);
            REQUIRE_NEAR(glm::dot(lamp.arm_end - junction, right), sign * 6, 2e-5);
        }
    }
    const auto invalid = make_traffic_signal_layout({}, {}, 7, 11, 3.5f);
    REQUIRE(invalid.arm_length_m == 0.0f);
    apricot_test::pass("32 rotated approaches including both travel directions: proper frame, curb side, lane target, driver facing");
}

void spacing() {
    REQUIRE(street_lamp_spacing(RoadClass::Freeway) == 0);
    REQUIRE(street_lamp_spacing(RoadClass::Alley) == 0);
    REQUIRE(street_lamp_spacing(RoadClass::Dirt) == 0);
    REQUIRE(street_lamp_stations(55, 64, 25, 25).empty());
    REQUIRE(street_lamp_stations(500, 0, 25, 25).empty());
    for (float gap : {64.0f, 88.0f}) {
        const auto stations = street_lamp_stations(400, gap, 29, 25);
        REQUIRE(stations.size() >= 3);
        REQUIRE(stations.front() >= 29);
        REQUIRE(stations.back() <= 375);
        for (std::size_t i = 1; i < stations.size(); ++i)
            REQUIRE_NEAR(stations[i] - stations[i - 1], gap, 1e-5);
        REQUIRE(stations == street_lamp_stations(400, gap, 29, 25));
    }
    apricot_test::pass("deterministic spacing, end clearances, short fragments and unsupported road classes");
}

LaneGraph build_lanes(const std::vector<RoadSpine>& spines) {
    RoadGraph graph;
    graph.build(spines, RoadGraphParams{}, GroundSampler{});
    LaneGraph lanes;
    lanes.build(graph, GroundSampler{});
    return lanes;
}

void network_placement() {
    auto spines = make_test_spines();
    const LaneGraph lanes = build_lanes(spines);
    bool checked_forward = false;
    bool checked_reverse = false;
    for (uint32_t j = 0; j < lanes.junction_count(); ++j) {
        if (lanes.junction_control(j) != JunctionControl::Signal) continue;
        for (LaneRef r : lanes.junction(j).incoming) {
            const auto& lane = lanes.lane(r);
            const auto signal = make_lane_signal_layout(lanes, j, r);
            const auto pose = lanes.pose(r, lane.length_m);
            REQUIRE(glm::dot(signal.facing, pose.tangent) < -0.9999f);
            REQUIRE(glm::dot(signal.pole_ground - lanes.junction(j).pos,
                             pose.right) > lane.width_m * 0.5f + kSidewalkWidthM);
            REQUIRE_NEAR(glm::dot(signal.arm_end - lanes.junction(j).pos,
                                  pose.right), lane.width_m * 0.25f, 1e-4);
            checked_forward = checked_forward || lane.forward;
            checked_reverse = checked_reverse || !lane.forward;
        }
    }
    REQUIRE(checked_forward && checked_reverse);
    const auto lamps = build_street_lamp_layouts(lanes);
    REQUIRE(!lamps.empty());
    std::reverse(spines.begin(), spines.end());
    const auto reordered = build_street_lamp_layouts(build_lanes(spines));
    REQUIRE(lamps.size() == reordered.size());
    for (std::size_t i = 0; i < lamps.size(); ++i) {
        REQUIRE(lamps[i].lane_key == reordered[i].lane_key);
        REQUIRE(glm::length(lamps[i].pole_ground - reordered[i].pole_ground) < 1e-4f);
        for (std::size_t j = i + 1; j < lamps.size(); ++j)
            REQUIRE(glm::length(lamps[i].pole_ground - lamps[j].pole_ground) >=
                    kStreetLampMinSeparationM - 1e-4f);
    }
    RoadSpine airport;
    airport.id = 91;
    airport.cls = RoadClass::Street;
    airport.points = {{-200, 2046}, {550, 2046}};
    REQUIRE(build_street_lamp_layouts(build_lanes({airport})).empty());
    apricot_test::pass("real lane producer: stable under spine reorder, no duplicate junction fixtures, airport exclusion");
}

void map_qa_positions() {
    const TerrainGround ground{city::kMapSeed};
    RoadGraph graph;
    graph.build(city::map_spines(), RoadGraphParams{}, ground.sampler());
    LaneGraph lanes;
    lanes.build(graph, ground.sampler());
    const auto signals = build_traffic_signal_layouts(lanes);
    for (std::size_t a = 0; a < signals.size(); ++a)
        for (std::size_t b = a + 1; b < signals.size(); ++b)
            REQUIRE(!traffic_signal_fixtures_overlap(signals[a], signals[b]));

    std::size_t sycamore_count = 0;
    std::size_t sycamore_south_facing_count = 0;
    for (const auto& signal : signals) {
        const glm::vec3 junction = lanes.junction(signal.junction).pos;
        if (glm::distance(glm::vec2{junction.x, junction.z},
                          glm::vec2{950.0f, 200.0f}) > 1.0f) {
            continue;
        }
        ++sycamore_count;
        if (signal.layout.facing.z > 0.95f) {
            ++sycamore_south_facing_count;
            REQUIRE(lanes.lane(signal.incoming).cls == RoadClass::Arterial);
        }
    }
    REQUIRE(sycamore_count == 2u);
    REQUIRE(sycamore_south_facing_count == 1u);
    apricot_test::pass("near-parallel Sycamore and Spine approaches share one arterial signal mast");
    const auto lamps = build_street_lamp_layouts(lanes);
    REQUIRE(!lamps.empty());
    std::printf("  map: %zu street lamp candidates (%zu shared-box nodes before terrain rejection)\n",
                lamps.size(), lamps.size() * 4);
    int printed = 0;
    for (uint32_t j = 0; j < lanes.junction_count() && printed < 3; ++j) {
        if (lanes.junction_control(j) != JunctionControl::Signal) continue;
        const auto& junction = lanes.junction(j);
        const LaneRef r = junction.incoming.front();
        const Lane& lane = lanes.lane(r);
        const auto pose = lanes.pose(r, lane.length_m);
        const auto signal = make_lane_signal_layout(lanes, j, r);
        const glm::vec3 eye = signal.arm_end - pose.tangent * 30.0f;
        std::printf("  signal QA: start-at %.2f %.2f, look toward %.2f %.2f; junction %.2f %.2f\n",
                    eye.x, eye.z, signal.arm_end.x, signal.arm_end.z,
                    junction.pos.x, junction.pos.z);
        ++printed;
    }
    for (std::size_t i = 0; i < std::min<std::size_t>(lamps.size(), 3u); ++i)
        std::printf("  streetlamp QA: pole XZ %.2f %.2f; bulb XZ %.2f %.2f\n",
                    lamps[i].pole_ground.x, lamps[i].pole_ground.z,
                    lamps[i].arm_end.x, lamps[i].arm_end.z);
}

}  // namespace

int main() {
    signal_frames();
    spacing();
    network_placement();
    map_qa_positions();
    return apricot_test::done("road_fixture_tests");
}
