#pragma once

namespace pengine {

struct WorldConfig {
    float cell_size      = 256.f;  // metres per cell side
    int   stream_radius  = 2;      // Chebyshev radius: (2r+1)^2 cells max loaded
    int   world_cells_x  = 32;     // total world width in cells (8 km)
    int   world_cells_z  = 32;
    int   max_uploads_per_frame = 4;
};

}  // namespace pengine
