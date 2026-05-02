#pragma once

#include <cstdint>

// Binary .emesh format — little-endian, mmap-friendly.
//
// File layout:
//   EmeshHeader
//   if (flags & EMESH_FLAG_SKINNED)
//     EmeshSkinnedVertex[vertex_count]   // 68 B each
//   else
//     EmeshVertex[vertex_count]          // 48 B each
//   uint32_t[index_count]
//   EmeshSubmesh[submesh_count]
//   char[string_block_size]              // null-terminated material name strings
//
// No internal pointers — safe to mmap and use directly.
//
// Bones, animations: not in this file — see skeleton_format.h / anim_format.h
// (separate sidecar files .eskel / .eanim).

namespace pengine {

constexpr uint32_t EMESH_MAGIC   = 0x48534D45u; // 'EMSH'
constexpr uint32_t EMESH_VERSION = 2u;

constexpr uint32_t EMESH_FLAG_SKINNED = 0x1u; // vertices include bone idx + weights

struct EmeshVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float tx, ty, tz, tw;
};

struct EmeshSkinnedVertex {
    float    px, py, pz;
    float    nx, ny, nz;
    float    u, v;
    float    tx, ty, tz, tw;
    uint8_t  bone_idx[4];     // up to 4 bones per vertex
    float    bone_weight[4];
};

struct EmeshSubmesh {
    uint32_t index_offset;
    uint32_t index_count;
    uint32_t material_name_offset;
    uint32_t _pad = 0;
};

struct EmeshHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;             // EMESH_FLAG_*
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t submesh_count;
    uint32_t string_block_size;
    uint32_t _pad = 0;          // 8-byte alignment
};

static_assert(sizeof(EmeshHeader)        == 32, "header size");
static_assert(sizeof(EmeshVertex)        == 48, "static vertex size");
static_assert(sizeof(EmeshSkinnedVertex) == 68, "skinned vertex size");
static_assert(sizeof(EmeshSubmesh)       == 16, "submesh size");

} // namespace pengine
