#pragma once

#include <string>
#include <vector>

#include "render/mesh.h"

namespace pengine {

class Shader;

struct Submesh {
    Mesh        mesh;
    std::string material_name;
};

class Model {
public:
    Model()  = default;
    ~Model() = default;

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    bool load(const std::string& emesh_path);

    // Draws all submeshes. Caller is responsible for binding shader + textures.
    void draw() const;

    const std::vector<Submesh>& submeshes() const { return submeshes_; }
    std::size_t submesh_count() const { return submeshes_.size(); }

private:
    std::vector<Submesh> submeshes_;
};

} // namespace pengine
