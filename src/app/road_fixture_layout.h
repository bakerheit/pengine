#pragma once

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "app/traffic_visual_layout.h"
#include "city/airport.h"
#include "road/lane_graph.h"
#include "traffic/crowd.h"

namespace apricot {

inline constexpr float kFixtureSideClearanceM = kSidewalkWidthM + 0.35f;
inline constexpr float kStreetLampHeightM = 8.0f;
inline constexpr float kStreetLampDrawDistanceM = 420.0f;
inline constexpr float kStreetLampLightDistanceM = 260.0f;
inline constexpr float kStreetLampMinSeparationM = 24.0f;

inline float street_lamp_spacing(RoadClass cls) {
    if (cls == RoadClass::Arterial) return 64.0f;
    if (cls == RoadClass::Street) return 88.0f;
    return 0.0f;
}

inline float fixture_junction_half_width(const LaneGraph& lanes, uint32_t j) {
    float half = 0.0f;
    const auto& junction = lanes.junction(j);
    for (const auto* refs : {&junction.incoming, &junction.outgoing})
        for (LaneRef r : *refs)
            half = std::max(half, lanes.lane(r).width_m * 0.5f);
    return half;
}

inline TrafficSignalLayout make_lane_signal_layout(const LaneGraph& lanes,
                                                    uint32_t j, LaneRef incoming,
                                                    float pole_height = 6.0f) {
    if (!lanes.valid(incoming) || j >= lanes.junction_count()) return {};
    const Lane& lane = lanes.lane(incoming);
    if (lane.junction_to != j || lane.centreline.size() < 2u) return {};
    glm::vec3 direction{0.0f};
    // Read the actual incoming geometry; the authored edge direction is
    // reversed for half the approaches and cannot orient their heads.
    for (std::size_t i = lane.centreline.size() - 1; i > 0; --i) {
        direction = lane.centreline[i] - lane.centreline[i - 1];
        direction.y = 0.0f;
        if (glm::length(direction) > 1e-4f) break;
    }
    float inbound_centre = 0.0f;
    int count = 0;
    for (LaneRef r : lanes.junction(j).incoming) {
        const Lane& approach = lanes.lane(r);
        if (approach.edge != lane.edge) continue;
        inbound_centre += approach.lateral_offset_m;
        ++count;
    }
    if (count == 0) return {};
    return make_traffic_signal_layout(lanes.junction(j).pos, direction,
        lane.width_m * 0.5f, fixture_junction_half_width(lanes, j),
        inbound_centre / static_cast<float>(count), kFixtureSideClearanceM,
        kFixtureSideClearanceM, pole_height);
}

// Equal arc-length stations, centred in the usable span. Short junction
// fragments get none. Splitting a road into multiple travel lanes must never
// multiply fixtures: callers use only the forward lane with index zero.
inline std::vector<float> street_lamp_stations(float length, float spacing,
                                              float start_clear, float end_clear) {
    std::vector<float> out;
    const float usable = length - start_clear - end_clear;
    if (!(spacing > 0.0f) || usable < spacing * 0.5f) return out;
    const int count = static_cast<int>(std::floor(usable / spacing)) + 1;
    const float first = start_clear +
        (usable - static_cast<float>(count - 1) * spacing) * 0.5f;
    for (int i = 0; i < count; ++i)
        out.push_back(first + static_cast<float>(i) * spacing);
    return out;
}

struct StreetLampLayout {
    glm::vec3 pole_ground{0.0f};
    glm::vec3 pole_top{0.0f};
    glm::vec3 arm_end{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float arm_length_m = 0.0f;
    uint64_t lane_key = 0;
};

struct TrafficSignalFixtureLayout {
    uint32_t junction = 0;
    LaneRef incoming = kInvalidLane;
    TrafficSignalLayout layout;
};

struct RoadControlLayout {
    LaneRef incoming = kInvalidLane;
    JunctionControl control = JunctionControl::None;
    bool all_way = false;
    bool painted = false;
    glm::vec3 pole_ground{0.0f};
    glm::vec3 marking_centre{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat marking_rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float marking_width_m = 0.0f;
};

inline bool road_control_pole_clear(const LaneGraph& lanes, uint32_t junction,
                                    glm::vec3 position) {
    for (const auto* refs : {&lanes.junction(junction).incoming,
                             &lanes.junction(junction).outgoing}) {
        for (LaneRef r : *refs) {
            const auto& lane = lanes.lane(r);
            const auto projection = lanes.project_onto(r, {position.x, position.z});
            const auto pose = lanes.pose(r, projection.dist_along_m);
            // A clamped endpoint is not a continuation of the asphalt.
            if (std::fabs(glm::dot(position - pose.position, pose.tangent)) > 0.5f)
                continue;
            const float t = projection.dist_along_m / std::max(0.01f, lane.length_m);
            const float offset = glm::mix(lane.lateral_offset_start_m,
                                         lane.lateral_offset_end_m, t);
            const float width = glm::mix(lane.departure_width_m, lane.approach_width_m, t);
            if (std::fabs(projection.lateral_m + offset) < width * 0.5f + 0.45f)
                return false;
        }
    }
    return true;
}

inline std::vector<RoadControlLayout> build_road_control_layouts(
    const LaneGraph& lanes, const CrowdTuning& tuning = {}) {
    std::vector<RoadControlLayout> result;
    for (uint32_t j = 0; j < lanes.junction_count(); ++j) {
        std::map<uint32_t, bool> seen_edges;
        const float clear = traffic_junction_clearance(lanes, j, tuning);
        for (LaneRef r : lanes.junction(j).incoming) {
            const Lane& lane = lanes.lane(r);
            const auto control = lanes.approach_control(r);
            if ((control != JunctionControl::Stop && control != JunctionControl::Yield) ||
                !seen_edges.emplace(lane.edge, true).second) continue;
            const float station = std::max(0.0f, lane.length_m - clear +
                tuning.traffic_half_length_m + 0.15f);
            const auto pose = lanes.pose(r, station);
            const float t = std::clamp(station / std::max(0.01f, lane.length_m), 0.0f, 1.0f);
            const float offset = glm::mix(lane.lateral_offset_start_m,
                                         lane.lateral_offset_end_m, t);
            const glm::vec3 centre = pose.position - pose.right * offset;
            const float side = offset < 0.0f ? -1.0f : 1.0f;
            const float width = glm::mix(lane.departure_width_m, lane.approach_width_m, t);
            RoadControlLayout layout;
            layout.incoming = r;
            layout.control = control;
            layout.all_way = lanes.junction_control(j) == JunctionControl::Stop;
            layout.painted = road_is_paved(lane.cls);
            layout.pole_ground = centre + pose.right * side * (width * 0.5f + 0.8f);
            // At a skewed merge, the side-road shoulder can still be a live
            // motorway lane. Move the sign upstream until its post clears
            // every connected carriageway, while keeping the yield line at
            // the driver's actual gate.
            for (int back = 0; back <= 48 &&
                    !road_control_pole_clear(lanes, j, layout.pole_ground); ++back) {
                const float pole_station = std::max(0.0f, station - static_cast<float>(back));
                const auto pole_pose = lanes.pose(r, pole_station);
                const float pole_t = pole_station / std::max(0.01f, lane.length_m);
                const float pole_offset = glm::mix(lane.lateral_offset_start_m,
                                                  lane.lateral_offset_end_m, pole_t);
                const float pole_width = glm::mix(lane.departure_width_m,
                                                  lane.approach_width_m, pole_t);
                for (int verge = 0; verge < 5; ++verge) {
                    layout.pole_ground = pole_pose.position + pole_pose.right *
                        (side * (pole_width * 0.5f + 0.8f +
                                 static_cast<float>(verge) * 0.5f) - pole_offset);
                    if (road_control_pole_clear(lanes, j, layout.pole_ground)) break;
                }
            }
            layout.marking_centre = centre + pose.right *
                (lane.one_way ? 0.0f : side * width * 0.25f);
            layout.marking_width_m = std::max(0.5f,
                width * (lane.one_way ? 1.0f : 0.5f) - 0.65f);
            glm::vec3 facing = -pose.tangent;
            facing.y = 0.0f; facing = glm::normalize(facing);
            layout.rotation = glm::normalize(glm::quat_cast(
                glm::mat3{pose.right, glm::vec3{0, 1, 0}, facing}));
            layout.marking_rotation = glm::normalize(glm::quat_cast(glm::mat3{
                pose.right, glm::normalize(glm::cross(-pose.tangent, pose.right)),
                -pose.tangent}));
            result.push_back(layout);
        }
    }
    return result;
}

inline bool traffic_signal_fixtures_overlap(
    const TrafficSignalFixtureLayout& a,
    const TrafficSignalFixtureLayout& b) {
    if (a.junction != b.junction) return false;
    const glm::vec2 base_delta{
        a.layout.pole_ground.x - b.layout.pole_ground.x,
        a.layout.pole_ground.z - b.layout.pole_ground.z};
    const glm::vec2 head_delta{
        a.layout.arm_end.x - b.layout.arm_end.x,
        a.layout.arm_end.z - b.layout.arm_end.z};
    return glm::dot(base_delta, base_delta) < 9.0f &&
           glm::dot(head_delta, head_delta) < 9.0f &&
           glm::dot(a.layout.facing, b.layout.facing) > 0.9659258f;
}

// A self-return or acute fork can contribute two authored edges that reach the
// same stop line from almost the same direction. Rendering one mast per edge
// stacks arms and heads on top of each other. Keep one physical fixture for
// that approach, preferring the more important/wider road; both lanes remain
// on the same signal phase.
inline std::vector<TrafficSignalFixtureLayout> build_traffic_signal_layouts(
    const LaneGraph& lanes, float pole_height = 6.0f) {
    std::vector<TrafficSignalFixtureLayout> out;
    for (uint32_t j = 0; j < lanes.junction_count(); ++j) {
        if (lanes.junction_control(j) != JunctionControl::Signal) continue;
        std::map<uint32_t, bool> seen_edges;
        for (LaneRef incoming : lanes.junction(j).incoming) {
            const Lane& lane = lanes.lane(incoming);
            if (seen_edges.emplace(lane.edge, true).second == false ||
                lane.centreline.size() < 2u) {
                continue;
            }
            TrafficSignalFixtureLayout candidate;
            candidate.junction = j;
            candidate.incoming = incoming;
            candidate.layout = make_lane_signal_layout(
                lanes, j, incoming, pole_height);
            if (!(candidate.layout.arm_length_m > 0.0f)) continue;

            auto duplicate = std::find_if(out.begin(), out.end(),
                [&](const TrafficSignalFixtureLayout& placed) {
                    return traffic_signal_fixtures_overlap(placed, candidate) &&
                           lanes.approach_group_a(j, placed.incoming) ==
                               lanes.approach_group_a(j, incoming);
                });
            if (duplicate == out.end()) {
                out.push_back(candidate);
                continue;
            }
            const Lane& placed = lanes.lane(duplicate->incoming);
            const bool preferred = road_class_index(lane.cls) <
                                       road_class_index(placed.cls) ||
                (lane.cls == placed.cls && lane.width_m > placed.width_m) ||
                (lane.cls == placed.cls && lane.width_m == placed.width_m &&
                 lane.key < placed.key);
            if (preferred) *duplicate = candidate;
        }
    }
    return out;
}

inline StreetLampLayout make_street_lamp_layout(const LanePose& pose,
                                                float lane_offset,
                                                float road_width, bool right_side) {
    const float side = right_side ? 1.0f : -1.0f;
    const glm::vec3 centre = pose.position - pose.right * lane_offset;
    const auto arm = make_traffic_signal_layout(
        centre, pose.tangent, road_width * 0.5f, 0.0f,
        side * (road_width * 0.5f - 1.0f), 0.0f,
        kFixtureSideClearanceM, kStreetLampHeightM);
    // The signal helper supplies the basis and lateral arm. A roadside lamp
    // belongs exactly at its station, without the signal's junction setback.
    const glm::vec3 shift = -arm.facing * (road_width * 0.5f);
    return {arm.pole_ground + shift, arm.pole_top + shift,
            arm.arm_end + shift, arm.rotation, arm.arm_length_m, 0};
}

// Greedy spacing is stable because candidates are visited in authored-key
// order. The small grid avoids an all-pairs search across the island.
inline std::vector<StreetLampLayout> build_street_lamp_layouts(const LaneGraph& lanes) {
    std::vector<LaneRef> roads;
    for (LaneRef r = 0; r < lanes.lane_count(); ++r) {
        const Lane& lane = lanes.lane(r);
        if (lane.forward && lane.index == 0 && street_lamp_spacing(lane.cls) > 0.0f)
            roads.push_back(r);
    }
    std::sort(roads.begin(), roads.end(), [&](LaneRef a, LaneRef b) {
        return lanes.lane(a).key < lanes.lane(b).key;
    });
    std::vector<StreetLampLayout> result;
    using Cell = std::pair<int, int>;
    std::map<Cell, std::vector<glm::vec2>> occupied;
    for (LaneRef r : roads) {
        const Lane& lane = lanes.lane(r);
        const auto stations = street_lamp_stations(lane.length_m,
            street_lamp_spacing(lane.cls),
            fixture_junction_half_width(lanes, lane.junction_from) + 14.0f,
            fixture_junction_half_width(lanes, lane.junction_to) + 14.0f);
        for (std::size_t i = 0; i < stations.size(); ++i) {
            const LanePose pose = lanes.pose(r, stations[i]);
            StreetLampLayout layout = make_street_lamp_layout(pose,
                lane.lateral_offset_m, lane.width_m, ((lane.key + i) & 1u) == 0u);
            layout.lane_key = lane.key;
            const glm::vec2 p{layout.pole_ground.x, layout.pole_ground.z};
            // Leave all authored airport fixtures alone, including runway,
            // taxiways and aprons, with room for the cantilever arm.
            if (city::airport_lot_contains(p.x, p.y, 12.0f)) continue;
            const LaneProjection near = lanes.nearest_lane(p, 24.0f);
            if (near.valid()) {
                const Lane& other = lanes.lane(near.lane);
                if (std::fabs(near.lateral_m + other.lateral_offset_m) <
                    other.width_m * 0.5f + kSidewalkWidthM + 0.15f) continue;
            }
            const Cell cell{static_cast<int>(std::floor(p.x / kStreetLampMinSeparationM)),
                            static_cast<int>(std::floor(p.y / kStreetLampMinSeparationM))};
            bool duplicate = false;
            for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz) {
                const auto it = occupied.find({cell.first + dx, cell.second + dz});
                if (it == occupied.end()) continue;
                for (glm::vec2 previous : it->second)
                    if (glm::length(previous - p) < kStreetLampMinSeparationM)
                        duplicate = true;
            }
            if (duplicate) continue;
            occupied[cell].push_back(p);
            result.push_back(layout);
        }
    }
    return result;
}

}  // namespace apricot
