#include "render/menu.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <vector>

#include "core/log.h"

namespace pengine {

namespace {

// ---------------------------------------------------------------------------
// Stroke-letter glyphs
// ---------------------------------------------------------------------------
//
// Each glyph is a small set of line segments in a 1x1 unit box, origin
// bottom-left. The renderer scales each glyph to a target pixel size and
// places it at a target pixel origin (top-left of the glyph cell).
//
// Only uppercase A-Z, digits 0-9, and a few punctuation marks needed by the
// current menu copy are defined. Unknown characters (including lowercase
// letters not handled here) draw as a blank space.

struct Stroke { glm::vec2 a; glm::vec2 b; };

template <std::size_t N>
struct Glyph {
    std::array<Stroke, N> strokes;
};

// Uppercase letters. Strokes are authored with +Y up (bottom-left origin)
// in a 1x1 box.

constexpr std::array<Stroke, 3> G_A{{
    {{0.f, 0.f},   {0.5f, 1.f}},
    {{0.5f, 1.f},  {1.f, 0.f}},
    {{0.2f, 0.4f}, {0.8f, 0.4f}},
}};
constexpr std::array<Stroke, 5> G_B{{
    {{0.f, 0.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {0.8f, 1.f}},
    {{0.8f, 1.f}, {0.8f, 0.5f}},
    {{0.f, 0.5f}, {0.8f, 0.5f}},
    {{0.f, 0.f},  {0.8f, 0.f}},
}};
constexpr std::array<Stroke, 4> G_C{{
    {{1.f, 1.f}, {0.f, 1.f}},
    {{0.f, 1.f}, {0.f, 0.f}},
    {{0.f, 0.f}, {1.f, 0.f}},
    {{1.f, 0.f}, {1.f, 0.f}}, // padding (unused)
}};
constexpr std::array<Stroke, 4> G_D{{
    {{0.f, 0.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {0.7f, 1.f}},
    {{0.7f, 1.f}, {1.f, 0.5f}},
    {{1.f, 0.5f}, {0.f, 0.f}},
}};
constexpr std::array<Stroke, 4> G_E{{
    {{1.f, 1.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {0.f, 0.f}},
    {{0.f, 0.f},  {1.f, 0.f}},
    {{0.f, 0.5f}, {0.7f, 0.5f}},
}};
constexpr std::array<Stroke, 3> G_F{{
    {{0.f, 0.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {1.f, 1.f}},
    {{0.f, 0.5f}, {0.7f, 0.5f}},
}};
constexpr std::array<Stroke, 5> G_G{{
    {{1.f, 1.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {0.f, 0.f}},
    {{0.f, 0.f},  {1.f, 0.f}},
    {{1.f, 0.f},  {1.f, 0.5f}},
    {{1.f, 0.5f}, {0.5f, 0.5f}},
}};
constexpr std::array<Stroke, 3> G_H{{
    {{0.f, 0.f},  {0.f, 1.f}},
    {{1.f, 0.f},  {1.f, 1.f}},
    {{0.f, 0.5f}, {1.f, 0.5f}},
}};
constexpr std::array<Stroke, 3> G_I{{
    {{0.f, 0.f}, {1.f, 0.f}},
    {{0.f, 1.f}, {1.f, 1.f}},
    {{0.5f, 0.f}, {0.5f, 1.f}},
}};
constexpr std::array<Stroke, 3> G_J{{
    {{1.f, 1.f},   {1.f, 0.2f}},
    {{1.f, 0.2f},  {0.7f, 0.f}},
    {{0.7f, 0.f},  {0.3f, 0.f}},
}};
constexpr std::array<Stroke, 3> G_K{{
    {{0.f, 0.f},  {0.f, 1.f}},
    {{0.f, 0.5f}, {1.f, 1.f}},
    {{0.f, 0.5f}, {1.f, 0.f}},
}};
constexpr std::array<Stroke, 2> G_L{{
    {{0.f, 1.f}, {0.f, 0.f}},
    {{0.f, 0.f}, {1.f, 0.f}},
}};
constexpr std::array<Stroke, 4> G_M{{
    {{0.f, 0.f},   {0.f, 1.f}},
    {{0.f, 1.f},   {0.5f, 0.45f}},
    {{0.5f, 0.45f},{1.f, 1.f}},
    {{1.f, 1.f},   {1.f, 0.f}},
}};
constexpr std::array<Stroke, 3> G_N{{
    {{0.f, 0.f}, {0.f, 1.f}},
    {{0.f, 1.f}, {1.f, 0.f}},
    {{1.f, 0.f}, {1.f, 1.f}},
}};
constexpr std::array<Stroke, 4> G_O{{
    {{0.f, 0.f}, {0.f, 1.f}},
    {{0.f, 1.f}, {1.f, 1.f}},
    {{1.f, 1.f}, {1.f, 0.f}},
    {{1.f, 0.f}, {0.f, 0.f}},
}};
constexpr std::array<Stroke, 4> G_P{{
    {{0.f, 0.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {1.f, 1.f}},
    {{1.f, 1.f},  {1.f, 0.5f}},
    {{1.f, 0.5f}, {0.f, 0.5f}},
}};
constexpr std::array<Stroke, 5> G_Q{{
    {{0.f, 0.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {1.f, 1.f}},
    {{1.f, 1.f},  {1.f, 0.f}},
    {{1.f, 0.f},  {0.f, 0.f}},
    {{0.6f, 0.3f},{1.1f, -0.1f}},
}};
constexpr std::array<Stroke, 5> G_R{{
    {{0.f, 0.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {1.f, 1.f}},
    {{1.f, 1.f},  {1.f, 0.5f}},
    {{1.f, 0.5f}, {0.f, 0.5f}},
    {{0.f, 0.5f}, {1.f, 0.f}},
}};
constexpr std::array<Stroke, 5> G_S{{
    {{1.f, 1.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {0.f, 0.5f}},
    {{0.f, 0.5f}, {1.f, 0.5f}},
    {{1.f, 0.5f}, {1.f, 0.f}},
    {{1.f, 0.f},  {0.f, 0.f}},
}};
constexpr std::array<Stroke, 2> G_T{{
    {{0.f, 1.f},   {1.f, 1.f}},
    {{0.5f, 1.f},  {0.5f, 0.f}},
}};
constexpr std::array<Stroke, 3> G_U{{
    {{0.f, 1.f}, {0.f, 0.f}},
    {{0.f, 0.f}, {1.f, 0.f}},
    {{1.f, 0.f}, {1.f, 1.f}},
}};
constexpr std::array<Stroke, 2> G_V{{
    {{0.f, 1.f},  {0.5f, 0.f}},
    {{0.5f, 0.f}, {1.f, 1.f}},
}};
constexpr std::array<Stroke, 4> G_W{{
    {{0.f, 1.f},     {0.25f, 0.f}},
    {{0.25f, 0.f},   {0.5f, 0.55f}},
    {{0.5f, 0.55f},  {0.75f, 0.f}},
    {{0.75f, 0.f},   {1.f, 1.f}},
}};
constexpr std::array<Stroke, 2> G_X{{
    {{0.f, 0.f}, {1.f, 1.f}},
    {{0.f, 1.f}, {1.f, 0.f}},
}};
constexpr std::array<Stroke, 3> G_Y{{
    {{0.f, 1.f},  {0.5f, 0.5f}},
    {{1.f, 1.f},  {0.5f, 0.5f}},
    {{0.5f, 0.5f},{0.5f, 0.f}},
}};
constexpr std::array<Stroke, 3> G_Z{{
    {{0.f, 1.f}, {1.f, 1.f}},
    {{1.f, 1.f}, {0.f, 0.f}},
    {{0.f, 0.f}, {1.f, 0.f}},
}};

// Punctuation.
constexpr std::array<Stroke, 2> G_DASH{{
    {{0.1f, 0.5f}, {0.9f, 0.5f}},
    {{0.1f, 0.5f}, {0.1f, 0.5f}}, // pad
}};
constexpr std::array<Stroke, 2> G_DOT{{
    {{0.4f, 0.f}, {0.6f, 0.f}},
    {{0.4f, 0.f}, {0.6f, 0.f}}, // pad (same)
}};
constexpr std::array<Stroke, 2> G_COMMA{{
    {{0.55f, 0.1f}, {0.4f, -0.1f}},
    {{0.55f, 0.1f}, {0.4f, -0.1f}}, // pad
}};
constexpr std::array<Stroke, 2> G_COLON{{
    {{0.4f, 0.7f}, {0.6f, 0.7f}},
    {{0.4f, 0.3f}, {0.6f, 0.3f}},
}};
constexpr std::array<Stroke, 2> G_EQ{{
    {{0.15f, 0.35f}, {0.85f, 0.35f}},
    {{0.15f, 0.65f}, {0.85f, 0.65f}},
}};
constexpr std::array<Stroke, 1> G_SLASH{{
    {{0.f, 0.f}, {1.f, 1.f}},
}};
constexpr std::array<Stroke, 3> G_LPAREN{{
    {{0.7f, 1.f},  {0.3f, 0.7f}},
    {{0.3f, 0.7f}, {0.3f, 0.3f}},
    {{0.3f, 0.3f}, {0.7f, 0.f}},
}};
constexpr std::array<Stroke, 3> G_RPAREN{{
    {{0.3f, 1.f},  {0.7f, 0.7f}},
    {{0.7f, 0.7f}, {0.7f, 0.3f}},
    {{0.7f, 0.3f}, {0.3f, 0.f}},
}};
constexpr std::array<Stroke, 2> G_PLUS{{
    {{0.5f, 0.2f}, {0.5f, 0.8f}},
    {{0.2f, 0.5f}, {0.8f, 0.5f}},
}};

// Digits 0-9.
constexpr std::array<Stroke, 4> G_0{{
    {{0.f, 0.f}, {0.f, 1.f}},
    {{0.f, 1.f}, {1.f, 1.f}},
    {{1.f, 1.f}, {1.f, 0.f}},
    {{1.f, 0.f}, {0.f, 0.f}},
}};
constexpr std::array<Stroke, 2> G_1{{
    {{0.4f, 0.8f}, {0.5f, 1.f}},
    {{0.5f, 1.f},  {0.5f, 0.f}},
}};
constexpr std::array<Stroke, 5> G_2{{
    {{0.f, 1.f},  {1.f, 1.f}},
    {{1.f, 1.f},  {1.f, 0.5f}},
    {{1.f, 0.5f}, {0.f, 0.5f}},
    {{0.f, 0.5f}, {0.f, 0.f}},
    {{0.f, 0.f},  {1.f, 0.f}},
}};
constexpr std::array<Stroke, 4> G_3{{
    {{0.f, 1.f},  {1.f, 1.f}},
    {{1.f, 1.f},  {1.f, 0.f}},
    {{1.f, 0.f},  {0.f, 0.f}},
    {{0.f, 0.5f}, {1.f, 0.5f}},
}};
constexpr std::array<Stroke, 3> G_4{{
    {{0.f, 1.f},  {0.f, 0.5f}},
    {{0.f, 0.5f}, {1.f, 0.5f}},
    {{1.f, 1.f},  {1.f, 0.f}},
}};
constexpr std::array<Stroke, 5> G_5{{
    {{1.f, 1.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {0.f, 0.5f}},
    {{0.f, 0.5f}, {1.f, 0.5f}},
    {{1.f, 0.5f}, {1.f, 0.f}},
    {{1.f, 0.f},  {0.f, 0.f}},
}};
constexpr std::array<Stroke, 5> G_6{{
    {{1.f, 1.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {0.f, 0.f}},
    {{0.f, 0.f},  {1.f, 0.f}},
    {{1.f, 0.f},  {1.f, 0.5f}},
    {{1.f, 0.5f}, {0.f, 0.5f}},
}};
constexpr std::array<Stroke, 2> G_7{{
    {{0.f, 1.f}, {1.f, 1.f}},
    {{1.f, 1.f}, {0.f, 0.f}},
}};
constexpr std::array<Stroke, 5> G_8{{
    {{0.f, 0.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {1.f, 1.f}},
    {{1.f, 1.f},  {1.f, 0.f}},
    {{1.f, 0.f},  {0.f, 0.f}},
    {{0.f, 0.5f}, {1.f, 0.5f}},
}};
constexpr std::array<Stroke, 5> G_9{{
    {{1.f, 0.f},  {1.f, 1.f}},
    {{1.f, 1.f},  {0.f, 1.f}},
    {{0.f, 1.f},  {0.f, 0.5f}},
    {{0.f, 0.5f}, {1.f, 0.5f}},
    {{1.f, 0.5f}, {0.f, 0.f}},
}};

// Dispatch: given a glyph char, append its strokes to `out`. Returns the
// glyph's advance width in unit-box units (1.0 for letters, 0.4 for space).
struct GlyphInfo {
    const Stroke* strokes;
    int           count;
    float         advance;
};

GlyphInfo lookup(char c) {
    char uc = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (uc) {
        case 'A': return {G_A.data(),  3, 1.f};
        case 'B': return {G_B.data(),  5, 1.f};
        case 'C': return {G_C.data(),  3, 1.f}; // last stroke is padding
        case 'D': return {G_D.data(),  4, 1.f};
        case 'E': return {G_E.data(),  4, 1.f};
        case 'F': return {G_F.data(),  3, 1.f};
        case 'G': return {G_G.data(),  5, 1.f};
        case 'H': return {G_H.data(),  3, 1.f};
        case 'I': return {G_I.data(),  3, 0.7f};
        case 'J': return {G_J.data(),  3, 1.f};
        case 'K': return {G_K.data(),  3, 1.f};
        case 'L': return {G_L.data(),  2, 1.f};
        case 'M': return {G_M.data(),  4, 1.1f};
        case 'N': return {G_N.data(),  3, 1.f};
        case 'O': return {G_O.data(),  4, 1.f};
        case 'P': return {G_P.data(),  4, 1.f};
        case 'Q': return {G_Q.data(),  5, 1.f};
        case 'R': return {G_R.data(),  5, 1.f};
        case 'S': return {G_S.data(),  5, 1.f};
        case 'T': return {G_T.data(),  2, 1.f};
        case 'U': return {G_U.data(),  3, 1.f};
        case 'V': return {G_V.data(),  2, 1.f};
        case 'W': return {G_W.data(),  4, 1.1f};
        case 'X': return {G_X.data(),  2, 1.f};
        case 'Y': return {G_Y.data(),  3, 1.f};
        case 'Z': return {G_Z.data(),  3, 1.f};
        case '0': return {G_0.data(), 4, 1.f};
        case '1': return {G_1.data(), 2, 0.7f};
        case '2': return {G_2.data(), 5, 1.f};
        case '3': return {G_3.data(), 4, 1.f};
        case '4': return {G_4.data(), 3, 1.f};
        case '5': return {G_5.data(), 5, 1.f};
        case '6': return {G_6.data(), 5, 1.f};
        case '7': return {G_7.data(), 2, 1.f};
        case '8': return {G_8.data(), 5, 1.f};
        case '9': return {G_9.data(), 5, 1.f};
        case '-': return {G_DASH.data(),   1, 0.8f};
        case '.': return {G_DOT.data(),    1, 0.4f};
        case ',': return {G_COMMA.data(),  1, 0.4f};
        case ':': return {G_COLON.data(),  2, 0.5f};
        case '=': return {G_EQ.data(),     2, 1.f};
        case '/': return {G_SLASH.data(),  1, 0.8f};
        case '(': return {G_LPAREN.data(), 3, 0.6f};
        case ')': return {G_RPAREN.data(), 3, 0.6f};
        case '+': return {G_PLUS.data(),   2, 0.8f};
        case '|': return {G_DASH.data(),   1, 0.4f}; // approximate
        case '_': return {G_DASH.data(),   1, 0.8f};
        case ' ': return {nullptr, 0, 0.5f};
        default:  return {nullptr, 0, 0.5f}; // unknown -> blank space
    }
}

// ---------------------------------------------------------------------------
// Stroke -> quad helper
// ---------------------------------------------------------------------------
//
// Emit a thick line segment as two triangles. All coords are in pixel space
// with +Y down. The shader expects vertex Y already-negated so it can do
// `(x, -y)` and get pixel-from-top.

void emit_stroke_px(std::vector<Menu::Vertex>& v,
                    glm::vec2 a_px, glm::vec2 b_px,
                    float thickness, glm::vec3 col) {
    glm::vec2 d = b_px - a_px;
    float len = glm::length(d);
    if (len < 1e-5f) return;
    glm::vec2 n{-d.y, d.x};
    n = n / len * (thickness * 0.5f);

    auto to_attr = [](glm::vec2 p) {
        return glm::vec2{p.x, -p.y}; // shader negates Y
    };

    glm::vec2 p0 = a_px - n;
    glm::vec2 p1 = a_px + n;
    glm::vec2 p2 = b_px + n;
    glm::vec2 p3 = b_px - n;
    v.push_back({to_attr(p0), col});
    v.push_back({to_attr(p1), col});
    v.push_back({to_attr(p2), col});
    v.push_back({to_attr(p0), col});
    v.push_back({to_attr(p2), col});
    v.push_back({to_attr(p3), col});
}

void emit_quad_px(std::vector<Menu::Vertex>& v,
                  glm::vec2 min_px, glm::vec2 max_px, glm::vec3 col) {
    auto to_attr = [](glm::vec2 p) {
        return glm::vec2{p.x, -p.y};
    };
    glm::vec2 a{min_px.x, min_px.y};
    glm::vec2 b{max_px.x, min_px.y};
    glm::vec2 c{max_px.x, max_px.y};
    glm::vec2 d{min_px.x, max_px.y};
    v.push_back({to_attr(a), col});
    v.push_back({to_attr(b), col});
    v.push_back({to_attr(c), col});
    v.push_back({to_attr(a), col});
    v.push_back({to_attr(c), col});
    v.push_back({to_attr(d), col});
}

// Measure: width (in unit-box units) of a string at unit glyph size,
// accounting for inter-glyph spacing.
float measure_units(const char* str, float spacing) {
    if (!str) return 0.f;
    float w = 0.f;
    bool first = true;
    for (const char* p = str; *p; ++p) {
        GlyphInfo g = lookup(*p);
        if (!first) w += spacing;
        w += g.advance;
        first = false;
    }
    return w;
}

// Emit a string at pixel origin (top-left of the glyph cell), with the given
// glyph height in pixels. Glyph width is `glyph_h_px` (square cell).
// `spacing` is the horizontal gap between glyphs in unit-box units.
void emit_string(std::vector<Menu::Vertex>& v,
                 const char* str, glm::vec2 origin_top_left_px,
                 float glyph_h_px, float thickness_px, float spacing,
                 glm::vec3 col) {
    if (!str) return;
    float pen_x = origin_top_left_px.x;
    float top_y = origin_top_left_px.y;
    float bot_y = top_y + glyph_h_px;

    bool first = true;
    for (const char* p = str; *p; ++p) {
        GlyphInfo g = lookup(*p);
        if (!first) pen_x += spacing * glyph_h_px;
        for (int i = 0; i < g.count; ++i) {
            const Stroke& s = g.strokes[i];
            // Source authored with +Y up (1.0 = top). Pixel space is +Y
            // down. Map: y_px = bot - sy * height.
            glm::vec2 a_px{
                pen_x + s.a.x * glyph_h_px,
                bot_y - s.a.y * glyph_h_px,
            };
            glm::vec2 b_px{
                pen_x + s.b.x * glyph_h_px,
                bot_y - s.b.y * glyph_h_px,
            };
            emit_stroke_px(v, a_px, b_px, thickness_px, col);
        }
        pen_x += g.advance * glyph_h_px;
        first = false;
    }
}

constexpr glm::vec3 BG_COLOR        {0.04f, 0.05f, 0.07f};
constexpr glm::vec3 TITLE_COLOR     {0.95f, 0.92f, 0.55f};
constexpr glm::vec3 ITEM_COLOR      {0.86f, 0.90f, 0.92f};
constexpr glm::vec3 ITEM_SEL_COLOR  {0.10f, 0.10f, 0.12f};
constexpr glm::vec3 HILITE_COLOR    {0.95f, 0.78f, 0.20f};
constexpr glm::vec3 FOOTER_COLOR    {0.55f, 0.62f, 0.66f};

constexpr float TITLE_GLYPH_H_PX = 48.f;
constexpr float ITEM_GLYPH_H_PX  = 32.f;
constexpr float FOOTER_GLYPH_H_PX = 20.f;
constexpr float TITLE_THICK_PX   = 5.f;
constexpr float ITEM_THICK_PX    = 3.5f;
constexpr float FOOTER_THICK_PX  = 2.f;
constexpr float GLYPH_SPACING    = 0.25f; // unit-box units
constexpr float ITEM_ROW_GAP_PX  = 18.f;

} // namespace

bool Menu::init(const std::string& assets_root) {
    if (!shader_.load(assets_root + "/shaders/minimap.vert",
                      assets_root + "/shaders/minimap.frag")) {
        PE_ERROR("Menu: failed to load shaders");
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

void Menu::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_);      vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    shader_.destroy();
}

void Menu::draw(const DrawState& s) {
    if (s.viewport_size_px.x <= 0.f || s.viewport_size_px.y <= 0.f) return;

    std::vector<Vertex> verts;
    verts.reserve(1024);

    // Full-screen background quad so the world behind us doesn't bleed
    // through. We're already clearing depth/colour before this, but a tinted
    // background reads more deliberately than the sky-blue clear colour.
    emit_quad_px(verts, {0.f, 0.f}, s.viewport_size_px, BG_COLOR);

    const float vw = s.viewport_size_px.x;
    const float vh = s.viewport_size_px.y;
    const float cx = vw * 0.5f;

    // Title: centred horizontally, about 18% from the top.
    {
        float w_units = measure_units(s.title, GLYPH_SPACING);
        float total_px = w_units * TITLE_GLYPH_H_PX;
        glm::vec2 origin{cx - total_px * 0.5f, vh * 0.18f};
        emit_string(verts, s.title, origin, TITLE_GLYPH_H_PX,
                    TITLE_THICK_PX, GLYPH_SPACING, TITLE_COLOR);
    }

    // Items: vertically stacked, centred. The block of items is centred
    // around the screen midline.
    int item_count = s.item_count;
    if (item_count > 0 && s.items != nullptr) {
        const float row_h = ITEM_GLYPH_H_PX + ITEM_ROW_GAP_PX;
        float block_h = static_cast<float>(item_count) * row_h - ITEM_ROW_GAP_PX;
        float start_y = vh * 0.5f - block_h * 0.5f;

        for (int i = 0; i < item_count; ++i) {
            const char* text = s.items[i] ? s.items[i] : "";
            float w_units = measure_units(text, GLYPH_SPACING);
            float total_px = w_units * ITEM_GLYPH_H_PX;
            float row_top = start_y + static_cast<float>(i) * row_h;
            glm::vec2 origin{cx - total_px * 0.5f, row_top};

            const bool is_selected = (i == s.selected_index);
            glm::vec3 col = is_selected ? ITEM_SEL_COLOR : ITEM_COLOR;

            if (is_selected) {
                // Highlight bar behind the selected row.
                const float pad_x = 28.f;
                const float pad_y = 8.f;
                glm::vec2 min_px{cx - total_px * 0.5f - pad_x,
                                  row_top - pad_y};
                glm::vec2 max_px{cx + total_px * 0.5f + pad_x,
                                  row_top + ITEM_GLYPH_H_PX + pad_y};
                emit_quad_px(verts, min_px, max_px, HILITE_COLOR);
            }

            emit_string(verts, text, origin, ITEM_GLYPH_H_PX,
                        ITEM_THICK_PX, GLYPH_SPACING, col);
        }
    }

    // Footer: small hint text near the bottom.
    if (s.footer && s.footer[0] != '\0') {
        float w_units = measure_units(s.footer, GLYPH_SPACING);
        float total_px = w_units * FOOTER_GLYPH_H_PX;
        glm::vec2 origin{cx - total_px * 0.5f, vh - 60.f};
        emit_string(verts, s.footer, origin, FOOTER_GLYPH_H_PX,
                    FOOTER_THICK_PX, GLYPH_SPACING, FOOTER_COLOR);
    }

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
    // Geometry is already in pixel coords. Place centre at origin, scale 1.
    shader_.set("u_screen_centre_px", glm::vec2{0.f, 0.f});
    shader_.set("u_radius_px",        1.0f);
    shader_.set("u_viewport_px",      s.viewport_size_px);
    // Effectively disable the unit-circle clip/rim that the minimap shader
    // uses for the radar.
    shader_.set("u_clip_radius",      1.0e9f);
    shader_.set("u_rim_radius",       1.0e9f);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));

    glBindVertexArray(0);
    if (depth_was) glEnable(GL_DEPTH_TEST);
    if (cull_was)  glEnable(GL_CULL_FACE);
}

// PBD-026: small text-helper for the Map Builder inspector readout.
// Renders a vertical stack of stroke-font lines without any of the
// full-screen background / centering decoration that `draw()` does. Reuses
// the same shader + VBO so we don't introduce a second text path.
void Menu::draw_text_lines(const TextLines& t) {
    if (t.viewport_size_px.x <= 0.f || t.viewport_size_px.y <= 0.f) return;
    if (!t.lines || t.count <= 0) return;

    std::vector<Vertex> verts;
    verts.reserve(static_cast<std::size_t>(t.count) * 64u);

    const float row_h = t.glyph_h_px * 1.45f;  // ~45% line gap, like the menu items
    for (int i = 0; i < t.count; ++i) {
        const char* s = t.lines[i] ? t.lines[i] : "";
        glm::vec2 origin{t.origin_top_left_px.x,
                          t.origin_top_left_px.y + static_cast<float>(i) * row_h};
        emit_string(verts, s, origin, t.glyph_h_px,
                    t.thickness_px, GLYPH_SPACING, t.color);
    }

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
    shader_.set("u_screen_centre_px", glm::vec2{0.f, 0.f});
    shader_.set("u_radius_px",        1.0f);
    shader_.set("u_viewport_px",      t.viewport_size_px);
    shader_.set("u_clip_radius",      1.0e9f);
    shader_.set("u_rim_radius",       1.0e9f);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));

    glBindVertexArray(0);
    if (depth_was) glEnable(GL_DEPTH_TEST);
    if (cull_was)  glEnable(GL_CULL_FACE);
}

} // namespace pengine
