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
