#pragma once

#include <cstddef>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace apricot {

// One instance of the forward-lit instanced draw path.
//
// THE LAYOUT IS A THREE-WAY CONTRACT. Change anything here and you must change
// all three, in the same commit:
//   1. this struct,
//   2. Mesh::upload_instances()'s attribute wiring (gfx/mesh.cpp),
//   3. assets/shaders/lit_instanced.vert's `layout(location = ...)` inputs.
//
// Attribute map, all divisor 1. Locations 0-2 are per-vertex and 3 is reserved
// for a tangent, so the instance block starts at 4:
//   4-7    model         mat4, one vec4 column per location
//   8-10   normal_c0..2  columns of the world normal matrix (xyz used, w pad)
//   11     tint          vec4, RGB multiplied into albedo, A into output alpha
//   12     uv_scale      vec2, tiles the diffuse texture
//
// tint and uv_scale ride HERE rather than in a per-draw uniform for one reason:
// scene/draw_batch.h deliberately keeps them out of the batch key, so two nodes
// differing only in colour or tiling still collapse into a single draw. Promote
// either one to a uniform and batching silently degrades to one draw per
// object while continuing to "work".
struct InstanceData {
    glm::mat4 model;
    glm::vec4 normal_c0;
    glm::vec4 normal_c1;
    glm::vec4 normal_c2;
    glm::vec4 tint;
    glm::vec2 uv_scale;
};

// The stride the attribute wiring uses. glm's default vec4 is 4-byte aligned
// (only the explicitly aligned_* types are over-aligned), so this packs with no
// padding at all: 64 + 16 + 16 + 16 + 16 + 8 = 136. If this assert ever fires,
// somebody either added a field or switched glm to the aligned types — either
// way the attribute wiring in mesh.cpp needs revisiting, which is the point.
static_assert(sizeof(InstanceData) == 136, "instance stride contract");
static_assert(offsetof(InstanceData, model) == 0, "instance attrib offset");
static_assert(offsetof(InstanceData, normal_c0) == 64, "instance attrib offset");
static_assert(offsetof(InstanceData, normal_c1) == 80, "instance attrib offset");
static_assert(offsetof(InstanceData, normal_c2) == 96, "instance attrib offset");
static_assert(offsetof(InstanceData, tint) == 112, "instance attrib offset");
static_assert(offsetof(InstanceData, uv_scale) == 128, "instance attrib offset");

// Build one instance record from a world matrix.
//
// The normal matrix is a full inverse-transpose, NOT mat3(model). The scenery
// is non-uniformly scaled boxes; under mat3(model) a box stretched along one
// axis lights as though its faces were sloped, and the error is subtle enough
// to read as "the lighting looks a bit off" rather than as a bug.
inline InstanceData make_instance(const glm::mat4& model, const glm::vec4& tint,
                                  const glm::vec2& uv_scale) {
    const glm::mat3 n = glm::inverseTranspose(glm::mat3(model));
    InstanceData d;
    d.model = model;
    d.normal_c0 = glm::vec4(n[0], 0.0f);
    d.normal_c1 = glm::vec4(n[1], 0.0f);
    d.normal_c2 = glm::vec4(n[2], 0.0f);
    d.tint = tint;
    d.uv_scale = uv_scale;
    return d;
}

}  // namespace apricot
