#include "render/speedometer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include <glm/gtc/constants.hpp>

#include "core/log.h"

namespace pengine {

namespace {

constexpr float GAUGE_RADIUS_PX = 96.f;
constexpr float GAUGE_MARGIN_PX = 28.f;
constexpr float MAX_SPEED_KMH   = 220.f;

constexpr glm::vec3 BG_COLOR      {0.055f, 0.065f, 0.070f};
constexpr glm::vec3 RIM_COLOR     {0.52f,  0.56f,  0.58f};
constexpr glm::vec3 TICK_COLOR    {0.82f,  0.86f,  0.84f};
constexpr glm::vec3 NEEDLE_COLOR  {1.00f,  0.22f,  0.16f};
constexpr glm::vec3 TEXT_COLOR    {0.94f,  0.98f,  0.93f};
constexpr glm::vec3 LABEL_COLOR   {0.66f,  0.78f,  0.75f};

struct Stroke { glm::vec2 a; glm::vec2 b; };

constexpr std::array<Stroke, 7> SEGMENTS{{
    {{0.f, 1.f},   {1.f, 1.f}},   // top
    {{1.f, 1.f},   {1.f, 0.5f}},   // upper right
    {{1.f, 0.5f},  {1.f, 0.f}},   // lower right
    {{0.f, 0.f},   {1.f, 0.f}},   // bottom
    {{0.f, 0.5f},  {0.f, 0.f}},   // lower left
    {{0.f, 1.f},   {0.f, 0.5f}},   // upper left
    {{0.f, 0.5f},  {1.f, 0.5f}},   // middle
}};

constexpr std::array<unsigned char, 10> DIGIT_MASK{{
    0x3f, // 0
    0x06, // 1
    0x5b, // 2
    0x4f, // 3
    0x66, // 4
    0x6d, // 5
    0x7d, // 6
    0x07, // 7
    0x7f, // 8
    0x6f, // 9
}};

constexpr std::array<Stroke, 3> LETTER_K{{
    {{0.f, 0.f},   {0.f, 1.f}},
    {{0.f, 0.5f},  {1.f, 1.f}},
    {{0.f, 0.5f},  {1.f, 0.f}},
}};

constexpr std::array<Stroke, 4> LETTER_M{{
    {{0.f, 0.f},   {0.f, 1.f}},
    {{0.f, 1.f},   {0.5f, 0.45f}},
    {{0.5f, 0.45f}, {1.f, 1.f}},
    {{1.f, 1.f},   {1.f, 0.f}},
}};

constexpr std::array<Stroke, 3> LETTER_H{{
    {{0.f, 0.f},   {0.f, 1.f}},
    {{1.f, 0.f},   {1.f, 1.f}},
    {{0.f, 0.5f},  {1.f, 0.5f}},
}};

constexpr std::array<Stroke, 1> SLASH{{
    {{0.f, 0.f},   {1.f, 1.f}},
}};

void emit_stroke(std::vector<Speedometer::Vertex>& v,
                 glm::vec2 a, glm::vec2 b, float thickness, glm::vec3 col) {
    glm::vec2 d = b - a;
    float len = glm::length(d);
    if (len < 1e-5f) return;
    glm::vec2 n{-d.y, d.x};
    n = n / len * (thickness * 0.5f);

    glm::vec2 p0 = a - n;
    glm::vec2 p1 = a + n;
    glm::vec2 p2 = b + n;
    glm::vec2 p3 = b - n;
    v.push_back({p0, col}); v.push_back({p1, col}); v.push_back({p2, col});
    v.push_back({p0, col}); v.push_back({p2, col}); v.push_back({p3, col});
}

template <std::size_t N>
void emit_glyph(std::vector<Speedometer::Vertex>& v,
                const std::array<Stroke, N>& strokes,
                glm::vec2 min, glm::vec2 size, float thickness,
                glm::vec3 col) {
    for (const Stroke& s : strokes) {
        glm::vec2 a{min.x + s.a.x * size.x, min.y + s.a.y * size.y};
        glm::vec2 b{min.x + s.b.x * size.x, min.y + s.b.y * size.y};
        emit_stroke(v, a, b, thickness, col);
    }
}

void emit_digit(std::vector<Speedometer::Vertex>& v,
                int digit, glm::vec2 min, glm::vec2 size,
                float thickness, glm::vec3 col) {
    if (digit < 0 || digit > 9) return;
    unsigned char mask = DIGIT_MASK[static_cast<std::size_t>(digit)];
    for (std::size_t i = 0; i < SEGMENTS.size(); ++i) {
        if ((mask & (1u << i)) == 0u) continue;
        glm::vec2 a{min.x + SEGMENTS[i].a.x * size.x,
                    min.y + SEGMENTS[i].a.y * size.y};
        glm::vec2 b{min.x + SEGMENTS[i].b.x * size.x,
                    min.y + SEGMENTS[i].b.y * size.y};
        emit_stroke(v, a, b, thickness, col);
    }
}

} // namespace

bool Speedometer::init(const std::string& assets_root) {
    if (!shader_.load(assets_root + "/shaders/minimap.vert",
                      assets_root + "/shaders/minimap.frag")) {
        PE_ERROR("Speedometer: failed to load shaders");
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, color)));

    glBindVertexArray(0);
    return true;
}

void Speedometer::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_);      vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

