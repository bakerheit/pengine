#include "gfx/hud.h"

#include <cstddef>

#include "core/log.h"
#include "gfx/gl_state.h"
#include "gfx/glyph_atlas.h"

namespace apricot {
namespace {

// The texture unit the HUD atlas lives on for the duration of a pass.
constexpr GLuint kAtlasUnit = 0;

const void* attrib_offset(std::size_t bytes) {
    return reinterpret_cast<const void*>(bytes);
}

}  // namespace

Hud::~Hud() { destroy(); }

bool Hud::init() {
    if (!shader_.build_from_files("shaders/hud.vert", "shaders/hud.frag")) {
        AP_ERROR("hud: shader failed to build; there will be no HUD");
        return false;
    }

    const std::vector<uint8_t> pixels = build_glyph_atlas();
    if (!atlas_.upload_r8(kAtlasW, kAtlasH, pixels, /*smooth=*/true)) {
        AP_ERROR("hud: glyph atlas upload failed");
        shader_.destroy();
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (!vao_ || !vbo_) {
        AP_ERROR("hud: GL refused to create the batch buffers");
        destroy();
        return false;
    }

    gl_state::bind_vertex_array(vao_);
    gl_state::bind_buffer(GL_ARRAY_BUFFER, vbo_);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(Vertex, color)));

    gl_state::bind_vertex_array(0);

    AP_INFO("hud: %dx%d procedural glyph atlas, %d glyphs, no font files",
            kAtlasW, kAtlasH, kCharCount);
    return true;
}

void Hud::destroy() {
    shader_.destroy();
    atlas_.destroy();
    // Every delete pairs with its gl_state hook. See the warning in gl_state.h.
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        gl_state::on_buffer_deleted(vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        gl_state::on_vertex_array_deleted(vao_);
        vao_ = 0;
    }
    vbo_capacity_bytes_ = 0;
    verts_.clear();
    in_pass_ = false;
}

void Hud::begin(glm::vec2 viewport_px) {
    if (in_pass_) {
        AP_ERROR("hud: begin() called twice without an end(); ignoring");
        return;
    }
    verts_.clear();
    viewport_ = viewport_px;
    in_pass_ = valid() && viewport_px.x > 0.0f && viewport_px.y > 0.0f;
    if (!in_pass_) return;

    glGetBooleanv(GL_DEPTH_TEST, &depth_was_);
    glGetBooleanv(GL_CULL_FACE, &cull_was_);
    glGetBooleanv(GL_BLEND, &blend_was_);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask_was_);
}

void Hud::push_quad(glm::vec2 min_px, glm::vec2 max_px, float u0, float v0,
                    float u1, float v1, glm::vec4 color) {
    if (!in_pass_) return;
    // A zero-area quad contributes nothing but still costs six vertices and a
    // rasteriser setup; drop it here rather than in the driver.
    if (!(max_px.x > min_px.x) || !(max_px.y > min_px.y)) return;

    const Vertex a{{min_px.x, min_px.y}, {u0, v0}, color};
    const Vertex b{{max_px.x, min_px.y}, {u1, v0}, color};
    const Vertex c{{max_px.x, max_px.y}, {u1, v1}, color};
    const Vertex d{{min_px.x, max_px.y}, {u0, v1}, color};

    verts_.push_back(a);
    verts_.push_back(b);
    verts_.push_back(c);
    verts_.push_back(a);
    verts_.push_back(c);
    verts_.push_back(d);
}

void Hud::rect(glm::vec2 min_px, glm::vec2 max_px, glm::vec4 color) {
    const GlyphUV s = solid_uv();
    push_quad(min_px, max_px, s.u0, s.v0, s.u1, s.v1, color);
}

void Hud::outline(glm::vec2 min_px, glm::vec2 max_px, float thickness,
                  glm::vec4 color) {
    if (thickness <= 0.0f) return;
    const float t = thickness;
    rect({min_px.x, min_px.y}, {max_px.x, min_px.y + t}, color);            // top
    rect({min_px.x, max_px.y - t}, {max_px.x, max_px.y}, color);            // bottom
    rect({min_px.x, min_px.y + t}, {min_px.x + t, max_px.y - t}, color);    // left
    rect({max_px.x - t, min_px.y + t}, {max_px.x, max_px.y - t}, color);    // right
}

float Hud::text(const char* s, glm::vec2 top_left_px, float glyph_h_px,
                glm::vec4 color) {
    if (!s || !*s || glyph_h_px <= 0.0f) return 0.0f;

    const float advance = glyph_advance_px(glyph_h_px);
    const float width = glyph_width_px(glyph_h_px);

    float x = top_left_px.x;
    for (const char* p = s; *p; ++p) {
        // Space has an empty bitmap, so pushing it would queue a quad that
        // covers nothing. Skip straight to the advance.
        if (*p != ' ') {
            const GlyphUV uv = glyph_uv(*p);
            push_quad({x, top_left_px.y}, {x + width, top_left_px.y + glyph_h_px},
                      uv.u0, uv.v0, uv.u1, uv.v1, color);
        }
        x += advance;
    }
    return x - top_left_px.x;
}

float Hud::text_centered(const char* s, float centre_x_px, float top_y_px,
                         float glyph_h_px, glm::vec4 color) {
    const float w = text_width_px(s, glyph_h_px);
    return text(s, {centre_x_px - w * 0.5f, top_y_px}, glyph_h_px, color);
}

void Hud::end() {
    last_quad_count_ = static_cast<int>(verts_.size() / 6u);
    last_draw_calls_ = 0;

    if (!in_pass_) {
        verts_.clear();
        return;
    }
    in_pass_ = false;

    if (!verts_.empty()) {
        gl_state::bind_vertex_array(vao_);
        gl_state::bind_buffer(GL_ARRAY_BUFFER, vbo_);

        const std::size_t bytes = verts_.size() * sizeof(Vertex);
        if (bytes > vbo_capacity_bytes_) {
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes),
                         verts_.data(), GL_STREAM_DRAW);
            vbo_capacity_bytes_ = bytes;
        } else {
            // Orphan then fill; the previous frame's draw may still be reading.
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vbo_capacity_bytes_), nullptr,
                         GL_STREAM_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                            verts_.data());
        }

        shader_.bind();
        shader_.set_vec2("u_viewport_px", viewport_);
        atlas_.bind(kAtlasUnit);
        shader_.set_int("u_atlas", static_cast<int>(kAtlasUnit));

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
        last_draw_calls_ = 1;
    }

    // Restore exactly what was there. The HUD runs between the world pass and
    // whatever the debug UI does; leaving depth off here would blank the next
    // frame's world and the cause would look like a renderer bug.
    if (depth_was_) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cull_was_) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (blend_was_) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glDepthMask(depth_mask_was_);

    verts_.clear();
}

}  // namespace apricot
