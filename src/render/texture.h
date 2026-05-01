#pragma once

#include <string>
#include <glad/gl.h>

namespace pengine {

class Texture {
public:
    Texture() = default;
    ~Texture() { destroy(); }

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Load from a PNG/JPG/etc. via stb_image, or a DDS (BC1/BC3) file.
    // Dispatches on file extension.
    bool load_file(const std::string& path);

    // 8x8 checkerboard procedural texture (no file needed).
    void load_checkerboard(int size = 64);

    void destroy();
    void bind(unsigned int unit = 0) const;

    GLuint id()     const { return tex_; }
    int    width()  const { return width_; }
    int    height() const { return height_; }

private:
    void apply_params() const;

    GLuint tex_    = 0;
    int    width_  = 0;
    int    height_ = 0;
};

} // namespace pengine
