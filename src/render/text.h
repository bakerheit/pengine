#pragma once

#include <string>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <stb_truetype.h>

#include "render/shader.h"

namespace pengine {

// Screen-space TTF text renderer. Bakes a single ASCII glyph atlas at a
// reference pixel height at init; renders strings as textured quads sampled
// from that atlas, scaled with vertex math so callers can choose arbitrary
// `glyph_h_px` per call.
//
// Bake size is high enough that target sizes (18–32 px in the debug HUD) get
// clean GL_LINEAR downsampling. SDF would do better at very small sizes; not
// needed yet.
class Text {
public:
    bool init(const std::string& assets_root);
    void shutdown();

    struct DrawState {
        const char* const* lines = nullptr;   // array of C strings, one per line
        int                count = 0;
        glm::vec2          origin_top_left_px {0.f};
        float              glyph_h_px         = 22.f;
        glm::vec3          color              {1.f, 1.f, 1.f};
        glm::vec2          viewport_size_px   {0.f};
        // Optional backplate behind the text block. Drawn when bg_color.a > 0.
        glm::vec4          bg_color           {0.f, 0.f, 0.f, 0.f};
        float              bg_padding_px      = 10.f;
    };
    void draw_lines(const DrawState& s);

    // Measure the rendered width of a string at a given glyph height — useful
    // for centring (footer text). Returns pixels.
    float measure_width(const char* s, float glyph_h_px) const;

private:
    static constexpr int   ATLAS_W           = 512;
    static constexpr int   ATLAS_H           = 512;
    static constexpr int   FIRST_CHAR        = 32;   // space
    static constexpr int   NUM_CHARS         = 96;   // through '~' (126)
    static constexpr float BAKE_PIXEL_HEIGHT = 48.f;

    Shader                            shader_;
    GLuint                            vao_       = 0;
    GLuint                            vbo_       = 0;
    GLuint                            atlas_tex_ = 0;
    std::size_t                       vbo_capacity_bytes_ = 0;
    std::vector<stbtt_bakedchar>      cdata_;
};

} // namespace pengine
