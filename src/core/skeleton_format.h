#pragma once

#include <cstdint>

// Binary .eskel format — bone hierarchy + inverse-bind matrices.
//
// File layout:
//   EskelHeader
//   EskelBone[bone_count]
//   char[string_block_size]              // null-terminated bone name strings
//
// inv_bind is the inverse of the bone's world-bind-pose matrix. To compute a
// final skinning matrix at frame time:
//
//     skin[b] = world_pose[b] * inv_bind[b]
//
// where world_pose[b] is the bone's current world transform sampled from the
// animation. world_pose is built top-down by accumulating local poses through
// the parent chain.

namespace pengine {

constexpr uint32_t ESKEL_MAGIC   = 0x4C4B5345u; // 'ESKL'
constexpr uint32_t ESKEL_VERSION = 1u;

struct EskelBone {
    int32_t  parent;           // -1 for root; otherwise an index into the bones array
    uint32_t name_offset;      // byte offset into string block (null-terminated)
    float    inv_bind[16];     // mesh-space → bone-space at bind time (mat4, col-major)
    float    bind_local[16];   // local-to-parent transform at bind time (mat4, col-major)
};

struct EskelHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t bone_count;
    uint32_t string_block_size;
};

static_assert(sizeof(EskelHeader) == 16, "skel header size");
static_assert(sizeof(EskelBone)   == 136, "skel bone size");

} // namespace pengine
