#include "gfx/texture.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/log.h"
#include "core/rng.h"
#include "gfx/gl_state.h"

namespace apricot {
namespace {

// The texture unit generators bind to while uploading. Deliberately the LAST
// unit rather than 0: unit 0 holds the material diffuse during a frame, and
// scribbling over it mid-frame to build a texture would silently swap what the
// next draw samples.
constexpr GLuint kEditUnit = 15;

bool power_of_two(int v) { return v > 0 && (v & (v - 1)) == 0; }

uint8_t to_byte(float v) {
    const float c = std::clamp(v, 0.0f, 1.0f);
    return static_cast<uint8_t>(c * 255.0f + 0.5f);
}

void put_rgb(std::vector<uint8_t>& out, std::size_t texel, glm::vec3 c) {
    out[texel * 3 + 0] = to_byte(c.r);
    out[texel * 3 + 1] = to_byte(c.g);
    out[texel * 3 + 2] = to_byte(c.b);
}

// Value in [0,1) at integer lattice point (x, z), wrapping at `period` so the
// noise field repeats exactly and the texture has no seam. The wrap is done on
// the LATTICE COORDINATE, not on the sampled value: fading the edges toward a
// constant instead would leave a visible flat band around the border.
float lattice(uint64_t seed, int x, int z, int period) {
    const int wx = ((x % period) + period) % period;
    const int wz = ((z % period) + period) % period;
    Rng r = rng_at(seed, wx, wz);
    return r.next_float();
}

float smootherstep(float t) { return t * t * (3.0f - 2.0f * t); }

// One octave of tiling value noise sampled at (u, v) in [0,1).
float value_noise(uint64_t seed, float u, float v, int period) {
    const float fx = u * static_cast<float>(period);
    const float fz = v * static_cast<float>(period);
    const int x0 = static_cast<int>(std::floor(fx));
    const int z0 = static_cast<int>(std::floor(fz));
    const float tx = smootherstep(fx - static_cast<float>(x0));
    const float tz = smootherstep(fz - static_cast<float>(z0));

    const float a = lattice(seed, x0, z0, period);
    const float b = lattice(seed, x0 + 1, z0, period);
    const float c = lattice(seed, x0, z0 + 1, period);
    const float d = lattice(seed, x0 + 1, z0 + 1, period);

    return glm::mix(glm::mix(a, b, tx), glm::mix(c, d, tx), tz);
}

}  // namespace

Texture::~Texture() { destroy(); }

Texture::Texture(Texture&& other) noexcept
    : tex_(other.tex_), width_(other.width_), height_(other.height_) {
    other.tex_ = 0;
    other.width_ = 0;
    other.height_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        tex_ = other.tex_;
        width_ = other.width_;
        height_ = other.height_;
        other.tex_ = 0;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

void Texture::destroy() {
    if (!tex_) return;
    glDeleteTextures(1, &tex_);
    // Paired with the delete, always. See the warning in gl_state.h — this is
    // the exact hook whose absence gives black textures on other people's
    // machines and never on yours.
    gl_state::on_texture_deleted(tex_);
    tex_ = 0;
    width_ = 0;
    height_ = 0;
}

void Texture::bind(GLuint unit) const { gl_state::bind_texture(unit, tex_); }

bool Texture::upload_rgb(int width, int height, const std::vector<uint8_t>& rgb) {
    const std::size_t expected =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u;
    if (width <= 0 || height <= 0 || rgb.size() != expected) {
        AP_ERROR("texture: refusing a %dx%d RGB upload with %zu bytes (need %zu)",
                 width, height, rgb.size(), expected);
        return false;
    }

    if (!tex_) glGenTextures(1, &tex_);
    if (!tex_) {
        AP_ERROR("texture: glGenTextures produced no object");
        return false;
    }
    gl_state::bind_texture(kEditUnit, tex_);

    // Rows are 3 bytes per texel and GL's default unpack alignment is 4, so any
    // width that is not a multiple of 4 gets its rows read at the wrong stride
    // and the image shears diagonally. Classic, and it only shows up on the odd
    // size somebody tries months later.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, rgb.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    width_ = width;
    height_ = height;
    return true;
}

bool Texture::make_checker(int size, int squares, glm::vec3 a, glm::vec3 b) {
    if (!power_of_two(size) || squares <= 0 || size % squares != 0) {
        AP_ERROR("texture: checker needs a power-of-two size divisible by "
                 "squares (got size=%d squares=%d)", size, squares);
        return false;
    }
    const int cell = size / squares;

    std::vector<uint8_t> rgb(static_cast<std::size_t>(size) *
                             static_cast<std::size_t>(size) * 3u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool odd = ((x / cell) + (y / cell)) % 2 != 0;
            put_rgb(rgb, static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(size) +
                         static_cast<std::size_t>(x),
                    odd ? b : a);
        }
    }
    return upload_rgb(size, size, rgb);
}

bool Texture::make_noise(int size, int base_freq, int octaves, glm::vec3 low,
                         glm::vec3 high, uint64_t seed) {
    if (!power_of_two(size) || base_freq <= 0 || octaves <= 0) {
        AP_ERROR("texture: noise needs a power-of-two size and positive "
                 "freq/octaves (got size=%d freq=%d octaves=%d)",
                 size, base_freq, octaves);
        return false;
    }

    std::vector<uint8_t> rgb(static_cast<std::size_t>(size) *
                             static_cast<std::size_t>(size) * 3u);
    const float inv = 1.0f / static_cast<float>(size);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) * inv;
            const float v = (static_cast<float>(y) + 0.5f) * inv;

