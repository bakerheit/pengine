#include "render/texture.h"

#include <stb_image.h>
#include <vector>

#include "core/log.h"
#include "render/gl_state.h"

namespace pengine {

bool Texture::load_file(const std::string& path) {
    stbi_set_flip_vertically_on_load(1);
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width_, &height_, &channels, 0);
    if (!data) {
        PE_ERROR("stb_image failed to load: %s  (%s)", path.c_str(), stbi_failure_reason());
        return false;
    }

    GLenum fmt = GL_RGB;
    if (channels == 4) fmt = GL_RGBA;
    else if (channels == 1) fmt = GL_RED;

    glGenTextures(1, &tex_);
    gl_state::bind_texture_2d(0, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(fmt),
                 width_, height_, 0, fmt, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    stbi_image_free(data);
    apply_params();

    PE_DEBUG("Texture loaded: %s (%dx%d ch=%d)", path.c_str(), width_, height_, channels);
    return true;
}

void Texture::load_checkerboard(int size) {
    width_ = height_ = size;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size * size * 3));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool white = ((x / 8 + y / 8) % 2) == 0;
            std::size_t idx = static_cast<std::size_t>((y * size + x) * 3);
            pixels[idx + 0] = white ? 220 : 60;
            pixels[idx + 1] = white ? 220 : 60;
            pixels[idx + 2] = white ? 220 : 60;
        }
    }
    glGenTextures(1, &tex_);
    gl_state::bind_texture_2d(0, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    apply_params();
}

void Texture::destroy() {
    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
}

void Texture::bind(unsigned int unit) const {
    gl_state::bind_texture_2d(unit, tex_);
}

void Texture::apply_params() const {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Anisotropic filtering (GL_EXT_texture_filter_anisotropic, core in GL 4.6 but
    // available via extension on 3.3; GLAD exposes the query).
    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        GLfloat max_aniso = 1.f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        max_aniso > 8.f ? 8.f : max_aniso);
    }
}

} // namespace pengine
