#pragma once

#include <vector>

#include "scene/aabb.h"
#include "world/cell_coord.h"
#include "world/world_defs.h"

namespace pengine {

class Texture;

struct CityTextures {
    const Texture* road     = nullptr;
    const Texture* building = nullptr;
};

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

} // namespace pengine
