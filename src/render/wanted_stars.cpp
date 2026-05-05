#include "render/wanted_stars.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "core/log.h"

namespace pengine {

namespace {

constexpr int   STAR_COUNT      = 5;
constexpr float STAR_OUTER_PX   = 12.5f;
constexpr float STAR_INNER_PX   = 5.6f;
constexpr float STAR_GAP_PX     = 6.f;
constexpr float HUD_MARGIN_X_PX = 24.f;
constexpr float HUD_MARGIN_Y_PX = 26.f;

constexpr glm::vec3 ACTIVE_COLOR  {1.00f,  0.82f,  0.18f};
constexpr glm::vec3 INACTIVE_COLOR{0.10f,  0.11f,  0.12f};
constexpr glm::vec3 SHADOW_COLOR  {0.005f, 0.006f, 0.008f};
constexpr glm::vec3 TIME_COLOR    {0.97f,  0.96f,  0.92f};
constexpr glm::vec3 MONEY_COLOR   {0.40f,  0.98f,  0.42f};
constexpr glm::vec3 HEALTH_COLOR  {0.96f,  0.32f,  0.55f};
constexpr glm::vec3 ARMOR_COLOR   {0.84f,  0.88f,  0.94f};

struct Stroke { glm::vec2 a; glm::vec2 b; };

constexpr std::array<Stroke, 7> SEGMENTS{{
    {{0.f, 1.f},   {1.f, 1.f}},
    {{1.f, 1.f},   {1.f, 0.5f}},
    {{1.f, 0.5f},  {1.f, 0.f}},
    {{0.f, 0.f},   {1.f, 0.f}},
    {{0.f, 0.5f},  {0.f, 0.f}},
    {{0.f, 1.f},   {0.f, 0.5f}},
    {{0.f, 0.5f},  {1.f, 0.5f}},
}};

constexpr std::array<unsigned char, 10> DIGIT_MASK{{
    0x3f, 0x06, 0x5b, 0x4f, 0x66,
    0x6d, 0x7d, 0x07, 0x7f, 0x6f,
}};

void emit_quad(std::vector<WantedStars::Vertex>& verts,
               glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d,
               glm::vec3 color) {
    verts.push_back({a, color}); verts.push_back({b, color});
    verts.push_back({c, color}); verts.push_back({a, color});
    verts.push_back({c, color}); verts.push_back({d, color});
}

void emit_stroke(std::vector<WantedStars::Vertex>& verts,
                 glm::vec2 a, glm::vec2 b, float thickness,
                 glm::vec3 color) {
    glm::vec2 d = b - a;
    float len = glm::length(d);
    if (len < 1e-5f) return;
    glm::vec2 n{-d.y, d.x};
    n = n / len * (thickness * 0.5f);
    emit_quad(verts, a - n, a + n, b + n, b - n, color);
}

void emit_disk(std::vector<WantedStars::Vertex>& verts,
               glm::vec2 c, float r, int sides, glm::vec3 color) {
    float prev_x = c.x + r;
    float prev_y = c.y;
    for (int i = 1; i <= sides; ++i) {
        float ang = static_cast<float>(i) / static_cast<float>(sides)
                  * glm::two_pi<float>();
        float nx = c.x + r * std::cos(ang);
        float ny = c.y + r * std::sin(ang);
        verts.push_back({c, color});
        verts.push_back({{prev_x, prev_y}, color});
        verts.push_back({{nx, ny}, color});
        prev_x = nx;
        prev_y = ny;
    }
}

void emit_digit(std::vector<WantedStars::Vertex>& verts,
                int digit, glm::vec2 min, glm::vec2 size,
                float thickness, glm::vec3 color) {
    if (digit < 0 || digit > 9) return;
    unsigned char mask = DIGIT_MASK[static_cast<std::size_t>(digit)];
    for (std::size_t i = 0; i < SEGMENTS.size(); ++i) {
        if ((mask & (1u << i)) == 0u) continue;
        glm::vec2 a{min.x + SEGMENTS[i].a.x * size.x,
                    min.y + SEGMENTS[i].a.y * size.y};
        glm::vec2 b{min.x + SEGMENTS[i].b.x * size.x,
                    min.y + SEGMENTS[i].b.y * size.y};
        emit_stroke(verts, a, b, thickness, color);
    }
}

float glyph_width(char ch, glm::vec2 digit_size) {
    if (ch == ':') return digit_size.x * 0.30f;
    if (ch == '$') return digit_size.x * 0.78f;
    return digit_size.x;
}

void emit_colon(std::vector<WantedStars::Vertex>& verts,
                glm::vec2 min, glm::vec2 size, glm::vec3 color) {
    float r = size.x * 0.09f;
    emit_disk(verts, {min.x + size.x * 0.5f, min.y + size.y * 0.68f},
              r, 12, color);
    emit_disk(verts, {min.x + size.x * 0.5f, min.y + size.y * 0.32f},
              r, 12, color);
}

void emit_dollar(std::vector<WantedStars::Vertex>& verts,
                 glm::vec2 min, glm::vec2 size, float thickness,
                 glm::vec3 color) {
    // Dollar sign as a compact segmented S plus a vertical strike.
    emit_digit(verts, 5, min, size, thickness, color);
    emit_stroke(verts,
                {min.x + size.x * 0.50f, min.y - size.y * 0.10f},
                {min.x + size.x * 0.50f, min.y + size.y * 1.10f},
                thickness * 0.72f, color);
}

void emit_text_right(std::vector<WantedStars::Vertex>& verts,
                     const char* text, glm::vec2 right_top,
                     glm::vec2 digit_size, float thickness,
                     glm::vec3 color) {
    int len = 0;
    while (text[len] != '\0') ++len;

    float total_w = 0.f;
    constexpr float GAP = 3.f;
    for (int i = 0; i < len; ++i) {
        total_w += glyph_width(text[i], digit_size);
        if (i + 1 < len) total_w += GAP;
    }

    float x = right_top.x - total_w;
    float y = right_top.y - digit_size.y;
    for (int i = 0; i < len; ++i) {
        char ch = text[i];
        float w = glyph_width(ch, digit_size);
        glm::vec2 size{w, digit_size.y};
        if (ch >= '0' && ch <= '9') {
            emit_digit(verts, ch - '0', {x, y}, size, thickness, color);
        } else if (ch == ':') {
            emit_colon(verts, {x, y}, size, color);
        } else if (ch == '$') {
            emit_dollar(verts, {x, y}, size, thickness, color);
        }
        x += w + GAP;
    }
}

void emit_text_right_shadow(std::vector<WantedStars::Vertex>& verts,
                            const char* text, glm::vec2 right_top,
                            glm::vec2 digit_size, float thickness,
                            glm::vec3 color) {
    emit_text_right(verts, text, right_top + glm::vec2{3.f, -3.f},
                    digit_size, thickness + 1.1f, SHADOW_COLOR);
    emit_text_right(verts, text, right_top, digit_size, thickness, color);
}

void emit_plus_icon(std::vector<WantedStars::Vertex>& verts,
                    glm::vec2 c, float r, glm::vec3 color) {
    float arm = r * 0.36f;
    emit_quad(verts, {c.x - arm, c.y - r}, {c.x + arm, c.y - r},
              {c.x + arm, c.y + r}, {c.x - arm, c.y + r}, color);
    emit_quad(verts, {c.x - r, c.y - arm}, {c.x + r, c.y - arm},
              {c.x + r, c.y + arm}, {c.x - r, c.y + arm}, color);
}

void emit_shield_icon(std::vector<WantedStars::Vertex>& verts,
                      glm::vec2 c, float r, glm::vec3 color) {
    glm::vec2 p0{c.x, c.y - r};
    glm::vec2 p1{c.x + r * 0.85f, c.y - r * 0.50f};
    glm::vec2 p2{c.x + r * 0.65f, c.y + r * 0.35f};
    glm::vec2 p3{c.x, c.y + r};
    glm::vec2 p4{c.x - r * 0.65f, c.y + r * 0.35f};
    glm::vec2 p5{c.x - r * 0.85f, c.y - r * 0.50f};
    verts.push_back({c, color}); verts.push_back({p0, color});
    verts.push_back({p1, color}); verts.push_back({c, color});
    verts.push_back({p1, color}); verts.push_back({p2, color});
    verts.push_back({c, color}); verts.push_back({p2, color});
    verts.push_back({p3, color}); verts.push_back({c, color});
    verts.push_back({p3, color}); verts.push_back({p4, color});
    verts.push_back({c, color}); verts.push_back({p4, color});
    verts.push_back({p5, color}); verts.push_back({c, color});
    verts.push_back({p5, color}); verts.push_back({p0, color});
}

void emit_star(std::vector<WantedStars::Vertex>& verts,
               glm::vec2 centre, float outer_r, float inner_r,
               glm::vec3 color) {
    glm::vec2 pts[10];
    constexpr float START = glm::half_pi<float>();
    for (int i = 0; i < 10; ++i) {
        float r = (i % 2 == 0) ? outer_r : inner_r;
        float a = START + static_cast<float>(i) * glm::pi<float>() / 5.f;
        pts[i] = centre + glm::vec2{std::cos(a), std::sin(a)} * r;
    }

    for (int i = 0; i < 10; ++i) {
        verts.push_back({centre, color});
        verts.push_back({pts[i], color});
        verts.push_back({pts[(i + 1) % 10], color});
    }
}

} // namespace

bool WantedStars::init(const std::string& assets_root) {
    if (!shader_.load(assets_root + "/shaders/minimap.vert",
                      assets_root + "/shaders/minimap.frag")) {
        PE_ERROR("WantedStars: failed to load shaders");
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

void WantedStars::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_);      vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

void WantedStars::draw(const DrawState& s) {
    if (s.viewport_size_px.x <= 0.f || s.viewport_size_px.y <= 0.f) return;

    int wanted = std::clamp(s.wanted_level, 0, STAR_COUNT);
    int health = std::clamp(static_cast<int>(std::round(s.health)), 0, 999);
    int armor = std::clamp(static_cast<int>(std::round(s.armor)), 0, 999);
    int money = std::clamp(s.money, 0, 99999999);
    int hour = ((s.hour % 24) + 24) % 24;
    int minute = ((s.minute % 60) + 60) % 60;

    std::vector<Vertex> verts;
    verts.reserve(2400);

    constexpr float RIGHT_X = -8.f;

    char money_buf[10] = {};
    std::snprintf(money_buf, sizeof(money_buf), "$%08d", money);
    emit_text_right_shadow(verts, money_buf, {RIGHT_X, -8.f},
                           {18.f, 30.f}, 3.4f, MONEY_COLOR);

    char time_buf[6] = {};
    std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d", hour, minute);
    emit_text_right_shadow(verts, time_buf, {RIGHT_X, -54.f},
                           {15.f, 25.f}, 2.8f, TIME_COLOR);

    float star_total_w = STAR_COUNT * STAR_OUTER_PX * 2.f
                       + (STAR_COUNT - 1) * STAR_GAP_PX;
    float star_right_edge = RIGHT_X;
    float star_left_edge  = star_right_edge - star_total_w;
    for (int i = 0; i < STAR_COUNT; ++i) {
        float x = star_left_edge + STAR_OUTER_PX
                + static_cast<float>(i) * (STAR_OUTER_PX * 2.f + STAR_GAP_PX);
        glm::vec2 c{x, -103.f};
        emit_star(verts, c + glm::vec2{2.f, -2.f},
                  STAR_OUTER_PX + 2.f, STAR_INNER_PX + 1.f, SHADOW_COLOR);
        emit_star(verts, c, STAR_OUTER_PX, STAR_INNER_PX,
                  i < wanted ? ACTIVE_COLOR : INACTIVE_COLOR);
    }

    constexpr glm::vec2 STAT_DIGIT_SIZE{16.f, 27.f};
    constexpr float     STAT_GLYPH_GAP   = 3.f;
    constexpr float     STAT_ICON_RADIUS = 9.5f;
    constexpr float     STAT_ICON_GAP    = 14.f;
    const float stat_digits_w = 3.f * STAT_DIGIT_SIZE.x + 2.f * STAT_GLYPH_GAP;
    const float stat_icon_x   = RIGHT_X - stat_digits_w - STAT_ICON_GAP;

    char health_buf[4] = {};
    std::snprintf(health_buf, sizeof(health_buf), "%03d", health);
    const float health_top   = -136.f;
    const float health_icon_y = health_top - STAT_DIGIT_SIZE.y * 0.5f;
    emit_plus_icon(verts, {stat_icon_x + 2.f, health_icon_y - 2.f},
                   STAT_ICON_RADIUS + 0.6f, SHADOW_COLOR);
    emit_plus_icon(verts, {stat_icon_x, health_icon_y},
                   STAT_ICON_RADIUS, HEALTH_COLOR);
    emit_text_right_shadow(verts, health_buf, {RIGHT_X, health_top},
                           STAT_DIGIT_SIZE, 3.0f, HEALTH_COLOR);

    char armor_buf[4] = {};
    std::snprintf(armor_buf, sizeof(armor_buf), "%03d", armor);
    const float armor_top    = -176.f;
    const float armor_icon_y = armor_top - STAT_DIGIT_SIZE.y * 0.5f;
    emit_shield_icon(verts, {stat_icon_x + 2.f, armor_icon_y - 2.f},
                     STAT_ICON_RADIUS + 1.1f, SHADOW_COLOR);
    emit_shield_icon(verts, {stat_icon_x, armor_icon_y},
                     STAT_ICON_RADIUS + 0.5f, ARMOR_COLOR);
    emit_text_right_shadow(verts, armor_buf, {RIGHT_X, armor_top},
                           STAT_DIGIT_SIZE, 3.0f, ARMOR_COLOR);

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
    shader_.set("u_screen_centre_px",
                glm::vec2{s.viewport_size_px.x - HUD_MARGIN_X_PX,
                          HUD_MARGIN_Y_PX});
    shader_.set("u_radius_px",   1.f);
    shader_.set("u_viewport_px", s.viewport_size_px);
    shader_.set("u_clip_radius", 10000.f);
    shader_.set("u_rim_radius",  10000.f);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
    glBindVertexArray(0);

    if (depth_was) glEnable(GL_DEPTH_TEST);
    if (cull_was)  glEnable(GL_CULL_FACE);
}

} // namespace pengine
