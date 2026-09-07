#include "gfx/hud.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core/asset_root.h"
#include "core/log.h"
#include "gfx/font_atlas.h"
#include "gfx/gl_state.h"
#include "gfx/glyph_atlas.h"

namespace apricot {
namespace {

// The texture unit the HUD atlas lives on for the duration of a pass.
constexpr GLuint kAtlasUnit = 0;

// Global presentation scale for regular game UI copy. Keeping this here makes
// every HUD, menu, map label and prompt grow together without changing the
// logo art or the layout code's authored sizes.
constexpr float kUiTextScale = 1.21f;

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

    using_ui_font_ = build_ui_font_atlas(
        asset_path("fonts/BebasNeue-Regular.ttf"),
        asset_path("fonts/Righteous-Regular.ttf"),
        asset_path("fonts/MaterialSymbolsSharp-HUD.ttf"), ui_font_);
    const std::vector<uint8_t> fallback_pixels =
        using_ui_font_ ? std::vector<uint8_t>{} : build_glyph_atlas();
    const std::vector<uint8_t>& pixels =
        using_ui_font_ ? ui_font_.pixels : fallback_pixels;
    const int atlas_width = using_ui_font_ ? kUiFontAtlasW : kAtlasW;
    const int atlas_height = using_ui_font_ ? kUiFontAtlasH : kAtlasH;
    if (!atlas_.upload_r8(atlas_width, atlas_height, pixels, /*smooth=*/true)) {
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

    if (using_ui_font_) {
        AP_INFO("hud: %dx%d Bebas Neue + %s title atlas, %d glyphs, %zu Material Symbols",
                atlas_width, atlas_height,
                ui_font_.title_available ? "Righteous" : "fallback",
                kCharCount,
                ui_font_.symbols_available ? kUiSymbolCount : 0u);
    } else {
        AP_WARN("hud: using %dx%d built-in bitmap font fallback",
                atlas_width, atlas_height);
    }
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
    ui_font_ = UiFontAtlas{};
    using_ui_font_ = false;
    verts_.clear();
    clip_enabled_ = false;
    in_pass_ = false;
}

void Hud::begin(glm::vec2 viewport_px) {
    if (in_pass_) {
        AP_ERROR("hud: begin() called twice without an end(); ignoring");
        return;
    }
    verts_.clear();
    clip_enabled_ = false;
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

    push_triangle(a, b, c);
    push_triangle(a, c, d);
}

void Hud::rect(glm::vec2 min_px, glm::vec2 max_px, glm::vec4 color) {
    const GlyphUV s = using_ui_font_ ? ui_font_.solid : solid_uv();
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

void Hud::push_solid_quad(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d,
                          glm::vec4 color) {
    if (!in_pass_) return;
    const GlyphUV s = using_ui_font_ ? ui_font_.solid : solid_uv();
    push_triangle({a, {s.u0, s.v0}, color}, {b, {s.u1, s.v0}, color},
                  {c, {s.u1, s.v1}, color});
    push_triangle({a, {s.u0, s.v0}, color}, {c, {s.u1, s.v1}, color},
                  {d, {s.u0, s.v1}, color});
}

void Hud::line(glm::vec2 a_px, glm::vec2 b_px, float thickness,
               glm::vec4 color) {
    if (!in_pass_ || thickness <= 0.0f) return;
    const glm::vec2 delta = b_px - a_px;
    const float length = std::sqrt(glm::dot(delta, delta));
    if (length <= 1e-4f) return;
    const glm::vec2 normal{-delta.y / length, delta.x / length};
    const glm::vec2 offset = normal * (thickness * 0.5f);
    push_solid_quad(a_px - offset, b_px - offset, b_px + offset,
                    a_px + offset, color);
}

void Hud::quad(glm::vec2 a_px, glm::vec2 b_px, glm::vec2 c_px,
               glm::vec2 d_px, glm::vec4 color) {
    push_solid_quad(a_px, b_px, c_px, d_px, color);
}

void Hud::colored_triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c,
                           glm::vec4 ca, glm::vec4 cb, glm::vec4 cc) {
    if (!in_pass_) return;
    const GlyphUV s = using_ui_font_ ? ui_font_.solid : solid_uv();
    const glm::vec2 uv{s.u0, s.v0};
    push_triangle({a, uv, ca}, {b, uv, cb}, {c, uv, cc});
}

void Hud::set_clip_rect(glm::vec2 lo, glm::vec2 hi) {
    clip_enabled_ = true;
    clip_lo_ = lo;
    clip_hi_ = hi;
}

void Hud::clear_clip_rect() { clip_enabled_ = false; }

void Hud::push_triangle(Vertex a, Vertex b, Vertex c) {
    const glm::vec2 lo = glm::min(a.pos, glm::min(b.pos, c.pos));
    const glm::vec2 hi = glm::max(a.pos, glm::max(b.pos, c.pos));
    if (clip_enabled_ && (lo.x > clip_hi_.x || lo.y > clip_hi_.y ||
                         hi.x < clip_lo_.x || hi.y < clip_lo_.y)) return;
    if (!clip_enabled_ || (lo.x >= clip_lo_.x && lo.y >= clip_lo_.y &&
                          hi.x <= clip_hi_.x && hi.y <= clip_hi_.y)) {
        verts_.insert(verts_.end(), {a, b, c});
        return;
    }
    std::array<Vertex, 8> polygon{};
    polygon[0] = a; polygon[1] = b; polygon[2] = c;
    int count = 3;
    for (int edge = 0; edge < 4 && count > 0; ++edge) {
        std::array<Vertex, 8> output{};
        int n = 0;
        const int axis = edge / 2;
        const float bound = edge % 2 == 0 ? clip_lo_[axis] : clip_hi_[axis];
        for (int i = 0; i < count; ++i) {
            const Vertex& va = polygon[static_cast<std::size_t>(i)];
            const Vertex& vb = polygon[static_cast<std::size_t>((i + 1) % count)];
            const bool in_a = edge % 2 == 0 ? va.pos[axis] >= bound : va.pos[axis] <= bound;
            const bool in_b = edge % 2 == 0 ? vb.pos[axis] >= bound : vb.pos[axis] <= bound;
            if (in_a) output[static_cast<std::size_t>(n++)] = va;
            if (in_a != in_b) {
                const float t = (bound - va.pos[axis]) / (vb.pos[axis] - va.pos[axis]);
                output[static_cast<std::size_t>(n++)] = {
                    glm::mix(va.pos, vb.pos, t), glm::mix(va.uv, vb.uv, t),
                    glm::mix(va.color, vb.color, t)};
            }
        }
        polygon = output;
        count = n;
    }
    for (int i = 1; i + 1 < count; ++i)
        verts_.insert(verts_.end(), {polygon[0], polygon[static_cast<std::size_t>(i)],
                                   polygon[static_cast<std::size_t>(i + 1)]});
}

void Hud::triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 color) {
    colored_triangle(a, b, c, color, color, color);
}

void Hud::smooth_line(glm::vec2 a, glm::vec2 b, float thickness, glm::vec4 color) {
    const glm::vec2 delta = b - a;
    const float length = glm::length(delta);
    if (!in_pass_ || thickness <= 0.0f || length < 1e-4f) return;
    const glm::vec2 normal{-delta.y / length, delta.x / length};
    const float half = std::max(0.0f, thickness * 0.5f - 0.5f);
    const glm::vec2 inner = normal * half;
    push_solid_quad(a - inner, b - inner, b + inner, a + inner, color);
    const glm::vec4 clear{glm::vec3(color), 0.0f};
    for (const float side : {-1.0f, 1.0f}) {
        const glm::vec2 i = inner * side;
        const glm::vec2 o = normal * (half + 1.0f) * side;
        colored_triangle(a + i, b + i, b + o, color, color, clear);
        colored_triangle(a + i, b + o, a + o, color, clear, clear);
    }
}

void Hud::circle(glm::vec2 centre, float radius, glm::vec4 color) {
    if (!in_pass_ || radius <= 0.0f) return;
    const int segments = std::clamp(static_cast<int>(radius * 2.0f), 16, 48);
    const float inner = std::max(0.0f, radius - 0.5f);
    const glm::vec4 clear{glm::vec3(color), 0.0f};
    for (int i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) * 6.283185307f / static_cast<float>(segments);
        const float b = static_cast<float>(i + 1) * 6.283185307f / static_cast<float>(segments);
        const glm::vec2 da{std::cos(a), std::sin(a)}, db{std::cos(b), std::sin(b)};
        const glm::vec2 pa = centre + da * inner, pb = centre + db * inner;
        const glm::vec2 qa = centre + da * (radius + 0.5f);
        const glm::vec2 qb = centre + db * (radius + 0.5f);
        triangle(centre, pa, pb, color);
        colored_triangle(pa, qa, qb, color, clear, clear);
        colored_triangle(pa, qb, pb, color, clear, color);
    }
}

