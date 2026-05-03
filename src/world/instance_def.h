#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "scene/transform.h"

namespace pengine {

// One placed object inside a world cell. Resolves through ModelRegistry by
// model_id at activation time. The intent (matching GTA's IPL semantics) is
// that every visual property lives on the ModelDef; instances vary only in
// transform.
//
// Exception, transitional: while we're still using a 1m procedural cube
// scaled to building/road/sidewalk dimensions, the texture's effective tile
// rate depends on instance scale. uv_scale_override == (0,0) means "use the
// model's value"; otherwise it overrides the model's uv_scale at activation.
// Once we have real meshes with proper UVs (step 6), this field goes away.
struct InstanceDef {
    uint32_t   model_id          = 0;
    Transform  transform;
    glm::vec2  uv_scale_override = {0.f, 0.f};
    uint32_t   lod_pair          = 0xFFFFFFFFu;  // unused yet; index in cell list
};

}  // namespace pengine
