#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glad/gl.h>

#include <glm/glm.hpp>

namespace apricot {

// Decode an image file to 8-bit RGBA, rows BOTTOM-UP: the vertical flip
// load_file() applies, because load_file() is exactly this plus upload_rgba().
// A CPU recolour that composites these texels and pushes them back through
// upload_rgba() therefore lands texel for texel on the paint the stock material
// loaded. Decode the same PNG any other way and every paint rect comes back
// mirrored across the atlas, onto the wrong panels.
//
// `rgba` holds width*height*4 bytes on success and is empty on failure.
// `source_channels`, when given, reports what the file itself stored.
bool decode_rgba_file(const std::string& path, int& width, int& height,
                      std::vector<uint8_t>& rgba, int* source_channels = nullptr);

// A GL texture object. World surfaces and debug art stay procedural; authored
// models may load their matching PNG paint through load_file().
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

    // Authored model paint. The image is flipped into the UV convention used
    // by the original vehicle sources and uploaded as RGBA with mipmaps.
    bool load_file(const std::string& path);

    // Push 8-bit RGBA (4 bytes per texel, rows bottom-up, as decode_rgba_file
    // gives) with the sampling load_file() gives authored paint: REPEAT,
    // trilinear, full mip chain. Keeps this object's GL id and never deletes
    // it. When the storage is already RGBA8 at this size the texels are
    // replaced in place (glTexSubImage2D) instead of reallocated; either way
    // the whole mip chain is regenerated, so a minified car never shows the
    // previous paint at a distance.
    bool upload_rgba(int width, int height, const std::vector<uint8_t>& rgba);

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

    // Fine aggregate with sparse dark pebbles and pale wear flecks. Unlike
    // make_noise(), this stays gritty when an 8 m road tile is viewed up close.
    bool make_asphalt(int size, uint64_t seed);

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
    // The level-0 storage was allocated as RGBA8 by upload_rgba(). Size alone
    // is not enough to write in place: an R8 glyph atlas or an RGB8 generator
    // at the same size has storage of the wrong format, and a sub-image write
    // into it converts silently instead of failing.
    bool rgba8_ = false;
};

}  // namespace apricot
