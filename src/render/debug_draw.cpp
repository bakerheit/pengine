#include "render/debug_draw.h"

#include <cmath>

namespace pengine {

bool DebugDraw::init(const std::string& assets_root) {
    if (!shader_.load(assets_root + "/shaders/debug.vert",
                      assets_root + "/shaders/debug.frag")) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);
    return true;
}

void DebugDraw::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    shader_.destroy();
}

void DebugDraw::clear() { verts_.clear(); }

void DebugDraw::line(const glm::vec3& a, const glm::vec3& b) {
    verts_.push_back(a);
    verts_.push_back(b);
}

void DebugDraw::box(const glm::vec3& mn, const glm::vec3& mx) {
    glm::vec3 c[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
        {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
    };
    int e[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4}, {0,4},{1,5},{2,6},{3,7}
    };
    for (auto& p : e) line(c[p[0]], c[p[1]]);
}

void DebugDraw::cylinder_xz(const glm::vec3& base, float radius, float height, int sides) {
    glm::vec3 top = base + glm::vec3{0.f, height, 0.f};
    glm::vec3 prev_b{}, prev_t{};
    for (int i = 0; i <= sides; ++i) {
        float a = static_cast<float>(i) / static_cast<float>(sides) * 6.2831853f;
        glm::vec3 off{ std::cos(a) * radius, 0.f, std::sin(a) * radius };
        glm::vec3 b = base + off;
        glm::vec3 t = top  + off;
        if (i > 0) {
            line(prev_b, b);
            line(prev_t, t);
            line(b, t);
        }
        prev_b = b; prev_t = t;
    }
}

void DebugDraw::cross(const glm::vec3& p, float s) {
    line(p - glm::vec3{s,0,0}, p + glm::vec3{s,0,0});
    line(p - glm::vec3{0,s,0}, p + glm::vec3{0,s,0});
    line(p - glm::vec3{0,0,s}, p + glm::vec3{0,0,s});
}

void DebugDraw::flush(const glm::mat4& view_proj, const glm::vec3& color) {
    if (verts_.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    std::size_t bytes = verts_.size() * sizeof(glm::vec3);
    if (bytes > vbo_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), verts_.data(), GL_DYNAMIC_DRAW);
        vbo_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), verts_.data());
    }

    GLboolean depth_was_on = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);

    shader_.use();
    shader_.set("u_view_proj", view_proj);
    shader_.set("u_color",     color);

    glBindVertexArray(vao_);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts_.size()));
    glBindVertexArray(0);

    if (depth_was_on) glEnable(GL_DEPTH_TEST);
}

} // namespace pengine
