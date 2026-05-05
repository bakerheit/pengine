#include "game/pedestrian_ai.h"

#include <cmath>

#include "world/city_layout.h"   // city_ground_sample
#include "world/road_graph.h"

namespace pengine {

glm::vec3 sidewalk_pose(const LaneId& edge, float dist_along, float side) {
    TrafficDirInfo info = traffic_dir_info(edge.dir);
    glm::vec3 origin = RoadGraph::intersection_pos(edge.i, edge.j, 0.f);
    glm::vec3 xz = origin
                 + info.unit  * dist_along
                 + info.right * (side * PED_SIDEWALK_OFFSET);
    // city_ground_sample returns heightmap + sidewalk curb when (x,z) is
    // inside a plot's kerb ring, so the ped's feet sit on the slab top
    // rather than sinking into the underlying terrain.
    return { xz.x, city_ground_sample(xz.x, xz.z), xz.z };
}

float sidewalk_yaw_deg(const LaneId& edge) {
    // Same convention as the player character at Application.cpp:809:
    //   target_yaw = atan2(velocity.z, velocity.x)
    // The visual rotation downstream is angleAxis(-yaw + 90°, Y), which
    // depends on this formulation. traffic_dir_info(dir).yaw_deg uses a
    // DIFFERENT convention (tuned for car chassis whose local +X is the
    // right side, with a separate 180° mesh flip on top) and would land
    // the ped facing perpendicular to its travel direction.
    glm::vec3 u = traffic_dir_info(edge.dir).unit;
    return glm::degrees(std::atan2(u.z, u.x));
}

} // namespace pengine
