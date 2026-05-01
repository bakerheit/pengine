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

    // Procedural city textures. All tile seamlessly. Pair with per-object
    // uv_scale so that one tile maps to a recognisable real-world feature:
    //   asphalt: ~4 m  (gritty road surface)
    //   grass:   ~6 m  (terrain)
    //   facade:  ~3 m  (one window per tile)
    void load_asphalt(int size = 64);
    void load_grass  (int size = 128);
    void load_facade (int size = 64);

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
