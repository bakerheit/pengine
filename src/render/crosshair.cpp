#include "render/crosshair.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "core/log.h"

namespace pengine {

namespace {

constexpr float CROSSHAIR_RADIUS_PX = 14.f; // outer ring radius in pixels
constexpr float INNER_LOCAL         = 0.78f; // inner edge as fraction of outer
constexpr int   RING_SEGMENTS       = 48;

constexpr glm::vec3 RING_COLOR{0.96f, 0.97f, 0.98f};

} // namespace

bool Crosshair::init(const std::string& assets_root) {
    if (!shader_.load(assets_root + "/shaders/minimap.vert",
                      assets_root + "/shaders/minimap.frag")) {
        PE_ERROR("Crosshair: failed to load shaders");
        return false;
    }

    std::vector<Vertex> verts;
    verts.reserve(RING_SEGMENTS * 6);
    for (int i = 0; i < RING_SEGMENTS; ++i) {
        float a0 = static_cast<float>(i)     / RING_SEGMENTS * glm::two_pi<float>();
        float a1 = static_cast<float>(i + 1) / RING_SEGMENTS * glm::two_pi<float>();
        glm::vec2 d0{std::cos(a0), std::sin(a0)};
        glm::vec2 d1{std::cos(a1), std::sin(a1)};
        glm::vec2 inner0 = d0 * INNER_LOCAL;
        glm::vec2 inner1 = d1 * INNER_LOCAL;
        glm::vec2 outer0 = d0;
        glm::vec2 outer1 = d1;
        verts.push_back({inner0, RING_COLOR});
        verts.push_back({outer0, RING_COLOR});
        verts.push_back({outer1, RING_COLOR});
        verts.push_back({inner0, RING_COLOR});
        verts.push_back({outer1, RING_COLOR});
        verts.push_back({inner1, RING_COLOR});
    }
    vertex_count_ = static_cast<GLsizei>(verts.size());

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                 verts.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, color)));

    glBindVertexArray(0);
    return true;
}

void Crosshair::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_);      vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

void Crosshair::draw(const DrawState& s) {
    if (s.viewport_size_px.x <= 0.f || s.viewport_size_px.y <= 0.f) return;
    if (vertex_count_ == 0) return;

    GLboolean depth_was = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cull_was  = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    shader_.use();
    glm::vec2 centre = s.viewport_size_px * 0.5f;
    shader_.set("u_screen_centre_px", centre);
    shader_.set("u_radius_px",        CROSSHAIR_RADIUS_PX);
    shader_.set("u_viewport_px",      s.viewport_size_px);
    // Disable the minimap rim/clip — crosshair geometry already fits inside
    // the unit circle and we don't want the rim darkening artifact.
    shader_.set("u_clip_radius",      10.0f);
    shader_.set("u_rim_radius",       10.0f);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count_);
    glBindVertexArray(0);

    if (depth_was) glEnable(GL_DEPTH_TEST);
    if (cull_was)  glEnable(GL_CULL_FACE);
}

} // namespace pengine