void Speedometer::emit_disk(std::vector<Vertex>& v, glm::vec2 c, float r,
                            int sides, glm::vec3 col) {
    float prev_x = c.x + r;
    float prev_y = c.y;
    for (int i = 1; i <= sides; ++i) {
        float ang = static_cast<float>(i) / static_cast<float>(sides) *
                    glm::two_pi<float>();
        float nx = c.x + r * std::cos(ang);
        float ny = c.y + r * std::sin(ang);
        v.push_back({{c.x, c.y}, col});
        v.push_back({{prev_x, prev_y}, col});
        v.push_back({{nx, ny}, col});
        prev_x = nx;
        prev_y = ny;
    }
}

void Speedometer::emit_quad(std::vector<Vertex>& v,
                            glm::vec2 a, glm::vec2 b,
                            glm::vec2 c, glm::vec2 d,
                            glm::vec3 col) {
    v.push_back({a, col}); v.push_back({b, col}); v.push_back({c, col});
    v.push_back({a, col}); v.push_back({c, col}); v.push_back({d, col});
}

void Speedometer::draw(const DrawState& s) {
    if (s.viewport_size_px.x <= 0.f || s.viewport_size_px.y <= 0.f) return;

    std::vector<Vertex> verts;
    verts.reserve(768);

    emit_disk(verts, {0.f, 0.f}, 0.98f, 64, BG_COLOR);
    emit_disk(verts, {0.f, 0.f}, 0.88f, 64, {0.075f, 0.085f, 0.090f});

    constexpr float START_DEG = -130.f;
    constexpr float SWEEP_DEG =  260.f;
    for (int i = 0; i <= 11; ++i) {
        float t = static_cast<float>(i) / 11.f;
        float a = glm::radians(START_DEG + SWEEP_DEG * t);
        glm::vec2 dir{std::sin(a), std::cos(a)};
        float len = (i % 2 == 0) ? 0.15f : 0.09f;
        float thick = (i % 2 == 0) ? 0.030f : 0.020f;
        emit_stroke(verts, dir * (0.74f - len), dir * 0.76f, thick, TICK_COLOR);
    }

    float speed = std::max(0.f, s.speed_kmh);
    float needle_t = std::min(speed / MAX_SPEED_KMH, 1.f);
    float needle_a = glm::radians(START_DEG + SWEEP_DEG * needle_t);
    glm::vec2 needle_dir{std::sin(needle_a), std::cos(needle_a)};
    emit_stroke(verts, needle_dir * -0.12f, needle_dir * 0.66f,
                0.045f, NEEDLE_COLOR);
    emit_disk(verts, {0.f, 0.f}, 0.075f, 24, NEEDLE_COLOR);
    emit_disk(verts, {0.f, 0.f}, 0.045f, 18, RIM_COLOR);

    int display_speed = std::min(999, static_cast<int>(std::round(speed)));
    char digits[4] = {};
    std::snprintf(digits, sizeof(digits), "%d", display_speed);
    int digit_count = static_cast<int>(std::char_traits<char>::length(digits));
    glm::vec2 digit_size{0.20f, 0.31f};
    float gap = 0.055f;
    float total_w = static_cast<float>(digit_count) * digit_size.x
                  + static_cast<float>(std::max(0, digit_count - 1)) * gap;
    float x = -total_w * 0.5f;
    for (int i = 0; i < digit_count; ++i) {
        emit_digit(verts, digits[i] - '0', {x, -0.38f}, digit_size,
                   0.032f, TEXT_COLOR);
        x += digit_size.x + gap;
    }

    glm::vec2 label_size{0.082f, 0.115f};
    float label_gap = 0.045f;
    float label_w = label_size.x * 4.f + label_gap * 3.f;
    float lx = -label_w * 0.5f;
    emit_glyph(verts, LETTER_K, {lx, -0.62f}, label_size, 0.018f, LABEL_COLOR);
    lx += label_size.x + label_gap;
    emit_glyph(verts, LETTER_M, {lx, -0.62f}, label_size, 0.018f, LABEL_COLOR);
    lx += label_size.x + label_gap;
    emit_glyph(verts, SLASH, {lx, -0.62f}, label_size, 0.018f, LABEL_COLOR);
    lx += label_size.x + label_gap;
    emit_glyph(verts, LETTER_H, {lx, -0.62f}, label_size, 0.018f, LABEL_COLOR);

    if (verts.empty()) return;

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    std::size_t bytes = verts.size() * sizeof(Vertex);
    if (bytes > vbo_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes),
                     verts.data(), GL_STREAM_DRAW);
        vbo_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                        verts.data());
    }

    GLboolean depth_was = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cull_was  = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    shader_.use();
    glm::vec2 centre{
        s.viewport_size_px.x - GAUGE_MARGIN_PX - GAUGE_RADIUS_PX,
        s.viewport_size_px.y - GAUGE_MARGIN_PX - GAUGE_RADIUS_PX,
    };
    shader_.set("u_screen_centre_px", centre);
    shader_.set("u_radius_px",        GAUGE_RADIUS_PX);
    shader_.set("u_viewport_px",      s.viewport_size_px);
    shader_.set("u_clip_radius",      10.0f);
    shader_.set("u_rim_radius",       10.0f);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));

    glBindVertexArray(0);
    if (depth_was) glEnable(GL_DEPTH_TEST);
    if (cull_was)  glEnable(GL_CULL_FACE);
}

} // namespace pengine
