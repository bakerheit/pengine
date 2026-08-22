#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"

namespace apricot {

// A square tile of meshed terrain.
//
// Chunks are SIM-SIDE geometry: plain vertex and index arrays with no GL type
// anywhere near them. src/gfx/ uploads them; nothing here knows that happened.
// They are never written to disk — a chunk is a pure function of (seed, coord)
// so regenerating is cheaper and safer than caching, and a cache on disk means
// a stale file silently outranks a code change.

// Side length of one chunk in metres.
inline constexpr float kChunkMetres = 64.0f;

// Vertices per chunk edge. 65 = 64 quads plus the shared closing row, so
// adjacent chunks evaluate height_at() at the identical world coordinate on
// their shared edge and the seam closes exactly rather than approximately.
inline constexpr int kChunkVerts = 65;

struct ChunkCoord {
    int32_t x = 0;
    int32_t z = 0;

    friend bool operator==(ChunkCoord a, ChunkCoord b) {
        return a.x == b.x && a.z == b.z;
    }
    friend bool operator!=(ChunkCoord a, ChunkCoord b) { return !(a == b); }
};

struct ChunkCoordHash {
    std::size_t operator()(ChunkCoord c) const noexcept {
        const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(c.x));
        const uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(c.z));
        return static_cast<std::size_t>((uz << 32) | ux);
    }
};

// Which chunk a world position falls in. Uses floor, not truncation:
// truncation folds -0.5 and +0.5 into the same chunk and produces a
// double-width seam through the origin.
ChunkCoord chunk_at(float world_x, float world_z);

// World-space origin (minimum corner) of a chunk.
glm::vec2 chunk_origin(ChunkCoord c);

struct TerrainVertex {
    glm::vec3 position;  // world space
    glm::vec3 normal;
    glm::vec2 uv;
};

struct ChunkMesh {
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;  // world space, for culling and streaming decisions
};

// Build one chunk's mesh. PURE in (seed, coord) — no clock, no shared state,
// no allocation beyond the returned buffers. Safe to call from a worker
// thread, which is the entire point.
ChunkMesh build_chunk(uint64_t seed, ChunkCoord coord);

}  // namespace apricot
