#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/aabb.h"
#include "core/emesh_format.h"

namespace apricot {

// CPU-side static geometry read from one cooked .emesh file. The reader lives
// below the renderer so malformed or stale assets can be checked headlessly;
// only gfx/ turns this plain data into GPU buffers.
struct StaticEmesh {
    std::vector<EmeshVertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;
};

// CPU-side rigged geometry. Bone transforms deliberately stay out of the mesh
// file: .emesh owns bind-space vertices and weights, while .eskel/.eanim own
// the hierarchy and motion. Keeping this as plain data lets the same parser be
// exercised by headless tests and the Asset Lab before gfx uploads anything.
struct SkinnedEmesh {
    std::vector<EmeshSkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;
};

// Read and validate a static .emesh. On failure `out` is left unchanged.
// Submeshes are deliberately merged: Apricot's current material contract is
// one diffuse per SceneNode, and the first imported vehicle assets each carry
// exactly one submesh.
bool read_static_emesh(const std::string& path, StaticEmesh& out);

// Read and validate a rigged .emesh. On failure `out` is left unchanged.
bool read_skinned_emesh(const std::string& path, SkinnedEmesh& out);

}  // namespace apricot
