#pragma once

#include <cstddef>
#include <vector>

#include <glad/gl.h>

#include <glm/glm.hpp>

#include "gfx/shader.h"
#include "gfx/texture.h"

namespace apricot {

// Screen-space HUD batcher. ONE DRAW CALL for the whole overlay.
//
// Panels and text share a buffer and a shader because they share an atlas: a
// solid panel is a quad whose UVs point at a block of the glyph atlas that is
// filled with 1.0 (see gfx/glyph_atlas.h). There is no mode uniform and no
// flush, so a speed readout sitting on a rounded backing plate costs exactly as
// much as either one alone.
//
// Coordinates are screen PIXELS, origin top-left, +Y down. Draw order is
// submission order — queue the plate, then the label.
//
// Depth test, depth write, culling and blend state are saved by begin() and
// restored by end(), so the HUD cannot leak state into whatever draws next.
class Hud {
public:
    Hud() = default;
    ~Hud();

    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    bool init();
    void destroy();
    bool valid() const { return shader_.valid() && atlas_.valid() && vao_ != 0; }

    // Open a pass. A degenerate viewport (a minimised window) is accepted and
    // makes every subsequent call a no-op rather than dividing by zero.
    void begin(glm::vec2 viewport_px);

    void rect(glm::vec2 min_px, glm::vec2 max_px, glm::vec4 color);

    // A `thickness`-px border drawn just inside the given rect, as four quads.
    void outline(glm::vec2 min_px, glm::vec2 max_px, float thickness,
                 glm::vec4 color);

    // Top-left anchored. Returns the advance width actually consumed, so a
    // caller can chain runs of different colours on one line.
    float text(const char* s, glm::vec2 top_left_px, float glyph_h_px,
               glm::vec4 color);

    // Horizontally centred on `centre_x_px`.
    float text_centered(const char* s, float centre_x_px, float top_y_px,
                        float glyph_h_px, glm::vec4 color);

    // Upload everything queued and issue the single draw, then restore the GL
    // state begin() saved.
    void end();

    // Quads queued during the last completed pass, and draws it took (0 when
    // nothing was queued, 1 otherwise). Reported in the debug overlay: if this
    // is ever above 1 the batching has been broken.
    int last_quad_count() const { return last_quad_count_; }
    int last_draw_calls() const { return last_draw_calls_; }

private:
    struct Vertex {
        glm::vec2 pos;
        glm::vec2 uv;
        glm::vec4 color;
    };

    void push_quad(glm::vec2 min_px, glm::vec2 max_px, float u0, float v0,
                   float u1, float v1, glm::vec4 color);

    Shader shader_;
    Texture atlas_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    std::size_t vbo_capacity_bytes_ = 0;

    std::vector<Vertex> verts_;
    glm::vec2 viewport_{0.0f, 0.0f};
    bool in_pass_ = false;

    int last_quad_count_ = 0;
    int last_draw_calls_ = 0;

    // GL state captured by begin(), restored by end().
    GLboolean depth_was_ = GL_FALSE;
    GLboolean cull_was_ = GL_FALSE;
    GLboolean blend_was_ = GL_FALSE;
    GLboolean depth_mask_was_ = GL_TRUE;
};

}  // namespace apricot
