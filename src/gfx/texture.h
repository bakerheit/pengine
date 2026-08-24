#pragma once

#include <cstdint>
#include <vector>

#include <glad/gl.h>

#include <glm/glm.hpp>

namespace apricot {

// A GL texture object.
//
// EVERY texture in this engine is generated in code. There is no image loader
// and there are no image files on disk, which is not a limitation being worked
// around — it is the point. A procedural texture cannot go missing from a
// package, cannot be the wrong colour space because someone re-exported it, and
// costs nothing to version. The generators below cover what a driving game
// actually needs: ground, scenery, and a debug checker.
//
// Every generator tiles seamlessly. A texture with a visible seam laid over
// terrain reads as a grid, and the grid is all anyone will see afterwards.
class Texture {
public:
    Texture() = default;
    ~Texture();

    // Move-only, same reason as Mesh: a copied GL handle gets deleted twice.
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    // --- procedural generators ----------------------------------------------
    // `size` is the side length in texels and must be a power of two so the
    // seamless wrapping maths and the mip chain both work out exactly.

    // Two-colour checkerboard, `squares` per edge. The debug texture: UV
    // problems, mirrored geometry and wrong tiling are all instantly visible.
    bool make_checker(int size, int squares, glm::vec3 a, glm::vec3 b);

    // Tiling value-noise field between two colours. `octaves` layers of
    // detail; `base_freq` is cells per edge at the first octave.
    bool make_noise(int size, int base_freq, int octaves, glm::vec3 low,
                    glm::vec3 high, uint64_t seed);

    // Vertical gradient from `bottom` to `top`. Tiles horizontally; it does NOT
    // tile vertically, by definition, so use it on things with a clear up.
    bool make_gradient(int size, glm::vec3 bottom, glm::vec3 top);

    // 1x1 opaque white. The default bound to every sampler, so a material that
    // forgot its texture draws its tint flat instead of sampling black — a
    // black object and an unlit object look identical, and one of them is a bug.
    bool make_white();

    // --- raw upload ---------------------------------------------------------
    // Single-channel coverage data (the glyph atlas). `pixels` must hold
    // exactly width*height bytes.
    bool upload_r8(int width, int height, const std::vector<uint8_t>& pixels,
                   bool smooth);

    void destroy();

    // Binds through gl_state, never glBindTexture directly.
    void bind(GLuint unit) const;

    GLuint id() const { return tex_; }
    bool valid() const { return tex_ != 0; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    // Create-or-reuse the GL object and push `rgb` (3 bytes per texel).
    bool upload_rgb(int width, int height, const std::vector<uint8_t>& rgb);

    GLuint tex_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace apricot
