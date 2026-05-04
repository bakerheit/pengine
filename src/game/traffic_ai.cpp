#include "game/traffic_ai.h"

#include <algorithm>
#include <cmath>

#include "world/road_grid.h"

namespace pengine {

TrafficDirInfo traffic_dir_info(GridDir dir) {
    switch (dir) {
        case GridDir::East:
            return {+1,  0, {+1.f, 0.f,  0.f}, { 0.f, 0.f, -1.f}, 270.f};
        case GridDir::North:
            return { 0, +1, { 0.f, 0.f, +1.f}, {+1.f, 0.f,  0.f}, 180.f};
        case GridDir::West:
            return {-1,  0, {-1.f, 0.f,  0.f}, { 0.f, 0.f, +1.f},  90.f};
        case GridDir::South:
            return { 0, -1, { 0.f, 0.f, -1.f}, {-1.f, 0.f,  0.f},   0.f};
    }
    return {+1, 0, {+1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, 270.f};
}

DriverProfile make_driver_profile(DriverProfileKind kind) {
    DriverProfile p;
    p.kind = kind;
    switch (kind) {
        case DriverProfileKind::Cautious:
            p.speed_mul = 0.82f;
            p.headway = 1.75f;
            p.min_gap = 3.8f;
            p.accel = 3.2f;
            p.brake = 7.0f;
            p.patience_seconds = 7.0f;
            p.safe_lane_gap = 34.f;
            p.honk_after = 5.5f;
            p.yellow_bias = 0.15f;
            break;
        case DriverProfileKind::Normal:
            p.speed_mul = 1.0f;
            p.headway = 1.35f;
            p.min_gap = 2.8f;
            p.accel = 4.5f;
            p.brake = 8.0f;
            p.patience_seconds = 5.0f;
            p.safe_lane_gap = 28.f;
            p.honk_after = 3.0f;
            p.yellow_bias = 0.5f;
            break;
        case DriverProfileKind::Impatient:
            p.speed_mul = 1.12f;
            p.headway = 1.05f;
            p.min_gap = 2.2f;
            p.accel = 5.6f;
            p.brake = 9.5f;
            p.patience_seconds = 3.0f;
            p.safe_lane_gap = 24.f;
            p.honk_after = 1.8f;
            p.yellow_bias = 0.72f;
            break;
        case DriverProfileKind::AggressiveLite:
            p.speed_mul = 1.22f;
            p.headway = 0.9f;
            p.min_gap = 1.8f;
            p.accel = 6.4f;
            p.brake = 10.5f;
            p.patience_seconds = 2.0f;
            p.safe_lane_gap = 20.f;
            p.honk_after = 1.0f;
            p.yellow_bias = 0.85f;
            break;
    }
    return p;
}

DriverProfile random_driver_profile(std::mt19937& rng) {
    const std::uint32_t r = rng() % 100u;
    if (r < 18u) return make_driver_profile(DriverProfileKind::Cautious);
    if (r < 78u) return make_driver_profile(DriverProfileKind::Normal);
    if (r < 94u) return make_driver_profile(DriverProfileKind::Impatient);
    return make_driver_profile(DriverProfileKind::AggressiveLite);
}

float traffic_follow_speed_for_gap(float gap, const DriverProfile& profile) {
    return std::max(0.f, (gap - profile.min_gap) / profile.headway);
}

bool traffic_should_stop_for_yellow(float distance_to_stop, float speed,
                                    const DriverProfile& profile) {
    float stopping_dist = (speed * speed)
        / (2.f * std::max(profile.brake, 0.1f));
    return distance_to_stop > stopping_dist && profile.yellow_bias < 0.7f;
}

bool traffic_profile_may_pass_jam(const DriverProfile& profile,
                                  float blocked_seconds) {
    switch (profile.kind) {
        case DriverProfileKind::Cautious:
            return false;
        case DriverProfileKind::Normal:
            return blocked_seconds >= profile.patience_seconds + 7.f;
        case DriverProfileKind::Impatient:
        case DriverProfileKind::AggressiveLite:
            return blocked_seconds >= profile.patience_seconds;
    }
    return false;
}

bool operator==(const LaneId& a, const LaneId& b) {
    return a.i == b.i && a.j == b.j && a.dir == b.dir;
}

bool operator!=(const LaneId& a, const LaneId& b) {
    return !(a == b);
}

TrafficLaneGraph::TrafficLaneGraph(const RoadGraph& graph)
    : graph_(graph) {}

IntersectionId TrafficLaneGraph::lane_end(const LaneId& id) {
    TrafficDirInfo info = traffic_dir_info(id.dir);
    return {id.i + info.di, id.j + info.dj};
}

TrafficTurnKind TrafficLaneGraph::turn_kind(GridDir from, GridDir to) {
    const int f = static_cast<int>(from);
    const int t = static_cast<int>(to);
    const int delta = (t - f + 4) & 3;
    if (delta == 0) return TrafficTurnKind::Straight;
    if (delta == 1) return TrafficTurnKind::Left;
    if (delta == 3) return TrafficTurnKind::Right;
    return TrafficTurnKind::UTurn;
}

float TrafficLaneGraph::turn_weight(TrafficTurnKind kind) {
    switch (kind) {
        case TrafficTurnKind::Straight: return 56.f;
        case TrafficTurnKind::Right:    return 25.f;
        case TrafficTurnKind::Left:     return 18.f;
        case TrafficTurnKind::UTurn:    return 1.f;
    }
    return 1.f;
}

TrafficLane TrafficLaneGraph::lane(const LaneId& id, float lane_offset) const {
    TrafficDirInfo info = traffic_dir_info(id.dir);
    glm::vec3 start_center = RoadGraph::intersection_pos(id.i, id.j, 0.f);
    glm::vec3 end_center = start_center + info.unit * ROAD_PITCH;
    TrafficLane lane_out;
    lane_out.id = id;
    lane_out.start = start_center + info.right * lane_offset;
    lane_out.end = end_center + info.right * lane_offset;
    lane_out.unit = info.unit;
    lane_out.right = info.right;
    lane_out.length = ROAD_PITCH;
    lane_out.stop_distance = ROAD_PITCH - 6.f;
    return lane_out;
}

bool TrafficLaneGraph::lane_loaded(const LaneId& id) const {
    TrafficDirInfo info = traffic_dir_info(id.dir);
    return graph_.is_intersection_loaded(id.i, id.j)
        && graph_.is_intersection_loaded(id.i + info.di, id.j + info.dj);
}

int TrafficLaneGraph::outgoing(const LaneId& id,
                               std::array<TrafficTurnLink, 4>& out) const {
    IntersectionId end = lane_end(id);
    std::array<GridDir, 4> dirs;
    int n = graph_.outgoing(end.i, end.j, dirs);
    std::size_t m = 0;
    for (int k = 0; k < n; ++k) {
        GridDir to_dir = dirs[static_cast<std::size_t>(k)];
        TrafficTurnKind kind = turn_kind(id.dir, to_dir);
        if (kind == TrafficTurnKind::UTurn && n > 1) continue;
        out[m++] = TrafficTurnLink{
            id,
            LaneId{end.i, end.j, to_dir},
            kind,
            turn_weight(kind),
        };
    }
    return static_cast<int>(m);
}

LaneId TrafficLaneGraph::choose_next_lane(const LaneId& id,
                                          std::mt19937& rng) const {
    std::array<TrafficTurnLink, 4> links;
    int n = outgoing(id, links);
    if (n <= 0) {
        IntersectionId end = lane_end(id);
        return LaneId{end.i, end.j, opposite(id.dir)};
    }

    float total = 0.f;
    for (int k = 0; k < n; ++k)
        total += links[static_cast<std::size_t>(k)].weight;

    float r = (static_cast<float>(rng() & 0xFFFFu) / 65535.f) * total;
    for (int k = 0; k < n; ++k) {
        const TrafficTurnLink& link = links[static_cast<std::size_t>(k)];
        r -= link.weight;
        if (r <= 0.f) return link.to;
    }
    return links[static_cast<std::size_t>(n - 1)].to;
}

TrafficRoute TrafficLaneGraph::make_route(LaneId start, int lane_count,
                                          std::mt19937& rng) const {
    TrafficRoute route;
    if (lane_count <= 0 || !lane_loaded(start)) return route;
    route.lanes.reserve(static_cast<std::size_t>(lane_count));
    LaneId cur = start;
    for (int k = 0; k < lane_count; ++k) {
        if (!lane_loaded(cur)) break;
        route.lanes.push_back(cur);
        cur = choose_next_lane(cur, rng);
    }
    return route;
}

glm::vec3 TrafficLaneGraph::lane_pose(const LaneId& id,
                                      float distance_along,
                                      float lane_offset,
                                      float lateral_offset) const {
    TrafficDirInfo info = traffic_dir_info(id.dir);
    glm::vec3 from = RoadGraph::intersection_pos(id.i, id.j, 0.f);
    float d = std::clamp(distance_along, 0.f, ROAD_PITCH);
    return from + info.unit * d + info.right * (lane_offset + lateral_offset);
}

} // namespace pengine
