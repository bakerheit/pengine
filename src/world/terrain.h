#pragma once

#include <cstdint>
#include <vector>

#include "render/mesh.h"

namespace pengine {

// Per-cell terrain chunk: a (RES+1)×(RES+1) vertex grid sampled from the
// world Heightmap. Vertices are in world-space (so the chunk's SceneNode
// transform should be identity).
struct TerrainChunk {
    static constexpr int RES = 32; // 32×32 quads per cell

    std::vector<Vertex>   verts;
    std::vector<uint32_t> indices;

    // Precomputed height samples (RES+1)×(RES+1), row-major (z * stride + x).
    std::vector<float>    heights;

    // Build CPU-side data for the chunk covering [origin_x, origin_x+cell_size] ×
    // [origin_z, origin_z+cell_size]. Safe to call from any thread.
    static TerrainChunk build(float origin_x, float origin_z, float cell_size);

    // Bilinear height query inside the chunk. Returns 0 if outside.
    float sample_local(float lx, float lz, float cell_size) const;
};

} // namespace pengine