float Hud::text_line_height(float glyph_h_px) const {
    return glyph_h_px * kUiTextScale;
}

float Hud::text(const char* s, glm::vec2 top_left_px, float glyph_h_px,
                glm::vec4 color) {
    if (!s || !*s || glyph_h_px <= 0.0f) return 0.0f;
    glyph_h_px *= kUiTextScale;

    if (using_ui_font_) {
        const float scale = glyph_h_px / ui_font_.line_height;
        const float baseline = top_left_px.y + ui_font_.ascent * scale;
        float x = top_left_px.x;
        for (const char* p = s; *p; ++p) {
            const UiGlyph& glyph =
                ui_font_.glyphs[static_cast<std::size_t>(ui_glyph_index(*p))];
            if (glyph.width > 0.0f && glyph.height > 0.0f) {
                const glm::vec2 min_px{x + glyph.x_offset * scale,
                                       baseline + glyph.y_offset * scale};
                const glm::vec2 max_px{min_px.x + glyph.width * scale,
                                       min_px.y + glyph.height * scale};
                push_quad(min_px, max_px, glyph.uv.u0, glyph.uv.v0,
                          glyph.uv.u1, glyph.uv.v1, color);
            }
            x += glyph.advance * scale;
        }
        return x - top_left_px.x;
    }

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

float Hud::measure_text(const char* s, float glyph_h_px) const {
    if (!s || !*s || glyph_h_px <= 0.0f) return 0.0f;
    glyph_h_px *= kUiTextScale;
    if (!using_ui_font_) return text_width_px(s, glyph_h_px);

    const float scale = glyph_h_px / ui_font_.line_height;
    float width = 0.0f;
    for (const char* p = s; *p; ++p) {
        width += ui_font_.glyphs[
            static_cast<std::size_t>(ui_glyph_index(*p))].advance * scale;
    }
    return width;
}

float Hud::text_centered(const char* s, float centre_x_px, float top_y_px,
                         float glyph_h_px, glm::vec4 color) {
    const float w = measure_text(s, glyph_h_px);
    return text(s, {centre_x_px - w * 0.5f, top_y_px}, glyph_h_px, color);
}

float Hud::title_text(const char* s, glm::vec2 top_left_px, float glyph_h_px,
                      glm::vec4 color) {
    if (!using_ui_font_ || !ui_font_.title_available)
        return text(s, top_left_px, glyph_h_px, color);
    if (!s || !*s || glyph_h_px <= 0.0f) return 0.0f;
    glyph_h_px *= kUiTextScale;
    const float scale = glyph_h_px / ui_font_.title_line_height;
    const float baseline = top_left_px.y + ui_font_.title_ascent * scale;
    float x = top_left_px.x;
    for (const char* p = s; *p; ++p) {
        const UiGlyph& glyph = ui_font_.title_glyphs[
            static_cast<std::size_t>(ui_glyph_index(*p))];
        if (glyph.width > 0.0f && glyph.height > 0.0f) {
            const glm::vec2 min_px{x + glyph.x_offset * scale,
                                   baseline + glyph.y_offset * scale};
            const glm::vec2 max_px{min_px.x + glyph.width * scale,
                                   min_px.y + glyph.height * scale};
            push_quad(min_px, max_px, glyph.uv.u0, glyph.uv.v0,
                      glyph.uv.u1, glyph.uv.v1, color);
        }
        x += glyph.advance * scale;
    }
    return x - top_left_px.x;
}

float Hud::measure_title_text(const char* s, float glyph_h_px) const {
    if (!using_ui_font_ || !ui_font_.title_available)
        return measure_text(s, glyph_h_px);
    if (!s || !*s || glyph_h_px <= 0.0f) return 0.0f;
    glyph_h_px *= kUiTextScale;
    const float scale = glyph_h_px / ui_font_.title_line_height;
    float width = 0.0f;
    for (const char* p = s; *p; ++p) {
        width += ui_font_.title_glyphs[
            static_cast<std::size_t>(ui_glyph_index(*p))].advance * scale;
    }
    return width;
}

float Hud::title_text_centered(const char* s, float centre_x_px,
                               float top_y_px, float glyph_h_px,
                               glm::vec4 color) {
    const float w = measure_title_text(s, glyph_h_px);
    return title_text(s, {centre_x_px - w * 0.5f, top_y_px}, glyph_h_px,
                      color);
}

void Hud::symbol(UiSymbol symbol, glm::vec2 top_left_px, float size_px,
                 glm::vec4 color) {
    if (!in_pass_ || !using_ui_font_ || !ui_font_.symbols_available ||
        size_px <= 0.0f) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(symbol);
    if (index >= kUiSymbolCount) return;

    size_px *= kUiTextScale;
    const UiGlyph& glyph = ui_font_.symbols[index];
    const float source_size = std::max(glyph.width, glyph.height);
    if (!(source_size > 0.0f)) return;
    const float scale = size_px / source_size;
    const glm::vec2 drawn{glyph.width * scale, glyph.height * scale};
    const glm::vec2 min_px = top_left_px + (glm::vec2{size_px} - drawn) * 0.5f;
    push_quad(min_px, min_px + drawn, glyph.uv.u0, glyph.uv.v0,
              glyph.uv.u1, glyph.uv.v1, color);
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
