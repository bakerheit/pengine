#pragma once

#include <string>
#include <memory>

#include "render/texture.h"

namespace pengine {

class Shader;

// Simple key=value material descriptor.
// Format (one per line, # for comments):
//   shader   = lit
//   diffuse  = textures/body.png
//   normal   = textures/body_n.png
//   specular = textures/body_s.png
struct MaterialDef {
    std::string shader_name = "lit";
    std::string diffuse_path;
    std::string normal_path;
    std::string specular_path;

    bool load(const std::string& path);
};

// A fully-loaded material ready to bind.
struct Material {
    MaterialDef         def;
    std::shared_ptr<Texture> diffuse;
    std::shared_ptr<Texture> normal;
    std::shared_ptr<Texture> specular;

    // Load def + all referenced textures.
    bool load(const std::string& mat_path, const std::string& assets_root);

    // Bind textures to fixed units and set sampler uniforms.
    void bind(Shader& shader) const;
};

} // namespace pengine
