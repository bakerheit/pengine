#include "world/terrain.h"

#include <algorithm>

#include "world/heightmap.h"

namespace pengine {

TerrainChunk TerrainChunk::build(float origin_x, float origin_z, float cell_size) {
    TerrainChunk chunk;
    constexpr int N      = RES + 1;
    const float   step   = cell_size / static_cast<float>(RES);
    const float   uv_tile = cell_size / 8.f; // ~8 m per tile

    const size_t SN = static_cast<size_t>(N);
    chunk.heights.resize(SN * SN);
    chunk.verts.reserve(SN * SN);

    // First pass: heights only (so neighbour normals are well-defined at edges).
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            float wx = origin_x + static_cast<float>(x) * step;
            float wz = origin_z + static_cast<float>(z) * step;
            chunk.heights[static_cast<size_t>(z) * SN + static_cast<size_t>(x)]
                = Heightmap::sample(wx, wz);
        }
    }

    // Second pass: vertices + normals (central differences from heightmap).
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            float wx = origin_x + static_cast<float>(x) * step;
            float wz = origin_z + static_cast<float>(z) * step;
            float wy = chunk.heights[static_cast<size_t>(z) * SN + static_cast<size_t>(x)];

            Vertex v;
            v.position = {wx, wy, wz};
            v.normal   = Heightmap::normal(wx, wz);
            v.uv       = {wx / uv_tile, wz / uv_tile};
            v.tangent  = {1.f, 0.f, 0.f, 1.f};
            chunk.verts.push_back(v);
        }
    }

    // Indices: two CCW triangles per quad (viewed from +Y, which is up).
    chunk.indices.reserve(static_cast<size_t>(RES) * RES * 6);
    for (int z = 0; z < RES; ++z) {
        for (int x = 0; x < RES; ++x) {
            uint32_t i00 = static_cast<uint32_t>(z       * N + x);
            uint32_t i10 = static_cast<uint32_t>(z       * N + x + 1);
            uint32_t i01 = static_cast<uint32_t>((z + 1) * N + x);
            uint32_t i11 = static_cast<uint32_t>((z + 1) * N + x + 1);
            // CCW when viewed from above:
            chunk.indices.push_back(i00);
            chunk.indices.push_back(i01);
            chunk.indices.push_back(i11);
            chunk.indices.push_back(i00);
            chunk.indices.push_back(i11);
            chunk.indices.push_back(i10);
        }
    }

    return chunk;
}

float TerrainChunk::sample_local(float lx, float lz, float cell_size) const {
    constexpr int N    = RES + 1;
    const size_t  SN   = static_cast<size_t>(N);
    const float   step = cell_size / static_cast<float>(RES);

    float fx = lx / step;
    float fz = lz / step;
    int   xi = static_cast<int>(std::floor(fx));
    int   zi = static_cast<int>(std::floor(fz));
    if (xi < 0 || zi < 0 || xi >= N - 1 || zi >= N - 1) return 0.f;

    float tx = fx - static_cast<float>(xi);
    float tz = fz - static_cast<float>(zi);

    size_t sxi = static_cast<size_t>(xi);
    size_t szi = static_cast<size_t>(zi);
    float h00 = heights[szi       * SN + sxi];
    float h10 = heights[szi       * SN + sxi + 1];
    float h01 = heights[(szi + 1) * SN + sxi];
    float h11 = heights[(szi + 1) * SN + sxi + 1];
    float h0  = h00 + (h10 - h00) * tx;
    float h1  = h01 + (h11 - h01) * tx;
    return h0 + (h1 - h0) * tz;
}

} // namespace pengine
