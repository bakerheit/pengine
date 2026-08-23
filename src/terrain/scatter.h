#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "terrain/chunk.h"
#include "terrain/surface.h"

namespace apricot {

// Rocks and trees, placed procedurally.
//
// PURE in (seed, chunk coord), like everything else here, and for the same
// reason: a chunk the player drives back to must come back identical, and a
// chunk approached from the north must generate identically to one approached
// from the south. Entropy is hash_coord()-keyed to a world CELL, never pulled
// from a sequential stream — a stream's value depends on how many times it has
// been drawn from, which is precisely the approach order nobody controls.

enum class PropKind : uint8_t {
    Tree = 0,
    Rock = 1,
};

inline constexpr std::size_t kPropKindCount = 2;

inline constexpr std::size_t prop_index(PropKind k) {
    return static_cast<std::size_t>(k);
}

// Placement grid pitch, in metres. Divides kChunkMetres exactly, so every cell
// belongs to exactly one chunk: no prop is emitted twice at a chunk boundary
// and none falls down the gap between two chunks.
inline constexpr float kScatterCellMetres = 4.0f;
inline constexpr int kScatterCellsPerChunk =
    static_cast<int>(kChunkMetres / kScatterCellMetres);

// Upper bound on props from one chunk: one per cell. The streamer sizes its
// instance budget against this, so it must stay true — if a cell ever emits
// more than one prop, this constant has to grow with it.
inline constexpr std::size_t kMaxPropsPerChunk =
    static_cast<std::size_t>(kScatterCellsPerChunk) *
    static_cast<std::size_t>(kScatterCellsPerChunk);

// Model variants per kind. The sim does not know what a variant looks like; it
// picks an index and the renderer owns the meaning.
inline constexpr uint8_t kTreeVariants = 4;
inline constexpr uint8_t kRockVariants = 3;

struct ScatterProp {
    PropKind kind = PropKind::Tree;
    uint8_t variant = 0;

    // What it is standing on. Carried rather than re-queried because the
    // placer already knows, and because a prop whose ground material differs
    // from the ground under it is a bug that is hard to see and easy to make.
    Surface ground = Surface::Grass;

    // World position, with y ON THE MESHED SURFACE — mesh_height_at(), not
    // height_at(). Sampling the continuous field instead leaves every prop
    // floating or buried by the difference between the field and the planar
    // triangle actually drawn beneath it, which on a slope is centimetres and
    // on a ridge is worse.
    glm::vec3 position{0.0f};

    float yaw = 0.0f;    // radians about +Y
    float scale = 1.0f;  // uniform
};

// Nominal unscaled dimensions and draw policy for a prop kind.
//
// The sim needs bounds to build a scene node's AABB and a distance at which to
// stop drawing it; it does not need the model. These are the contract between
// "the placer decided a tree goes here" and "the renderer knows how big a tree
// is", and they live on the sim side so culling stays headless-testable.
struct PropDims {
    float radius;  // horizontal half-extent, metres, at scale 1
    float height;  // vertical extent above the ground point, metres, at scale 1

    // Per-node draw distance in metres. Only ever SHORTENS visibility against
    // the global limit — see Scene::cull. Small props leave the draw list long
    // before the terrain they sit on does, which is most of what makes a
    // scattered world affordable.
    float draw_distance;
};

const PropDims& prop_dims(PropKind k);

// Every prop belonging to one chunk, in a fully determined order (rows of
// cells, +Z outer, +X inner). PURE in (seed, coord).
std::vector<ScatterProp> scatter_chunk(uint64_t seed, ChunkCoord coord);

}  // namespace apricot
