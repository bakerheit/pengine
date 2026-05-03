#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "world/model_def.h"

namespace pengine {

class Mesh;
class Texture;

// Registry of all world-model definitions, loaded from one or more IDE files.
// Owns the GPU resources (meshes/textures) it resolves; defs hold borrowed
// pointers into those owned resources. The registry must outlive any IPL
// instance referencing its ids.
//
// IDE text format (whitespace-delimited, '#' comments, blank lines OK):
//
//   # id   name           mesh                   texture            draw  flags     lod
//   10     bldg_cube      proc:cube              proc:facade        300   BLDG      0
//   20     road_slab      proc:cube              proc:asphalt       250   ROAD      0
//
// Optional trailing key=value fields (any order):
//   tint=R,G,B        per-model RGB multiplier   (default 1,1,1)
//   uv=U,V            per-model UV-scale         (default 1,1)
//
// Asset paths are resolved against ASSETS_DIR unless they start with "proc:".
class ModelRegistry {
public:
    bool load_ide(const std::filesystem::path& ide_path);

    // Realises every def's mesh/texture/local_bounds. Must be called on the
    // GL thread. Returns false on first hard failure (missing required asset).
    bool resolve_assets();

    const ModelDef* get(uint32_t id) const;
    std::size_t     size()           const { return defs_.size(); }
    const std::unordered_map<uint32_t, ModelDef>& all() const { return defs_; }

private:
    const Mesh*    fetch_mesh(const std::string& path);
    const Texture* fetch_texture(const std::string& path);

    std::unordered_map<uint32_t, ModelDef>                    defs_;
    std::unordered_map<std::string, std::unique_ptr<Mesh>>    meshes_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures_;
};

}  // namespace pengine
