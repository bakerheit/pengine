#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "world/road_graph.h"

namespace pengine {

struct TrafficDirInfo {
    int       di = 1;
    int       dj = 0;
    glm::vec3 unit{1.f, 0.f, 0.f};
    glm::vec3 right{0.f, 0.f, -1.f};
    float     yaw_deg = 270.f;
};

TrafficDirInfo traffic_dir_info(GridDir dir);

enum class LaneChangeIntent : std::uint8_t {
    None,
    AroundBlocker,
    ReturnToLane,
};

enum class TrafficAgentState : std::uint8_t {
    Cruise,
    FollowLeader,
    ApproachSignal,
    Queued,
    YieldIntersection,
    TraverseIntersection,
    LaneChange,
    AvoidObstacle,
    BlockedRecovery,
    Panic,
    PhysicsFallback,
};

enum class DriverProfileKind : std::uint8_t {
    Cautious,
    Normal,
    Impatient,
    AggressiveLite,
};

struct DriverProfile {
    DriverProfileKind kind = DriverProfileKind::Normal;
    float speed_mul        = 1.f;
    float headway          = 1.35f;
    float min_gap          = 2.8f;
    float accel            = 4.5f;
    float brake            = 8.0f;
    float patience_seconds = 5.0f;
    float safe_lane_gap    = 28.f;
    float honk_after       = 3.0f;
    float yellow_bias      = 0.5f; // 0 = stop early, 1 = likely to continue.
};

DriverProfile make_driver_profile(DriverProfileKind kind);
DriverProfile random_driver_profile(std::mt19937& rng);
float traffic_follow_speed_for_gap(float gap, const DriverProfile& profile);
bool traffic_should_stop_for_yellow(float distance_to_stop, float speed,
                                    const DriverProfile& profile);
bool traffic_profile_may_pass_jam(const DriverProfile& profile,
                                  float blocked_seconds);

struct LaneId {
    int     i = 0;
    int     j = 0;
    GridDir dir = GridDir::East;
};

bool operator==(const LaneId& a, const LaneId& b);
bool operator!=(const LaneId& a, const LaneId& b);

struct IntersectionId {
    int i = 0;
    int j = 0;
};

enum class TrafficTurnKind : std::uint8_t {
    Left,
    Straight,
    Right,
    UTurn,
};

struct TrafficLane {
    LaneId    id;
    glm::vec3 start{0.f};
    glm::vec3 end{0.f};
    glm::vec3 unit{1.f, 0.f, 0.f};
    glm::vec3 right{0.f, 0.f, -1.f};
    float     length = 0.f;
    float     stop_distance = 0.f;
};

struct TrafficTurnLink {
    LaneId          from;
    LaneId          to;
    TrafficTurnKind kind = TrafficTurnKind::Straight;
    float           weight = 1.f;
};

struct TrafficRoute {
    std::vector<LaneId> lanes;
};

class TrafficLaneGraph {
public:
    explicit TrafficLaneGraph(const RoadGraph& graph);

    TrafficLane lane(const LaneId& id, float lane_offset) const;
    bool lane_loaded(const LaneId& id) const;
    int outgoing(const LaneId& id, std::array<TrafficTurnLink, 4>& out) const;
    LaneId choose_next_lane(const LaneId& id, std::mt19937& rng) const;
    TrafficRoute make_route(LaneId start, int lane_count, std::mt19937& rng) const;
    glm::vec3 lane_pose(const LaneId& id, float distance_along,
                        float lane_offset, float lateral_offset) const;

    static IntersectionId lane_end(const LaneId& id);
    static TrafficTurnKind turn_kind(GridDir from, GridDir to);
    static float turn_weight(TrafficTurnKind kind);

private:
    const RoadGraph& graph_;
};

} // namespace pengine
