#pragma once

#include <cstddef>
#include <vector>

#include <glad/gl.h>

#include <glm/glm.hpp>

#include "gfx/font_atlas.h"
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
// Coordinates are canvas units, origin top-left, +Y down. begin() supplies the
// canvas extent; the shader maps it onto the active GL viewport. Game UI uses
// UiCanvas for resolution-independent sizing. Draw order is submission order.
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

    // Clip submitted geometry on the CPU, preserving UV/color interpolation
    // and the single batch. Used only while drawing the map body.
    void set_clip_rect(glm::vec2 lo, glm::vec2 hi);
    void clear_clip_rect();

    void rect(glm::vec2 min_px, glm::vec2 max_px, glm::vec4 color);

    // A `thickness`-px border drawn just inside the given rect, as four quads.
    void outline(glm::vec2 min_px, glm::vec2 max_px, float thickness,
                 glm::vec4 color);

    // Screen-space segment with square caps. Used by the city map for roads,
    // boundaries and the player heading; still batches into this HUD's one
    // draw call.
    void line(glm::vec2 a_px, glm::vec2 b_px, float thickness,
              glm::vec4 color);

    // Feathered vector edges for the zoomable city map, still in this batch.
    void smooth_line(glm::vec2 a, glm::vec2 b, float thickness, glm::vec4 color);
    void circle(glm::vec2 centre, float radius, glm::vec4 color);
    void triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 color);

    // A colour per corner, interpolated linearly across the triangle. Same
    // solid atlas block, same CPU clip and same single draw as everything else:
    // a gradient costs vertices, never a draw call.
    void gradient_triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec4 ca,
                           glm::vec4 cb, glm::vec4 cc);

    // Axis-aligned rect with a colour at each corner, blended BILINEARLY. The
    // GPU can only interpolate a triangle linearly, and a four-corner blend is
    // not linear — a colour picker's saturation/value plane drawn as two
    // triangles is visibly wrong through its middle. So the rect is cut into
    // `cols` x `rows` cells, each exact at its own corners, and the error
    // shrinks with the square of the cell count. Both counts are clamped to
    // 1..32, which caps one call at 6144 vertices. A zero-area rect draws
    // nothing. See src/gfx/README.md for the measured error per grid size.
    void gradient_rect(glm::vec2 min_px, glm::vec2 max_px, glm::vec4 top_left,
                       glm::vec4 top_right, glm::vec4 bottom_right,
                       glm::vec4 bottom_left, int cols = 1, int rows = 1);

    // Use the real font advances and presentation scale for label placement.
    float measure_text(const char* s, float glyph_h_px) const;
    float text_line_height(float glyph_h_px) const;

    // Convex screen-space quad, submitted in clockwise order. Map building
    // footprints use this so Pinatty's six-degree street grid stays visible
    // instead of collapsing into axis-aligned boxes.
    void quad(glm::vec2 a_px, glm::vec2 b_px, glm::vec2 c_px, glm::vec2 d_px,
              glm::vec4 color);

    // Top-left anchored. Returns the advance width actually consumed, so a
    // caller can chain runs of different colours on one line.
    float text(const char* s, glm::vec2 top_left_px, float glyph_h_px,
               glm::vec4 color);

    // Horizontally centred on `centre_x_px`.
    float text_centered(const char* s, float centre_x_px, float top_y_px,
                        float glyph_h_px, glm::vec4 color);

    // Uses the Righteous face from the Probable Cause title art. It shares the
    // regular HUD atlas and draw call.
    float title_text(const char* s, glm::vec2 top_left_px, float glyph_h_px,
                     glm::vec4 color);
    float measure_title_text(const char* s, float glyph_h_px) const;
    float title_text_centered(const char* s, float centre_x_px, float top_y_px,
                              float glyph_h_px, glm::vec4 color);

    // Material Symbols icon, top-left anchored inside a square. The HUD
    // symbols are packed beside Bebas Neue so panels, copy and icons remain a
    // single draw call.
    void symbol(UiSymbol symbol, glm::vec2 top_left_px, float size_px,
                glm::vec4 color);

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
    void push_solid_quad(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d,
                         glm::vec4 color);
    void colored_triangle(glm::vec2 a, glm::vec2 b, glm::vec2 c,
                          glm::vec4 ca, glm::vec4 cb, glm::vec4 cc);
    void push_triangle(Vertex a, Vertex b, Vertex c);

    Shader shader_;
    Texture atlas_;
    UiFontAtlas ui_font_;
    bool using_ui_font_ = false;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    std::size_t vbo_capacity_bytes_ = 0;

    std::vector<Vertex> verts_;
    glm::vec2 viewport_{0.0f, 0.0f};
    bool in_pass_ = false;
    bool clip_enabled_ = false;
    glm::vec2 clip_lo_{0.0f}, clip_hi_{0.0f};

    int last_quad_count_ = 0;
    int last_draw_calls_ = 0;

    // GL state captured by begin(), restored by end().
    GLboolean depth_was_ = GL_FALSE;
    GLboolean cull_was_ = GL_FALSE;
    GLboolean blend_was_ = GL_FALSE;
    GLboolean depth_mask_was_ = GL_TRUE;
};

}  // namespace apricot
