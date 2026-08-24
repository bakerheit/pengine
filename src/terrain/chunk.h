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

// ---------------------------------------------------------------------------
//  Level of detail
// ---------------------------------------------------------------------------
//
// A chunk at level L samples every (1 << L)-th point of the LOD-0 lattice.
// kChunkQuads is a power of two, so every level divides it exactly and a
// coarse chunk's vertices are a strict SUBSET of the fine lattice — sampled at
// the identical absolute world coordinates, so height_at() returns the
// identical bits. Nothing is filtered, averaged or decimated: a coarse chunk
// asks the same pure function fewer questions.
//
// That subset property is the whole design. It is what keeps two chunks at the
// SAME level closing exactly, exactly as they did before LOD existed, and it
// is what makes the crack between two DIFFERENT levels a bounded, measurable
// quantity rather than an open-ended one.

// Coarsest level. 3 is 8 m between vertices: 81 vertices against 4225, which
// is the 1/64 density ring docs/design/pinatty.md asks for.
inline constexpr int kMaxChunkLod = 3;

// Lattice stride, quad count and vertex count per edge at a level.
constexpr int lod_step(int lod) { return 1 << lod; }
constexpr int lod_quads(int lod) { return kChunkQuads >> lod; }
constexpr int lod_verts(int lod) { return lod_quads(lod) + 1; }

static_assert(lod_quads(kMaxChunkLod) * lod_step(kMaxChunkLod) == kChunkQuads,
              "every LOD stride must divide kChunkQuads exactly, or a coarse "
              "chunk's last column lands off the global lattice and the seam "
              "this whole scheme protects stops closing");

// Metres between vertices at a level.
constexpr float lod_spacing_metres(int lod) {
    return kVertexSpacingMetres * static_cast<float>(lod_step(lod));
}

// ---------------------------------------------------------------------------
//  Skirts, and why they and not a stitched row
// ---------------------------------------------------------------------------
//
// Two chunks at different levels do not share vertices along their common
// edge. The coarse side draws a chord where the fine side draws the field, and
// between them is a crack you can see the sky through.
//
// THE FIX IS NOT TO AVERAGE. docs/architecture.md is explicit and it is right:
// averaging across a boundary hides the crack and keeps its cause, and it
// destroys the one property that makes any of this reproducible — that a
// vertex is height_at() at its own absolute world coordinate and nothing else.
//
// So every chunk hangs a SKIRT: a vertical curtain around its perimeter,
// dropped straight down from the edge vertices it already has. It adds
// geometry; it modifies none. The crack is still there in the mathematical
// sense and the skirt simply fills the hole with ground-coloured ground.
//
// The alternative considered and rejected was an explicitly stitched
// transition row, where a chunk emits a matching edge for whatever its
// neighbour's level happens to be. It closes the crack exactly, and it costs
// the purity: build_chunk() would take the four neighbouring levels as
// arguments, and every chunk on a ring boundary would need a full rebuild
// whenever a NEIGHBOUR changed level, not just when it did. That is a large
// multiplier on rebuild churn at exactly the radius where the player is moving
// fastest through rings, bought for a seam a skirt already hides.
//
// SKIRT DEPTH IS MEASURED, NOT GUESSED. See build_chunk().

// Floor on skirt depth, in metres. Flat ground has no crack to hide but a
// grazing view along a boundary can still catch a hairline of background
// through the shared edge, so the curtain is never zero.
inline constexpr float kMinSkirtMetres = 0.5f;

