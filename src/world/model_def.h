#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "scene/aabb.h"

namespace pengine {

class Mesh;
class Texture;

// GTA-style world model categories. A model can carry multiple flags (e.g. a
// hill piece tagged Ground|Prop). Used by the streamer for activation rules
// (e.g. Lod twins are placed but only drawn at distance) and by tools that
// want to query "all road meshes".
namespace ModelFlag {
constexpr uint32_t None     = 0;
constexpr uint32_t Building = 1u << 0;
constexpr uint32_t Road     = 1u << 1;
constexpr uint32_t Walk     = 1u << 2;  // sidewalks
constexpr uint32_t Ground   = 1u << 3;  // terrain / hills
constexpr uint32_t Tree     = 1u << 4;
constexpr uint32_t Lod      = 1u << 5;  // is itself the low-detail twin
constexpr uint32_t Prop     = 1u << 6;
}  // namespace ModelFlag

// Definition of one art asset. ModelRegistry owns these; the streamer reads
// them by id when it activates an instance from an IPL file.
struct ModelDef {
    uint32_t    id        = 0;
    std::string name;

    // Asset references. Either a real on-disk path (e.g. "world/road.emesh",
    // "tex/asphalt.png") or a procedural sentinel:
    //   mesh:    "proc:cube"
    //   texture: "proc:asphalt", "proc:grass", "proc:facade",
    //            "proc:sidewalk", "proc:checker"
    std::string mesh_path;
    std::string texture_path;

    float    draw_dist = 300.f;   // metres; renderer fades past this
    uint32_t flags     = 0;
    uint32_t lod_id    = 0;       // 0 = no LOD twin

    glm::vec3 tint     = {1.f, 1.f, 1.f};
    glm::vec2 uv_scale = {1.f, 1.f};

    // Resolved by ModelRegistry::resolve_assets(). Null until then.
    const Mesh*    mesh    = nullptr;
    const Texture* texture = nullptr;
    AABB           local_bounds;
};

// Parse a flag list like "BLDG|ROAD". Unknown tokens are ignored (and warned).
// Returns the bitmask. Accepts: NONE, BLDG, ROAD, WALK, GROUND, TREE, LOD, PROP.
uint32_t parse_model_flags(const std::string& s);

// Pretty-print a flag bitmask like "BLDG|LOD". Returns a thread-local buffer.
const char* format_model_flags(uint32_t flags);

}  // namespace pengine
