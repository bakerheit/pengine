#include "render/texture.h"

#include <stb_image.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
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

// ---------------------------------------------------------------------------
// Procedural textures (deterministic; no asset files needed).
// ---------------------------------------------------------------------------

namespace {

inline unsigned char clamp_u8(int v) {
    return static_cast<unsigned char>(std::max(0, std::min(255, v)));
}

void upload_rgb(GLuint& tex_out, int size, const std::vector<unsigned char>& pixels) {
    glGenTextures(1, &tex_out);
    gl_state::bind_texture_2d(0, tex_out);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
}

} // namespace

void Texture::load_asphalt(int size) {
    width_ = height_ = size;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size * size * 3));
    std::mt19937 rng(0xa5fa17u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int n = static_cast<int>(rng() % 28u) - 14;       // ±14 grain
            int r = 48 + n, g = 48 + n, b = 52 + n;
            // Occasional dark pebble.
            if ((rng() & 0x3fu) == 0) { r -= 22; g -= 22; b -= 22; }
            // Occasional light fleck (oil sheen / wear).
            else if ((rng() & 0x7fu) == 0) { r += 18; g += 18; b += 18; }
            std::size_t idx = static_cast<std::size_t>((y * size + x) * 3);
            pixels[idx + 0] = clamp_u8(r);
            pixels[idx + 1] = clamp_u8(g);
            pixels[idx + 2] = clamp_u8(b);
        }
    }
    upload_rgb(tex_, size, pixels);
    apply_params();
}

void Texture::load_grass(int size) {
    width_ = height_ = size;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size * size * 3));
    std::mt19937 rng(0xc0ffeeu);
    // Low-frequency dark patches via 8x8 sample-and-bilerp.
    constexpr int LF = 8;
    int patch[LF * LF];
    for (int i = 0; i < LF * LF; ++i)
        patch[i] = static_cast<int>(rng() % 36u) - 18;        // ±18 patch bias

    auto sample_patch = [&](int x, int y) {
        float fx = static_cast<float>(x) / size * (LF - 1);
        float fy = static_cast<float>(y) / size * (LF - 1);
        int   ix = static_cast<int>(fx);
        int   iy = static_cast<int>(fy);
        float tx = fx - ix, ty = fy - iy;
        int p00 = patch[iy       * LF + ix];
        int p10 = patch[iy       * LF + ix + 1];
        int p01 = patch[(iy + 1) * LF + ix];
        int p11 = patch[(iy + 1) * LF + ix + 1];
        float p0 = p00 + (p10 - p00) * tx;
        float p1 = p01 + (p11 - p01) * tx;
        return static_cast<int>(p0 + (p1 - p0) * ty);
    };

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int hf  = static_cast<int>(rng() % 24u) - 12;     // ±12 grain
            int lf  = sample_patch(x, y);
            int r   =  68 + hf + lf / 2;
            int g   = 105 + hf + lf;
            int b   =  44 + hf + lf / 2;
            std::size_t idx = static_cast<std::size_t>((y * size + x) * 3);
            pixels[idx + 0] = clamp_u8(r);
            pixels[idx + 1] = clamp_u8(g);
            pixels[idx + 2] = clamp_u8(b);
        }
    }
    upload_rgb(tex_, size, pixels);
    apply_params();
}

void Texture::load_facade(int size) {
    width_ = height_ = size;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size * size * 3));
    std::mt19937 rng(0xfacadeu);

    // One window per tile. Window occupies ~55% of tile, framed by a sill,
    // surrounded by wall.
    const int frame_in  = static_cast<int>(size * 0.20f);  // 20% margin
    const int frame_out = static_cast<int>(size * 0.24f);  //  4% frame thickness

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int  noise = static_cast<int>(rng() % 16u) - 8; // wall grain ±8
            int  r, g, b;
            bool in_window = (x >= frame_in  && x < size - frame_in  &&
                              y >= frame_in  && y < size - frame_in);
            bool in_frame  = (x >= frame_out && x < size - frame_out &&
                              y >= frame_out && y < size - frame_out);
            if (in_window) {
                // Dark glass with a faint vertical highlight to read as window.
                int sheen = (x % 8 < 2) ? 22 : 0;
                r = 28 + sheen;
                g = 36 + sheen;
                b = 52 + sheen;
            } else if (in_frame) {
                // Sill / frame: slightly darker than wall.
                r = 150 + noise / 2;
                g = 145 + noise / 2;
                b = 135 + noise / 2;
            } else {
                // Wall.
                r = 200 + noise;
                g = 192 + noise;
                b = 178 + noise;
            }
            std::size_t idx = static_cast<std::size_t>((y * size + x) * 3);
            pixels[idx + 0] = clamp_u8(r);
            pixels[idx + 1] = clamp_u8(g);
            pixels[idx + 2] = clamp_u8(b);
        }
    }
    upload_rgb(tex_, size, pixels);
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
