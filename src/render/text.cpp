#include "render/text.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/log.h"

namespace pengine {

namespace {

struct Vertex {
    glm::vec2 pos_px;
    glm::vec2 uv;
};

bool read_file_binary(const std::string& path, std::vector<unsigned char>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }
    out.resize(static_cast<std::size_t>(n));
    const std::size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

} // namespace

bool Text::init(const std::string& assets_root) {
    // Atlas + glyph metrics — bake once from the bundled TTF.
    const std::string ttf_path = assets_root + "/fonts/Roboto-Regular.ttf";
    std::vector<unsigned char> ttf;
    if (!read_file_binary(ttf_path, ttf)) {
        PE_ERROR("Text: failed to read font %s", ttf_path.c_str());
        return false;
    }

    std::vector<unsigned char> bitmap(static_cast<std::size_t>(ATLAS_W) *
                                       static_cast<std::size_t>(ATLAS_H));
    cdata_.assign(NUM_CHARS, stbtt_bakedchar{});
    const int rc = stbtt_BakeFontBitmap(
        ttf.data(), 0,
        BAKE_PIXEL_HEIGHT,
        bitmap.data(), ATLAS_W, ATLAS_H,
        FIRST_CHAR, NUM_CHARS,
        cdata_.data());
    if (rc <= 0) {
        // rc < 0 → atlas was too small; rc == 0 → no glyphs baked.
        PE_ERROR("Text: stbtt_BakeFontBitmap failed (rc=%d)", rc);
        return false;
    }

    glGenTextures(1, &atlas_tex_);
    glBindTexture(GL_TEXTURE_2D, atlas_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, ATLAS_W, ATLAS_H, 0,
                 GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, pos_px)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if (!shader_.load(assets_root + "/shaders/text.vert",
                      assets_root + "/shaders/text.frag")) {
        PE_ERROR("Text: failed to load text shader");
        return false;
    }

    return true;
}

void Text::shutdown() {
    shader_.destroy();
    if (vbo_)       { glDeleteBuffers(1, &vbo_);          vbo_       = 0; }
    if (vao_)       { glDeleteVertexArrays(1, &vao_);     vao_       = 0; }
    if (atlas_tex_) { glDeleteTextures(1, &atlas_tex_);   atlas_tex_ = 0; }
    cdata_.clear();
}

float Text::measure_width(const char* s, float glyph_h_px) const {
    if (!s || cdata_.empty()) return 0.f;
    const float scale = glyph_h_px / BAKE_PIXEL_HEIGHT;
    float x = 0.f, y = 0.f;
    for (; *s; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(cdata_.data(), ATLAS_W, ATLAS_H,
                           c - FIRST_CHAR, &x, &y, &q, /*opengl_fillrule*/ 1);
    }
    return x * scale;
}

void Text::draw_lines(const DrawState& s) {
    if (!s.lines || s.count <= 0 || cdata_.empty()) return;
    if (s.viewport_size_px.x <= 0.f || s.viewport_size_px.y <= 0.f) return;

    const float scale = s.glyph_h_px / BAKE_PIXEL_HEIGHT;
    const float row_h = s.glyph_h_px * 1.4f;

    // First pass: count vertices so we can size the VBO once.
    std::size_t total_chars = 0;
    for (int i = 0; i < s.count; ++i) {
        if (!s.lines[i]) continue;
        for (const char* p = s.lines[i]; *p; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c >= FIRST_CHAR && c < FIRST_CHAR + NUM_CHARS) ++total_chars;
        }
    }
    if (total_chars == 0) return;

    std::vector<Vertex> verts;
    verts.reserve(total_chars * 6);

    // Second pass: emit quads. Origin is top-left; baseline is at top + ascent
    // ≈ origin.y + BAKE_PIXEL_HEIGHT * 0.8f (scaled). stbtt_GetBakedQuad
    // returns coords in the baked space; we scale on the way out.
    for (int i = 0; i < s.count; ++i) {
        if (!s.lines[i]) continue;
        const float row_top   = s.origin_top_left_px.y + static_cast<float>(i) * row_h;
        const float baseline  = row_top + BAKE_PIXEL_HEIGHT * 0.8f * scale;

        float x = s.origin_top_left_px.x / scale;  // pen in baked units
        float y = baseline / scale;
        for (const char* p = s.lines[i]; *p; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c < FIRST_CHAR || c >= FIRST_CHAR + NUM_CHARS) continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata_.data(), ATLAS_W, ATLAS_H,
                               c - FIRST_CHAR, &x, &y, &q, /*opengl_fillrule*/ 1);

