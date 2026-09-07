#include "gfx/tornado.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "core/log.h"
#include "gfx/gl_state.h"
#include "gfx/tornado_geometry.h"

namespace apricot {
namespace {

const void* attrib_offset(std::size_t bytes) {
    return reinterpret_cast<const void*>(bytes);
}

struct SavedBufferBindings {
    GLint vao = 0;
    GLint array_buffer = 0;

    void capture() {
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    }

    void restore() const {
        gl_state::bind_vertex_array(static_cast<GLuint>(vao));
        gl_state::bind_buffer(GL_ARRAY_BUFFER,
                              static_cast<GLuint>(array_buffer));
    }
};

// Tornado is a self-contained pass. Save every state it changes so callers do
// not need to know which pass ran last, including cached object bindings.
struct SavedRenderState {
    GLboolean depth_test = GL_FALSE;
    GLboolean blend = GL_FALSE;
    GLboolean cull = GL_FALSE;
    GLboolean depth_mask = GL_TRUE;
    GLint depth_func = GL_LESS;
    GLint blend_equation_rgb = GL_FUNC_ADD;
    GLint blend_equation_alpha = GL_FUNC_ADD;
    GLint blend_src_rgb = GL_ONE;
    GLint blend_dst_rgb = GL_ZERO;
    GLint blend_src_alpha = GL_ONE;
    GLint blend_dst_alpha = GL_ZERO;
    GLint program = 0;
    GLint vao = 0;

    void capture() {
        depth_test = glIsEnabled(GL_DEPTH_TEST);
        blend = glIsEnabled(GL_BLEND);
        cull = glIsEnabled(GL_CULL_FACE);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glGetIntegerv(GL_DEPTH_FUNC, &depth_func);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_equation_rgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_equation_alpha);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    }

    void restore() const {
        glDepthFunc(static_cast<GLenum>(depth_func));
        glDepthMask(depth_mask);
        glBlendEquationSeparate(static_cast<GLenum>(blend_equation_rgb),
                                static_cast<GLenum>(blend_equation_alpha));
        glBlendFuncSeparate(static_cast<GLenum>(blend_src_rgb),
                            static_cast<GLenum>(blend_dst_rgb),
                            static_cast<GLenum>(blend_src_alpha),
                            static_cast<GLenum>(blend_dst_alpha));
        depth_test ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        cull ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        gl_state::use_program(static_cast<GLuint>(program));
        gl_state::bind_vertex_array(static_cast<GLuint>(vao));
    }
};

bool finite_position(const glm::vec2& center_xz_m, float ground_y_m) {
    return std::isfinite(center_xz_m.x) && std::isfinite(center_xz_m.y) &&
           std::isfinite(ground_y_m);
}

}  // namespace

TornadoRenderer::~TornadoRenderer() { destroy(); }

bool TornadoRenderer::init() {
    destroy();

    const TornadoMeshData mesh = build_tornado_mesh();
    if (mesh.vertices.empty() ||
        mesh.vertices.size() >
            static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        AP_ERROR("tornado: invalid generated funnel geometry (%zu vertices)",
                 mesh.vertices.size());
        return false;
    }

    if (!shader_.build_from_files("shaders/tornado.vert",
                                  "shaders/tornado.frag")) {
        AP_ERROR("tornado: shader failed to build; funnel is disabled");
        return false;
    }

    SavedBufferBindings saved;
    saved.capture();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (vao_ == 0 || vbo_ == 0) {
        AP_ERROR("tornado: GL refused funnel buffers (vao=%u vbo=%u)", vao_,
                 vbo_);
        destroy();
        saved.restore();
        return false;
    }

    gl_state::bind_vertex_array(vao_);
    gl_state::bind_buffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizeiptr bytes =
        static_cast<GLsizeiptr>(mesh.vertices.size()) *
        static_cast<GLsizeiptr>(sizeof(TornadoVertex));
    glBufferData(GL_ARRAY_BUFFER, bytes, mesh.vertices.data(), GL_STATIC_DRAW);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(TornadoVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(TornadoVertex,
                                                 local_position_m)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(TornadoVertex, funnel_uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(TornadoVertex, layer)));

    vertex_count_ = static_cast<GLsizei>(mesh.vertices.size());
    saved.restore();
    return true;
}

void TornadoRenderer::destroy() {
    shader_.destroy();
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        gl_state::on_buffer_deleted(vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        gl_state::on_vertex_array_deleted(vao_);
        vao_ = 0;
    }
    vertex_count_ = 0;
}

void TornadoRenderer::render(const Camera& camera,
                             const glm::vec2& center_xz_m,
                             float ground_y_m, float intensity,
                             float time_seconds) {
    if (!valid() || !finite_position(center_xz_m, ground_y_m) ||
        !std::isfinite(intensity) || intensity <= 0.0f ||
        !std::isfinite(time_seconds)) {
        return;
    }

    const float strength = std::clamp(intensity, 0.0f, 1.0f);
    const glm::vec3 center_ground{
        center_xz_m.x,
        ground_y_m,
        center_xz_m.y,
    };

    SavedRenderState saved;
    saved.capture();

    shader_.bind();
    shader_.set_mat4("u_view_proj", camera.view_projection());
    shader_.set_vec3("u_center_ground", center_ground);
    shader_.set_vec3("u_camera_position", camera.position);
    shader_.set_float("u_intensity", strength);
    shader_.set_float("u_time", time_seconds);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    // Funnel shells must be hidden by opaque world depth, but translucent
    // layers must not hide one another or block later translucent effects.
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    // The camera can enter the funnel, and both sides of each smoke shell are
    // meaningful. Culling either side makes half the effect disappear.
    glDisable(GL_CULL_FACE);

    gl_state::bind_vertex_array(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertex_count_);

    saved.restore();
}

}  // namespace apricot