// Safety factor on the measured perimeter relief.
//
// The relief measured over the coarsest neighbour's cell width is an upper
// bound on the chord error in the usual case and not a proof of one, because
// the two sides sample different points. 1.5 buys the headroom;
// tests/terrain_lod_tests.cpp prints the margin that actually remains at every
// level pairing over real terrain, so this is a number with evidence under it
// rather than a number somebody felt good about.
inline constexpr float kSkirtReliefFactor = 1.5f;

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
    int lod = 0;
    std::vector<TerrainVertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;  // world space, for culling and streaming decisions

    // Indices belonging to the TOP SURFACE. The skirt's indices follow them,
    // so [0, surface_index_count) is the ground and the rest is curtain.
    //
    // This exists so build_chunk_collision() can be structurally unable to put
    // a skirt triangle into a collision set. A skirt is vertical: its face
    // normal is horizontal, and a set whose entire contract is "normal.y > 0,
    // because a height field cannot overhang" would have to flip it to obey
    // that contract, producing a contact plane the geometry does not have.
    std::size_t surface_index_count = 0;

    // How far the skirt hangs below the perimeter, in metres. Reported rather
    // than recomputed because the seam test asserts against it directly: the
    // guarantee is "the crack at this boundary is never deeper than this".
    float skirt_depth = 0.0f;

    // Bytes this mesh will occupy once uploaded. The streamer's memory
    // reporting is only honest if it comes from the arrays themselves.
    std::size_t gpu_bytes() const {
        return vertices.size() * sizeof(TerrainVertex) +
               indices.size() * sizeof(uint32_t);
    }
};

// Build one chunk's mesh at a level of detail. PURE in (seed, coord, lod) — no
// clock, no shared state, no allocation beyond the returned buffers. Safe to
// call from a worker thread, which is the entire point.
//
// `lod` is clamped to [0, kMaxChunkLod]. Level L samples every (1 << L)-th
// point of the global lattice, at absolute world coordinates, so a coarse
// chunk's vertices are bit-identical to the fine chunk's at the points they
// share.
//
// SKIRT DEPTH IS MEASURED FROM THE CHUNK'S OWN PERIMETER, not from a constant.
// A chunk on flat coast gets the floor; a chunk on a ridge face gets a curtain
// proportional to the relief that is actually there. The measurement is the
// largest height step between neighbouring perimeter vertices at this chunk's
// own spacing — which is the scale of the chord error a coarser neighbour can
// introduce — times kSkirtReliefFactor. It uses vertices the mesher has
// already built, so it costs no extra height_at() calls.
ChunkMesh build_chunk(uint64_t seed, ChunkCoord coord, int lod = 0);

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
//
// TWO THINGS LOD ADDED TO THAT PROMISE.
//
// It reads only [0, surface_index_count), so a skirt triangle cannot enter a
// collision set — see the note on that field.
//
// And it REFUSES a mesh with lod > 0, loudly, returning nothing. A coarsened
// mesh is exactly the "downsampled copy" this engine forbids collision from
// using: the car would rest on a chord while the player watches it float over
// the ridge that chord cuts. The streamer keeps the chunks the player can
// touch at level 0 for precisely this reason, so a caller who gets here with a
// coarse mesh has a bug and wants to hear about it rather than get plausible
// triangles back.
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

// Height of the drawn triangle at a world XZ, AT A GIVEN LEVEL OF DETAIL.
//
// THIS IS A MEASURING INSTRUMENT AND NOT A GROUND QUERY. It exists so that
// "how far does the drawn surface move when a chunk changes level" is a number
// somebody can print, rather than a thing argued about — and that number is
// what decides how far out anything draped on the terrain (a road ribbon, a
// prop, a decal) can be drawn before it visibly floats.
//
// DO NOT FEED IT TO PHYSICS. Contact goes through mesh_height_at(), which is
// this at level 0, because the streamer guarantees level 0 under the car and
// collision must never come from a coarsened surface. A caller passing a
// non-zero level here and using the answer for contact has re-created the
// downsampled-collision bug by hand.
float mesh_height_at_lod(uint64_t seed, float x, float z, int lod);

// Face normal of the drawn triangle at a world XZ. Piecewise constant across
// each triangle, which is exactly right for contact response — the smooth
// normal_at() is a SHADING normal and using it for physics puts the contact
// plane at an angle the geometry does not have.
glm::vec3 mesh_normal_at(uint64_t seed, float x, float z);

}  // namespace apricot
