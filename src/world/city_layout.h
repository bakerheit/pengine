#pragma once

#include <vector>

#include "scene/aabb.h"
#include "world/cell_coord.h"
#include "world/instance_def.h"
#include "world/road_grid.h"

namespace pengine {

// Procedural city block layout for one streaming cell. Deterministic from
// (coord, cfg). Designed so neighbouring cells line up at shared streets.
//
// Layout: 4x4 blocks per cell, 8 m streets between blocks (and at cell edges).
//   block size = (cell_size - 5*street_width) / 4
//   For cell_size = 256 m:    block = 54 m,  street = 8 m
//
// Outputs are world-space InstanceDefs (visual) and AABBs (collision =
// buildings only; roads are aesthetic and rely on the heightmap for the
// driving surface).
//
// This function is the procedural authoring tool: the streamer calls it on
// first encounter of a cell, saves the result as an IPL, and reads from disk
// thereafter. It carries no runtime dependency on textures — model ids fully
// describe the look, resolved through ModelRegistry at activation time.
struct CityCellLayout {
    std::vector<InstanceDef> instances;
    std::vector<AABB>        collisions;
};

CityCellLayout generate_city_cell(CellCoord coord, float cell_size);

// Walkable ground height at world (x, z): heightmap sample plus the sidewalk
// curb (15 cm) when the point lies inside a plot's sidewalk ring. Roads and
// terrain return the bare heightmap value. Used by the character controller
// (and pedestrian AI) so feet rest on the slab top, not in it.
float city_ground_sample(float x, float z);

}  // namespace pengine