            float sum = 0.0f;
            float amp = 0.5f;
            float norm = 0.0f;
            int period = base_freq;
            for (int o = 0; o < octaves; ++o) {
                // Each octave uses its own seed channel, or every octave would
                // be the same field scaled and they would sum into visible
                // self-similar blotches instead of detail.
                sum += amp * value_noise(seed + static_cast<uint64_t>(o) * 0x9E37u,
                                         u, v, period);
                norm += amp;
                amp *= 0.5f;
                // Period doubles per octave and must stay a divisor of the
                // texture, or the octave stops tiling and reintroduces a seam.
                if (period * 2 > size) break;
                period *= 2;
            }
            const float n = norm > 0.0f ? sum / norm : 0.0f;

            put_rgb(rgb, static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(size) +
                         static_cast<std::size_t>(x),
                    glm::mix(low, high, n));
        }
    }
    return upload_rgb(size, size, rgb);
}

bool Texture::make_gradient(int size, glm::vec3 bottom, glm::vec3 top) {
    if (!power_of_two(size)) {
        AP_ERROR("texture: gradient needs a power-of-two size (got %d)", size);
        return false;
    }

    std::vector<uint8_t> rgb(static_cast<std::size_t>(size) *
                             static_cast<std::size_t>(size) * 3u);
    for (int y = 0; y < size; ++y) {
        // Row 0 is the TOP of a GL texture only if the caller uploads it that
        // way; we treat v=0 as bottom to match the UV convention used by the
        // mesh generators, so `bottom` really is at the bottom.
        const float t = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
        const glm::vec3 c = glm::mix(bottom, top, t);
        for (int x = 0; x < size; ++x) {
            put_rgb(rgb, static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(size) +
                         static_cast<std::size_t>(x),
                    c);
        }
    }
    return upload_rgb(size, size, rgb);
}

bool Texture::make_white() {
    const std::vector<uint8_t> rgb{255u, 255u, 255u};
    return upload_rgb(1, 1, rgb);
}

bool Texture::upload_r8(int width, int height, const std::vector<uint8_t>& pixels,
                        bool smooth) {
    const std::size_t expected =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (width <= 0 || height <= 0 || pixels.size() != expected) {
        AP_ERROR("texture: refusing a %dx%d R8 upload with %zu bytes (need %zu)",
                 width, height, pixels.size(), expected);
        return false;
    }

    if (!tex_) glGenTextures(1, &tex_);
    if (!tex_) {
        AP_ERROR("texture: glGenTextures produced no object");
        return false;
    }
    gl_state::bind_texture(kEditUnit, tex_);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED,
                 GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    // CLAMP_TO_EDGE, not REPEAT. A glyph atlas sampled at a cell border with
    // REPEAT pulls in the texel from the opposite edge of the atlas, and you
    // get a hairline of some unrelated letter down the side of every character.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // No mipmaps: the HUD is drawn at roughly 1:1 and a mip chain on an atlas
    // bleeds neighbouring cells together at the lower levels.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    width_ = width;
    height_ = height;
    return true;
}

}  // namespace apricot
