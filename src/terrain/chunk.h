#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"

namespace apricot {

// A square tile of meshed terrain.
//
// Chunks are SIM-SIDE geometry: plain vertex and index arrays with no renderer
// type anywhere near them. src/gfx/ uploads them; nothing here knows that
// happened. They are never written to disk — a chunk is a pure function of
// (seed, coord) so regenerating is cheaper and safer than caching, and a cache
// on disk means a stale file silently outranks a code change.

// Side length of one chunk in metres.
inline constexpr float kChunkMetres = 64.0f;

// Vertices per chunk edge. 65 = 64 quads plus the shared closing row, so
// adjacent chunks evaluate height_at() at the identical world coordinate on
// their shared edge and the seam closes exactly rather than approximately.
inline constexpr int kChunkVerts = 65;

// Quads per chunk edge, and the metre spacing between vertices.
inline constexpr int kChunkQuads = kChunkVerts - 1;
inline constexpr float kVertexSpacingMetres =
    kChunkMetres / static_cast<float>(kChunkQuads);

// THE VERTEX LATTICE IS GLOBAL. kChunkMetres is an exact multiple of
// kVertexSpacingMetres and chunk origins are exact multiples of kChunkMetres,
// so every chunk's local lattice lands on the same world-space grid as every
// other chunk's. That is what makes mesh_height_at() below able to reconstruct
// the drawn triangle without knowing which chunk it is in — and what makes
// shared edges bit-identical rather than merely close.

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

    // World-space UV, in chunk units. Continuous ACROSS chunk boundaries
    // because it is derived from the world position, so a tiling texture
    // crosses a seam without any stitching.
    glm::vec2 uv;

    // Blend weights in Surface order: rock, gravel, grass, sand. Sums to 1.
    //
    // ADDED BY THE TERRAIN TICKET — see the note at the top of chunk.cpp.
    // Splatting four materials needs per-vertex weights; without them a
    // terrain shader has nothing to mix and every chunk draws as one flat
    // material. The renderer needs one more vertex attribute for this.
    glm::vec4 material_weights;
};

struct ChunkMesh {
    ChunkCoord coord;
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;  // world space, for culling and streaming decisions
};

// Build one chunk's mesh. PURE in (seed, coord) — no clock, no shared state,
// no allocation beyond the returned buffers. Safe to call from a worker
// thread, which is the entire point.
ChunkMesh build_chunk(uint64_t seed, ChunkCoord coord);

// ---------------------------------------------------------------------------
//  Collision
// ---------------------------------------------------------------------------

struct CollisionTri {
    glm::vec3 a;
    glm::vec3 b;
    glm::vec3 c;

    // Unit face normal, always in the +Y hemisphere. A height field cannot
    // overhang, so a downward-facing terrain triangle is a winding bug and
    // never a real feature.
    glm::vec3 normal;
};

struct ChunkCollision {
    ChunkCoord coord;
    std::vector<CollisionTri> triangles;
    AABB bounds;
};

// Collision triangles for a chunk.
//
// NOTE THE SIGNATURE. It takes the built mesh and NOT the seed, so it is
// structurally incapable of re-deriving the surface: the only geometry it can
// see is the geometry that draws. That is deliberate. The alternative — a
// second function that also knows the seed and rebuilds the surface its own
// way, perhaps at a coarser step "because collision does not need the detail"
// — drifts from the visual mesh the moment either side is touched, and the
// resulting "the car floats above that hill" bug is invisible until somebody
// drives there.
//
// The solid the car hits IS the solid the player sees, enforced by the type
// system rather than by a comment asking nicely.
ChunkCollision build_chunk_collision(const ChunkMesh& mesh);

// ---------------------------------------------------------------------------
//  Point queries against the MESHED surface
// ---------------------------------------------------------------------------

// Height of the DRAWN TRIANGLE at a world XZ, as opposed to height_at()'s
// value of the underlying continuous field.
//
// THESE ARE NOT THE SAME NUMBER AND THE DIFFERENCE IS A REAL BUG.
//
// The mesh is planar between lattice points; the height field is not. They
// agree exactly AT the lattice and diverge everywhere in between — by
// centimetres on a gentle grade and by more on a ridge. Anything that has to
// line up with what the player can see (wheel contact, a prop sitting on the
// ground, a camera that must not clip through a hill) has to ask the mesh, not
// the field. Resampling the field instead is what makes a car sink into a
// slope it is visibly resting on.
//
// Pure, and valid ANYWHERE — including in chunks that have never been meshed,
// because it reconstructs the same lattice cell the mesher would have built.
// Physics must never depend on streaming state.
float mesh_height_at(uint64_t seed, float x, float z);

// Face normal of the drawn triangle at a world XZ. Piecewise constant across
// each triangle, which is exactly right for contact response — the smooth
// normal_at() is a SHADING normal and using it for physics puts the contact
// plane at an angle the geometry does not have.
glm::vec3 mesh_normal_at(uint64_t seed, float x, float z);

}  // namespace apricot
