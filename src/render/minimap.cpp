#include "render/minimap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include <glm/gtc/constants.hpp>

#include "core/log.h"
#include "world/heightmap.h"
#include "world/road_grid.h"

namespace pengine {

namespace {

// Visible world radius around the player on the minimap, in metres.
constexpr float MAP_WORLD_RADIUS_M = 200.f;

// Pixel layout of the minimap circle in the viewport.
constexpr float MAP_RADIUS_PX = 110.f;
constexpr float MAP_MARGIN_PX = 24.f;

// Colours.
constexpr glm::vec3 BG_COLOR       {0.10f, 0.13f, 0.10f};
constexpr glm::vec3 ROAD_COLOR     {0.42f, 0.42f, 0.45f};
constexpr glm::vec3 PLAYER_COLOR   {1.00f, 0.85f, 0.20f};
constexpr glm::vec3 LABEL_COLOR    {0.95f, 0.95f, 0.95f};
constexpr glm::vec3 LABEL_N_COLOR  {1.00f, 0.40f, 0.40f}; // tint N red so it stands out

// Compass labels orbit at this radius (just outside the minimap rim).
constexpr float LABEL_ORBIT_R   = 1.14f;
constexpr float LABEL_HEIGHT    = 0.11f;  // letter cell height in minimap-local units
constexpr float LABEL_WIDTH     = 0.085f;
constexpr float LABEL_STROKE    = 0.022f; // letter stroke thickness

// Letter strokes: each stroke is a line segment in unit-letter coords
// ([0, 1] x [0, 1]) that gets thickened into a quad.
struct Stroke { glm::vec2 a; glm::vec2 b; };

// "N" — left vertical, diagonal, right vertical
constexpr std::array<Stroke, 3> LETTER_N{{
    {{0.f, 0.f}, {0.f, 1.f}},
    {{0.f, 1.f}, {1.f, 0.f}},
    {{1.f, 0.f}, {1.f, 1.f}},
}};

// "E" — left vertical and three horizontals
constexpr std::array<Stroke, 4> LETTER_E{{
    {{0.f,  0.f}, {0.f, 1.f}},
    {{0.f,  1.f}, {1.f, 1.f}},
    {{0.f,  0.5f}, {0.7f, 0.5f}},
    {{0.f,  0.f}, {1.f, 0.f}},
}};

// "S" — top, top-left vertical, middle, bottom-right vertical, bottom
constexpr std::array<Stroke, 5> LETTER_S{{
    {{0.f, 1.f}, {1.f, 1.f}},
    {{0.f, 0.5f}, {0.f, 1.f}},
    {{0.f, 0.5f}, {1.f, 0.5f}},
    {{1.f, 0.f}, {1.f, 0.5f}},
    {{0.f, 0.f}, {1.f, 0.f}},
}};

// "W" — four diagonals forming the W shape (full height each)
constexpr std::array<Stroke, 4> LETTER_W{{
    {{0.f,  1.f}, {0.25f, 0.f}},
    {{0.25f, 0.f}, {0.5f, 0.6f}},
    {{0.5f,  0.6f}, {0.75f, 0.f}},
    {{0.75f, 0.f}, {1.f,    1.f}},
}};

// Thicken a line segment from a to b into a quad of the given thickness,
// emitting two triangles. Vertices are in minimap-local coords.
void emit_stroke(std::vector<Minimap::Vertex>& v,
                  glm::vec2 a, glm::vec2 b, float thickness, glm::vec3 col) {
    glm::vec2 d = b - a;
    float len = glm::length(d);
    if (len < 1e-5f) return;
    glm::vec2 n{ -d.y, d.x };
    n = n / len * (thickness * 0.5f);
    glm::vec2 p0 = a - n;
    glm::vec2 p1 = a + n;
    glm::vec2 p2 = b + n;
    glm::vec2 p3 = b - n;
    v.push_back({p0, col}); v.push_back({p1, col}); v.push_back({p2, col});
    v.push_back({p0, col}); v.push_back({p2, col}); v.push_back({p3, col});
}

template <std::size_t N>
void emit_letter(std::vector<Minimap::Vertex>& v,
                  const std::array<Stroke, N>& strokes,
                  glm::vec2 centre, float w, float h, glm::vec3 col) {
    // Letter strokes are in [0,1] x [0,1]. Translate so centre maps to
    // `centre`, scale to (w, h).
    for (const Stroke& s : strokes) {
        glm::vec2 a{(s.a.x - 0.5f) * w + centre.x,
                    (s.a.y - 0.5f) * h + centre.y};
        glm::vec2 b{(s.b.x - 0.5f) * w + centre.x,
                    (s.b.y - 0.5f) * h + centre.y};
        emit_stroke(v, a, b, LABEL_STROKE, col);
    }
}

}  // namespace

bool Minimap::init(const std::string& assets_root) {
    if (!shader_.load(assets_root + "/shaders/minimap.vert",
                      assets_root + "/shaders/minimap.frag")) {
        PE_ERROR("Minimap: failed to load shaders");
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

void Minimap::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_);      vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

void Minimap::emit_disk(std::vector<Vertex>& v, glm::vec2 c, float r,
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
        prev_x = nx; prev_y = ny;
    }
}

void Minimap::emit_quad(std::vector<Vertex>& v,
                         glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d,
                         glm::vec3 col) {
    v.push_back({a, col}); v.push_back({b, col}); v.push_back({c, col});
    v.push_back({a, col}); v.push_back({c, col}); v.push_back({d, col});
}

void Minimap::draw(const DrawState& s) {
    if (s.viewport_size_px.x <= 0.f || s.viewport_size_px.y <= 0.f) return;

    float yaw_rad = glm::radians(s.player_yaw_deg);
    float c_yaw = std::cos(yaw_rad);
    float s_yaw = std::sin(yaw_rad);
    float scale = 1.f / MAP_WORLD_RADIUS_M;

    auto world_xz_to_local = [&](float wx, float wz) -> glm::vec2 {
        float dx = wx - s.player_pos_world.x;
        float dz = wz - s.player_pos_world.z;
        // mx along right_xz, my along forward_xz where
        //   right_xz   = (cos yaw, sin yaw),
        //   forward_xz = (sin yaw, -cos yaw).
        float mx = dx * c_yaw + dz * s_yaw;
        float my = dx * s_yaw - dz * c_yaw;
        return {mx * scale, my * scale};
    };

    std::vector<Vertex> verts_clip;   // inside-circle, gets discarded outside r=1
    std::vector<Vertex> verts_label;  // outside-circle, no discard
    verts_clip.reserve(2048);
    verts_label.reserve(256);

    // ---- Background disk ----------------------------------------------------
    emit_disk(verts_clip, {0.f, 0.f}, 1.0f, 64, BG_COLOR);

    // ---- Roads --------------------------------------------------------------
    const float reach = MAP_WORLD_RADIUS_M + ROAD_HALF_WIDTH;
    int ns_lo = static_cast<int>(std::floor((s.player_pos_world.x - reach) / ROAD_PITCH));
    int ns_hi = static_cast<int>(std::ceil ((s.player_pos_world.x + reach) / ROAD_PITCH));
    int ew_lo = static_cast<int>(std::floor((s.player_pos_world.z - reach) / ROAD_PITCH));
    int ew_hi = static_cast<int>(std::ceil ((s.player_pos_world.z + reach) / ROAD_PITCH));

    float world_size = Heightmap::world_size_m();
    float zmin = std::max(0.f, s.player_pos_world.z - reach);
    float zmax = std::min(world_size, s.player_pos_world.z + reach);
    float xmin = std::max(0.f, s.player_pos_world.x - reach);
    float xmax = std::min(world_size, s.player_pos_world.x + reach);

    for (int k = ns_lo; k <= ns_hi; ++k) {
        float xc = static_cast<float>(k) * ROAD_PITCH;
        if (xc < 0.f || xc > world_size) continue;
        glm::vec2 a = world_xz_to_local(xc - ROAD_HALF_WIDTH, zmin);
        glm::vec2 b = world_xz_to_local(xc + ROAD_HALF_WIDTH, zmin);
        glm::vec2 c = world_xz_to_local(xc + ROAD_HALF_WIDTH, zmax);
        glm::vec2 d = world_xz_to_local(xc - ROAD_HALF_WIDTH, zmax);
        emit_quad(verts_clip, a, b, c, d, ROAD_COLOR);
    }
    for (int k = ew_lo; k <= ew_hi; ++k) {
        float zc = static_cast<float>(k) * ROAD_PITCH;
        if (zc < 0.f || zc > world_size) continue;
        glm::vec2 a = world_xz_to_local(xmin, zc - ROAD_HALF_WIDTH);
        glm::vec2 b = world_xz_to_local(xmax, zc - ROAD_HALF_WIDTH);
        glm::vec2 c = world_xz_to_local(xmax, zc + ROAD_HALF_WIDTH);
        glm::vec2 d = world_xz_to_local(xmin, zc + ROAD_HALF_WIDTH);
        emit_quad(verts_clip, a, b, c, d, ROAD_COLOR);
    }

    // ---- Player marker ------------------------------------------------------
    {
        constexpr float L = 0.10f;
        constexpr float W = 0.07f;
        glm::vec2 tip {0.f,  L};
        glm::vec2 bl  {-W,  -L * 0.5f};
        glm::vec2 br  { W,  -L * 0.5f};
        verts_clip.push_back({tip, PLAYER_COLOR});
        verts_clip.push_back({bl,  PLAYER_COLOR});
        verts_clip.push_back({br,  PLAYER_COLOR});
    }

    // ---- Compass labels (N / E / S / W) ------------------------------------
    // World cardinals projected into minimap-local space:
    //   N (-Z): (-sin yaw,  cos yaw)
    //   E (+X): ( cos yaw,  sin yaw)
    //   S (+Z): ( sin yaw, -cos yaw)
    //   W (-X): (-cos yaw, -sin yaw)
    // Letters stay upright in screen space — they just orbit the rim.
    glm::vec2 dir_n{ -s_yaw,  c_yaw };
    glm::vec2 dir_e{  c_yaw,  s_yaw };
    glm::vec2 dir_s{  s_yaw, -c_yaw };
    glm::vec2 dir_w{ -c_yaw, -s_yaw };

    emit_letter(verts_label, LETTER_N, dir_n * LABEL_ORBIT_R,
                 LABEL_WIDTH, LABEL_HEIGHT, LABEL_N_COLOR);
    emit_letter(verts_label, LETTER_E, dir_e * LABEL_ORBIT_R,
                 LABEL_WIDTH, LABEL_HEIGHT, LABEL_COLOR);
    emit_letter(verts_label, LETTER_S, dir_s * LABEL_ORBIT_R,
                 LABEL_WIDTH, LABEL_HEIGHT, LABEL_COLOR);
    emit_letter(verts_label, LETTER_W, dir_w * LABEL_ORBIT_R,
                 LABEL_WIDTH, LABEL_HEIGHT, LABEL_COLOR);

    // ---- Upload + draw -----------------------------------------------------
    GLsizei n_clip  = static_cast<GLsizei>(verts_clip.size());
    GLsizei n_label = static_cast<GLsizei>(verts_label.size());
    if (n_clip == 0 && n_label == 0) return;

    std::vector<Vertex> all;
    all.reserve(verts_clip.size() + verts_label.size());
    all.insert(all.end(), verts_clip.begin(),  verts_clip.end());
    all.insert(all.end(), verts_label.begin(), verts_label.end());

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    std::size_t bytes = all.size() * sizeof(Vertex);
    if (bytes > vbo_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes),
                     all.data(), GL_STREAM_DRAW);
        vbo_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                        all.data());
    }

    GLboolean depth_was = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cull_was  = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    shader_.use();
    glm::vec2 centre{
        MAP_MARGIN_PX + MAP_RADIUS_PX,
        s.viewport_size_px.y - MAP_MARGIN_PX - MAP_RADIUS_PX,
    };
    shader_.set("u_screen_centre_px", centre);
    shader_.set("u_radius_px",        MAP_RADIUS_PX);
    shader_.set("u_viewport_px",      s.viewport_size_px);

    // Pass 1: inside-circle geometry, hard discard at radius 1, rim border.
    shader_.set("u_clip_radius", 1.00f);
    shader_.set("u_rim_radius",  1.00f);
    glDrawArrays(GL_TRIANGLES, 0, n_clip);

    // Pass 2: compass labels (radius ~1.18). No discard, no rim darkening.
    shader_.set("u_clip_radius", 10.00f);
    shader_.set("u_rim_radius",  10.00f);
    glDrawArrays(GL_TRIANGLES, n_clip, n_label);

    glBindVertexArray(0);
    if (depth_was) glEnable(GL_DEPTH_TEST);
    if (cull_was)  glEnable(GL_CULL_FACE);
}

}  // namespace pengine
