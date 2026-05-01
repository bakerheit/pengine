#pragma once

#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "render/shader.h"

namespace pengine {

// Immediate-style line renderer for debugging. Submit lines per frame, then
// flush() once after the main scene to draw them on top.
class DebugDraw {
public:
    bool init(const std::string& assets_root);
    void shutdown();

    void clear();
    void line(const glm::vec3& a, const glm::vec3& b);
    void box (const glm::vec3& min, const glm::vec3& max);
    void cylinder_xz(const glm::vec3& base, float radius, float height, int sides = 16);
    void cross(const glm::vec3& p, float size);

    // Draws all submitted lines with `color`. Disables depth-test so debug
    // geometry is always visible.
    void flush(const glm::mat4& view_proj, const glm::vec3& color);

private:
    GLuint                   vao_ = 0;
    GLuint                   vbo_ = 0;
    std::size_t              vbo_capacity_ = 0;
    std::vector<glm::vec3>   verts_;
    Shader                   shader_;
};

} // namespace pengine
