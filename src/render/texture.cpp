#include "render/texture.h"

#include <stb_image.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include "core/log.h"
#include "render/gl_state.h"

// BC1/BC3 compressed format enums (GL_EXT_texture_compression_s3tc).
// Defined manually so we don't need glad extension headers.
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0u
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3u
#endif

namespace pengine {

namespace {

// ---- Minimal DDS reader (BC1 / BC3 only, no cubemaps, no arrays) -----------

struct DDSPixelFormat {
    uint32_t size, flags, four_cc, rgb_bit_count;
    uint32_t r_mask, g_mask, b_mask, a_mask;
};
struct DDSHeader {
    uint32_t     size, flags, height, width, pitch_or_linear, depth, mip_count;
    uint32_t     reserved[11];
    DDSPixelFormat pf;
    uint32_t     caps[4];
    uint32_t     reserved2;
};

static uint32_t make_fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<unsigned char>(a))       |
           (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16)|
           (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
}

bool load_dds(const std::string& path, GLuint& tex_out, int& w_out, int& h_out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    uint32_t magic = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != make_fourcc('D','D','S',' ')) {
        PE_ERROR("Texture: not a DDS file: %s", path.c_str());
        return false;
    }

    DDSHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f) return false;

    uint32_t four_cc = hdr.pf.four_cc;
    GLenum internal_fmt = 0;
    uint32_t block_size = 8;
    if (four_cc == make_fourcc('D','X','T','1')) {
        internal_fmt = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        block_size   = 8;
    } else if (four_cc == make_fourcc('D','X','T','5')) {
        internal_fmt = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        block_size   = 16;
    } else {
        PE_ERROR("Texture: DDS FourCC 0x%08x not supported (need DXT1 or DXT5): %s",
                 four_cc, path.c_str());
        return false;
    }

    uint32_t mip_count = (hdr.mip_count > 0) ? hdr.mip_count : 1u;
    w_out = static_cast<int>(hdr.width);
    h_out = static_cast<int>(hdr.height);

    glGenTextures(1, &tex_out);
    gl_state::bind_texture_2d(0, tex_out);

    uint32_t w = hdr.width, h = hdr.height;
    for (uint32_t mip = 0; mip < mip_count; ++mip) {
        uint32_t size = ((w + 3) / 4) * ((h + 3) / 4) * block_size;
        std::vector<char> buf(size);
        f.read(buf.data(), static_cast<std::streamsize>(size));
        glCompressedTexImage2D(GL_TEXTURE_2D,
                               static_cast<GLint>(mip),
                               internal_fmt,
                               static_cast<GLsizei>(w),
                               static_cast<GLsizei>(h),
                               0,
                               static_cast<GLsizei>(size),
                               buf.data());
        w = (w > 1) ? w / 2 : 1;
        h = (h > 1) ? h / 2 : 1;
    }
    if (mip_count == 1) glGenerateMipmap(GL_TEXTURE_2D);
    return true;
}

bool ends_with_ci(const std::string& s, const char* suffix) {
    std::size_t sl = std::strlen(suffix);
    if (s.size() < sl) return false;
    std::string tail = s.substr(s.size() - sl);
    for (auto& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return tail == suffix;
}

} // namespace

bool Texture::load_file(const std::string& path) {
    if (ends_with_ci(path, ".dds")) {
        if (!load_dds(path, tex_, width_, height_)) return false;
        apply_params();
        PE_DEBUG("Texture (DDS) loaded: %s (%dx%d)", path.c_str(), width_, height_);
        return true;
    }

    stbi_set_flip_vertically_on_load(1);
    int channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &width_, &height_, &channels, 0);
    if (!data) {
        PE_ERROR("stb_image failed: %s  (%s)", path.c_str(), stbi_failure_reason());
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

    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        GLfloat max_aniso = 1.f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        max_aniso > 8.f ? 8.f : max_aniso);
    }
}

} // namespace pengine
