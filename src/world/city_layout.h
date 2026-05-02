#pragma once

#include <vector>

#include "scene/aabb.h"
#include "world/cell_coord.h"
#include "world/world_defs.h"

namespace pengine {

class Texture;

struct CityTextures {
    const Texture* road     = nullptr;
    const Texture* sidewalk = nullptr;
    const Texture* building = nullptr;
};

// Global road grid constants. Roads run along world lines x = i * ROAD_PITCH
// (NS roads) and z = j * ROAD_PITCH (EW roads). Intersections at the
// integer grid points (i, j). Each cell at (cx, cz) owns the 4×4 roads
// indexed by i ∈ [cx*4, cx*4+4), j ∈ [cz*4, cz*4+4).
constexpr float ROAD_PITCH       = 64.f;
constexpr float STREET_WIDTH     = 8.f;
constexpr int   ROADS_PER_CELL   = 4;     // = cell_size / ROAD_PITCH

// Procedural city block layout for one streaming cell. Deterministic from
// (coord, cfg). Designed so neighbouring cells line up at shared streets.
//
// Layout: 4x4 blocks per cell, 8 m streets between blocks (and at cell edges).
//   block size = (cell_size - 5*street_width) / 4
//   For cell_size = 256 m:    block = 54 m,  street = 8 m
//
// Outputs are world-space ObjectDefs (visual) and AABBs (collision = buildings
// only; roads are aesthetic and rely on the heightmap for the driving surface).
struct CityCellLayout {
    std::vector<ObjectDef> visuals;     // buildings + road slabs
    std::vector<AABB>      collisions;  // buildings only
};

CityCellLayout generate_city_cell(CellCoord coord, float cell_size, float ground_y,
                                   const CityTextures& tex);

// Walkable ground height at world (x, z): heightmap sample plus the sidewalk
// curb (15 cm) when the point lies inside a plot's sidewalk ring. Roads and
// terrain return the bare heightmap value. Used by the character controller
// (and pedestrian AI) so feet rest on the slab top, not in it.
float city_ground_sample(float x, float z);

} // namespace pengine
