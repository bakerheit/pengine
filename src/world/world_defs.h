#pragma once

#include "scene/transform.h"

namespace pengine {

// A single object instance inside a world cell.
struct ObjectDef {
    uint32_t  model_id   = 0;   // 0 = unit cube (the only model in Phase 4)
    Transform transform;
    float     lod_near   = 200.f; // switch to low-detail beyond this (Phase 5+)
    float     lod_far    = 500.f; // cull beyond this
};

struct WorldConfig {
    float cell_size      = 256.f;  // metres per cell side
    int   stream_radius  = 2;      // Chebyshev radius: (2r+1)^2 cells max loaded
    int   world_cells_x  = 16;     // total world width in cells (4 km)
    int   world_cells_z  = 16;
    int   max_uploads_per_frame = 4; // new cells to activate per frame
};

} // namespace pengine
