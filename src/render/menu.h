#pragma once

#include <string>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "render/shader.h"

namespace pengine {

// Lightweight full-screen menu overlay. Renders a centred title + a vertical
// list of selectable items as stroke-letter labels. No font library: glyphs
// are 2D line strokes (uppercase A-Z + digits + space, see menu.cpp).
//
// The widget is purely a renderer — it does not own selection state or input.
// The caller (Application) decides which list/title to draw and which index
// is highlighted.
class Menu {
public:
    bool init(const std::string& assets_root);
    void shutdown();

    struct DrawState {
        const char* title          = "";
        const char* const* items   = nullptr;   // array of C strings
        int         item_count     = 0;
        int         selected_index = 0;
        // Optional sub-label drawn below the list (e.g. "Coming soon").
        const char* footer         = "";
        glm::vec2   viewport_size_px {0.f};
    };

    void draw(const DrawState& s);

    // Lightweight key/value readout helper (PBD-026). Draws a vertical stack
    // of stroke-font lines anchored at `origin_top_left_px` in the same
    // coordinate space as `draw()` (pixels, +Y down). Does NOT clear the
    // framebuffer or draw a background — the caller has already rendered the
    // scene and wants the text to overlay it.
    //
    // Used by the Map Builder inspector to render the picked-instance
    // readout in a corner of the screen.
    struct TextLines {
        const char* const* lines = nullptr;   // array of C strings
        int                count = 0;
        glm::vec2          origin_top_left_px {0.f};
        float              glyph_h_px         = 18.f;
        float              thickness_px       = 2.f;
        glm::vec3          color              {0.95f, 0.92f, 0.55f};
        glm::vec2          viewport_size_px   {0.f};
    };
    void draw_text_lines(const TextLines& t);

    // PBD-030: filled-rect submission for tool overlays (asset palette
    // backplate, tint swatches, selection-highlight bars). Each Rect emits
    // one quad in the same pixel coord space as draw_text_lines. Caller
    // batches its rects and issues one call so we keep GL state churn flat.
    struct Rect {
        glm::vec2 min_px {0.f};
        glm::vec2 max_px {0.f};
        glm::vec3 color  {1.f, 1.f, 1.f};
    };
    void draw_rects(const Rect* rects, int count, glm::vec2 viewport_size_px);

    struct Vertex {
        glm::vec2 pos;     // pixel coords
        glm::vec3 color;
    };

private:
    Shader      shader_;
    GLuint      vao_          = 0;
    GLuint      vbo_          = 0;
    std::size_t vbo_capacity_ = 0;
};

} // namespace pengine
