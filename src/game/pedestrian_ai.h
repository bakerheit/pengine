#pragma once

#include <glm/glm.hpp>

#include "game/traffic_ai.h"     // LaneId, traffic_dir_info, GridDir
#include "world/road_grid.h"

namespace pengine {

// Sidewalk lateral offset: peds walk on the slab strip between the road
// edge (ROAD_HALF_WIDTH from centreline) and the plot interior (further
// out by SIDEWALK_WIDTH). Centre of that strip is the natural walking
// line so the ped doesn't clip the kerb or the building wall.
constexpr float PED_SIDEWALK_OFFSET = ROAD_HALF_WIDTH + SIDEWALK_WIDTH * 0.5f;

// World pose of a pedestrian on edge (intersection (i,j) -> outgoing dir),
// `dist_along` metres in along the edge's travel direction, on `side`
// (+1 = right of travel, -1 = left). Y comes from city_ground_sample so
// the feet land on the kerb slab, not the underlying heightmap.
//
// `side` is float so jaywalking (phase 3) can lerp continuously between
// the two sidewalk lines.
glm::vec3 sidewalk_pose(const LaneId& edge, float dist_along, float side);

// Yaw (degrees) for a pedestrian travelling along `edge`. Same convention
// as traffic_dir_info: 0=south, 90=west, 180=north, 270=east.
float sidewalk_yaw_deg(const LaneId& edge);

} // namespace pengine