            const float x0 = q.x0 * scale, y0 = q.y0 * scale;
            const float x1 = q.x1 * scale, y1 = q.y1 * scale;

            // Two triangles per glyph quad (CCW in pixel space; vert shader
            // flips Y to GL space).
            verts.push_back({{x0, y0}, {q.s0, q.t0}});
            verts.push_back({{x1, y0}, {q.s1, q.t0}});
            verts.push_back({{x1, y1}, {q.s1, q.t1}});

            verts.push_back({{x0, y0}, {q.s0, q.t0}});
            verts.push_back({{x1, y1}, {q.s1, q.t1}});
            verts.push_back({{x0, y1}, {q.s0, q.t1}});
        }
    }

    if (verts.empty()) return;

    // Optionally prepend a backplate quad for the whole text block. Drawn
    // first so the glyphs render on top. Width = max line width; height spans
    // from the first row top to the last row top + glyph height.
    const bool want_bg = s.bg_color.a > 0.f;
    std::size_t bg_vert_count = 0;
    if (want_bg) {
        float max_w = 0.f;
        for (int i = 0; i < s.count; ++i) {
            if (!s.lines[i]) continue;
            const float w = measure_width(s.lines[i], s.glyph_h_px);
            if (w > max_w) max_w = w;
        }
        const float pad = s.bg_padding_px;
        const float x0  = s.origin_top_left_px.x - pad;
        const float y0  = s.origin_top_left_px.y - pad;
        const float x1  = s.origin_top_left_px.x + max_w + pad;
        const float y1  = s.origin_top_left_px.y +
                          static_cast<float>(s.count - 1) * row_h +
                          s.glyph_h_px + pad;

        // Prepend so the backplate draws first.
        std::vector<Vertex> with_bg;
        with_bg.reserve(verts.size() + 6);
        // UVs are unused in fill mode; anything is fine.
        with_bg.push_back({{x0, y0}, {0.f, 0.f}});
        with_bg.push_back({{x1, y0}, {1.f, 0.f}});
        with_bg.push_back({{x1, y1}, {1.f, 1.f}});
        with_bg.push_back({{x0, y0}, {0.f, 0.f}});
        with_bg.push_back({{x1, y1}, {1.f, 1.f}});
        with_bg.push_back({{x0, y1}, {0.f, 1.f}});
        with_bg.insert(with_bg.end(), verts.begin(), verts.end());
        verts.swap(with_bg);
        bg_vert_count = 6;
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const std::size_t bytes = verts.size() * sizeof(Vertex);
    if (bytes > vbo_capacity_bytes_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes),
                     verts.data(), GL_DYNAMIC_DRAW);
        vbo_capacity_bytes_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(bytes), verts.data());
    }

    // Render state for screen-space alpha text. Match the pattern other 2D
    // draws use (minimap, menu, crosshair): disable cull/depth, enable blend,
    // restore on exit. Without disabling cull, the CW-wound text quads get
    // culled as back faces (engine is configured CCW front, GL_BACK cull).
    GLboolean was_blend     = glIsEnabled(GL_BLEND);
    GLboolean was_depthtest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean was_cull      = glIsEnabled(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    shader_.use();
    shader_.set("u_viewport_px", s.viewport_size_px);
    shader_.set("u_atlas",       0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas_tex_);

    glBindVertexArray(vao_);

    if (bg_vert_count > 0) {
        shader_.set("u_mode",  1);
        shader_.set("u_color", s.bg_color);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(bg_vert_count));
    }

    shader_.set("u_mode",  0);
    shader_.set("u_color", glm::vec4{s.color, 1.f});
    glDrawArrays(GL_TRIANGLES,
                 static_cast<GLint>(bg_vert_count),
                 static_cast<GLsizei>(verts.size() - bg_vert_count));

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!was_blend)     glDisable(GL_BLEND);
    if (was_depthtest)  glEnable(GL_DEPTH_TEST);
    if (was_cull)       glEnable(GL_CULL_FACE);
}

} // namespace pengine
