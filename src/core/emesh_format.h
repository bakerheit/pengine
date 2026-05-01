#pragma once

#include <cstdint>

// Binary .emesh format — little-endian, mmap-friendly.
//
// File layout:
//   EmeshHeader
//   Vertex[header.vertex_count]          (sizeof(EmeshVertex) each)
//   uint32_t[header.index_count]
//   EmeshSubmesh[header.submesh_count]
//   char[header.string_block_size]       (null-terminated material name strings)
//
// All offsets/counts are absolute within their section.
// No internal pointers — safe to mmap and use directly.

namespace pengine {

constexpr uint32_t EMESH_MAGIC   = 0x48534D45u; // 'EMSH'
constexpr uint32_t EMESH_VERSION = 1u;

struct EmeshVertex {
    float px, py, pz;   // position
    float nx, ny, nz;   // normal
    float u, v;          // uv
    float tx, ty, tz, tw; // tangent (w = bitangent sign)
};

struct EmeshSubmesh {
    uint32_t index_offset;         // first index in the index section
    uint32_t index_count;
    uint32_t material_name_offset; // byte offset into string block (null-terminated)
    uint32_t _pad = 0;
};

struct EmeshHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t submesh_count;
    uint32_t string_block_size;
};
static_assert(sizeof(EmeshHeader)  == 24, "header size");
static_assert(sizeof(EmeshVertex)  == 48, "vertex size");
static_assert(sizeof(EmeshSubmesh) == 16, "submesh size");

} // namespace pengine
