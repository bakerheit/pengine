#pragma once
// The respray booth's colour picker: the preset table, the HSV custom
// controls, the layout that drawing and pointer hit-testing share, and the
// focus model a pad or keyboard drives.
//
// Header-only and free of the host layer, so the whole picker runs in a
// headless suite (tests/paint_picker_tests.cpp). The host layer owns raw
// events, drawing and the paint pool; it calls the entry points below and
// reads preview() at most once per rendered frame (preview_changed()).
//
// Coordinates are UiCanvas units (game/ui_canvas.h), never framebuffer
// pixels: map a pointer with UiCanvas::from_window before passing it in.
// Drawing and hit-testing read the same PaintPickerLayout, which is what stops
// "highlights one control, picks another".
//
// Timing is host UI time handed to update(dt). The sim is paused while the
// booth is open, so nothing here reads the sim clock, and nothing here reaches
// a sim step except the PaintOrder handed over on confirm.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>

#include <glm/glm.hpp>

#include "game/vehicle_paint.h"

namespace apricot {

// ---------------------------------------------------------------------------
// HSV. Hue is in turns, [0, 1). Saturation and value are [0, 1].
// ---------------------------------------------------------------------------
struct PaintHsv {
    float h = 0.0f, s = 0.0f, v = 0.0f;
};

// The hue bar has two red ends and no wrap: a clamp keeps Left/Right simple.
inline constexpr float kPaintHueMax = 0.99999f;

// sRGB-encoded, as the HUD draws every other colour. Piecewise linear in hue
// with its kinks at sixths, and bilinear in (s, v) at a fixed hue; the
// tessellation counts below rely on both.
inline glm::vec3 paint_hsv_to_rgb(PaintHsv c) {
    const double hp = std::clamp(static_cast<double>(c.h), 0.0, 1.0) * 6.0;
    const double sat = std::clamp(static_cast<double>(c.s), 0.0, 1.0);
    const double val = std::clamp(static_cast<double>(c.v), 0.0, 1.0);
    const double chroma = val * sat;
    const double x = chroma * (1.0 - std::fabs(std::fmod(hp, 2.0) - 1.0));
    const double m = val - chroma;
    double r = 0, g = 0, b = 0;
    switch (std::min(static_cast<int>(hp), 5)) {
        case 0: r = chroma; g = x; break;
        case 1: r = x; g = chroma; break;
        case 2: g = chroma; b = x; break;
        case 3: g = x; b = chroma; break;
        case 4: r = x; b = chroma; break;
        default: r = chroma; b = x; break;
    }
    return {static_cast<float>(r + m), static_cast<float>(g + m), static_cast<float>(b + m)};
}

inline uint8_t paint_unit_to_byte(double unit) {
    return static_cast<uint8_t>(std::floor(std::clamp(unit, 0.0, 1.0) * 255.0 + 0.5));
}

inline PaintColor hsv_to_rgb8(PaintHsv c) {
    const glm::vec3 rgb = paint_hsv_to_rgb(c);
    return {paint_unit_to_byte(rgb.r), paint_unit_to_byte(rgb.g), paint_unit_to_byte(rgb.b)};
}

// A grey has no hue, so it keeps `keep_hue`: dragging to grey and back must not
// make the hue jump.
inline PaintHsv rgb8_to_hsv(PaintColor c, float keep_hue) {
    const int r = c.r, g = c.g, b = c.b;
    const int hi = std::max({r, g, b}), lo = std::min({r, g, b});
    const int span = hi - lo;
    PaintHsv out;
    out.v = static_cast<float>(hi / 255.0);
    out.s = hi == 0 ? 0.0f : static_cast<float>(static_cast<double>(span) / hi);
    if (span == 0) {
        out.h = std::clamp(keep_hue, 0.0f, kPaintHueMax);
        return out;
    }
    double h = 0;
    if (hi == r)
        h = static_cast<double>(g - b) / span;
    else if (hi == g)
        h = 2.0 + static_cast<double>(b - r) / span;
    else
        h = 4.0 + static_cast<double>(r - g) / span;
    h /= 6.0;
    if (h < 0) h += 1.0;
    out.h = std::min(static_cast<float>(h), kPaintHueMax);
    return out;
}

// ---------------------------------------------------------------------------
// Presets. Real car paint names; sRGB targets for the recolour, which decides
// the exact output. Group labels avoid "metallic": a respray changes albedo,
// not the material. CREAM and DESERT TAN sat 0.034 and 0.045 OKLab from BONE
// WHITE and CHAMPAGNE, too close to tell apart on a car; COBALT BLUE and ULTRA
// VIOLET replaced them, and paint_picker_tests holds every pair >= 0.06.
// Saves store RGB, never an index, so this table may be reordered.
// ---------------------------------------------------------------------------
struct PaintPreset {
    const char* name;   // at most 14 characters: the chip and name line fit that
    const char* group;
    PaintColor rgb;
};

inline constexpr int kPaintGridColumns = 6, kPaintGridRows = 4;
inline constexpr std::array<PaintPreset, 24> kPaintPresets{{
    {"BONE WHITE", "SOLID", {0xEA, 0xE7, 0xDF}},
    {"JET BLACK", "SOLID", {0x11, 0x12, 0x14}},
    {"SIGNAL RED", "SOLID", {0xB5, 0x12, 0x1B}},
    {"TAXI YELLOW", "SOLID", {0xE7, 0xB4, 0x16}},
    {"RACING GREEN", "SOLID", {0x13, 0x4A, 0x2C}},
    {"HARBOR BLUE", "SOLID", {0x1D, 0x4E, 0x89}},
    {"SILVER FROST", "DEEP", {0xBF, 0xC3, 0xC7}},
    {"GUNMETAL", "DEEP", {0x4B, 0x51, 0x57}},
    {"MIDNIGHT BLUE", "DEEP", {0x1A, 0x24, 0x42}},
    {"CHAMPAGNE", "DEEP", {0xCD, 0xB7, 0x8E}},
    {"BURGUNDY", "DEEP", {0x5E, 0x16, 0x23}},
    {"COPPER", "DEEP", {0x9A, 0x5B, 0x35}},
    {"SUNSET ORANGE", "BRIGHT", {0xE4, 0x58, 0x1C}},
    {"LIME", "BRIGHT", {0x8D, 0xC6, 0x3F}},
    {"HOT PINK", "BRIGHT", {0xDC, 0x3F, 0x87}},
    {"SKY BLUE", "BRIGHT", {0x56, 0xA7, 0xDC}},
    {"ROYAL PURPLE", "BRIGHT", {0x4E, 0x2C, 0x83}},
    {"TEAL", "BRIGHT", {0x13, 0x83, 0x7C}},
    {"ULTRA VIOLET", "BRIGHT", {0x8A, 0x3F, 0xD1}},
    {"OLIVE DRAB", "UTILITY", {0x5B, 0x5D, 0x34}},
    {"CHOCOLATE", "UTILITY", {0x4B, 0x2F, 0x21}},
    {"PRIMER GREY", "UTILITY", {0x7D, 0x80, 0x80}},
    {"MINT", "UTILITY", {0xA9, 0xD8, 0xC1}},
    {"COBALT BLUE", "SOLID", {0x23, 0x56, 0xC9}},
}};

// Index of the preset with exactly this colour, or -1.
inline int find_paint_preset(PaintColor c) {
    for (std::size_t i = 0; i < kPaintPresets.size(); ++i)
        if (kPaintPresets[i].rgb == c) return static_cast<int>(i);
    return -1;
}

// Factory orders carry no colour, so two factory orders always compare equal.
inline PaintOrder normalised_paint_order(PaintOrder o) {
    if (o.factory) o.colour = {};
    return o;
}

// "FACTORY PAINT", the preset's name, or "CUSTOM": the name the chips and the
// completion card show.
inline const char* paint_order_name(PaintOrder o) {
    if (o.factory) return "FACTORY PAINT";
    const int i = find_paint_preset(o.colour);
    return i >= 0 ? kPaintPresets[static_cast<std::size_t>(i)].name : "CUSTOM";
}

// Weighted RGB distance ("redmean"), squared. Cheap and good enough to pick a
// default that visibly changes the car.
inline double paint_redmean_distance_sq(PaintColor a, PaintColor b) {
    const double rmean = (a.r + b.r) * 0.5;
    const double dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return (2.0 + rmean / 256.0) * dr * dr + 4.0 * dg * dg +
           (2.0 + (255.0 - rmean) / 256.0) * db * db;
}

// The booth opens on the preset farthest from the car's paint, so the first
// frame already shows a preview. Ties go to the lowest index.
inline int paint_contrast_preset(PaintColor current) {
    int best = 0;
    double best_d = -1.0;
    for (std::size_t i = 0; i < kPaintPresets.size(); ++i) {
        const double d = paint_redmean_distance_sq(current, kPaintPresets[i].rgb);
        if (d > best_d) {
            best_d = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Gradient vertices, shared by the host layer's draw and the tessellation test.
// ---------------------------------------------------------------------------
// The SV plane is bilinear at a fixed hue, which two triangles get wrong by up
// to 63.75/255 (the s*v term). A 12x12 grid of cells keeps triangle
// interpolation within 0.44/255 for 864 vertices.
inline constexpr int kPaintPlaneGrid = 12;
// The hue bar's kinks fall on multiples of 60 degrees, so 36 segments are exact.
inline constexpr int kPaintHueSegments = 36;

// Vertex (i, j) of an n x n grid: saturation runs along i left to right, value
// along j from 1 at the top to 0 at the bottom.
inline glm::vec4 sv_plane_vertex_colour(float hue, int i, int j, int n) {
    const float cells = static_cast<float>(std::max(n, 1));
    const PaintHsv c{hue, static_cast<float>(i) / cells, 1.0f - static_cast<float>(j) / cells};
    return glm::vec4(paint_hsv_to_rgb(c), 1.0f);
}

// Left edge of segment i, and the right edge of segment i-1; i runs 0..36.
inline glm::vec4 hue_segment_colour(int i) {
    const int k = std::clamp(i, 0, kPaintHueSegments);
    const PaintHsv c{static_cast<float>(k) / static_cast<float>(kPaintHueSegments), 1.0f, 1.0f};
    return glm::vec4(paint_hsv_to_rgb(c), 1.0f);
}

// Left and right ends; both tracks are linear in their parameter, so one quad
// per track is exact.
inline std::array<glm::vec4, 2> saturation_track_colours(PaintHsv c) {
    return {glm::vec4(paint_hsv_to_rgb({c.h, 0.0f, c.v}), 1.0f),
            glm::vec4(paint_hsv_to_rgb({c.h, 1.0f, c.v}), 1.0f)};
}
inline std::array<glm::vec4, 2> brightness_track_colours(PaintHsv c) {
    return {glm::vec4(paint_hsv_to_rgb({c.h, c.s, 0.0f}), 1.0f),
            glm::vec4(paint_hsv_to_rgb({c.h, c.s, 1.0f}), 1.0f)};
}

// ---------------------------------------------------------------------------
// Controls and layout.
// ---------------------------------------------------------------------------
enum class PaintTab : uint8_t { Presets, Custom };

// Focus rows, top to bottom as drawn: the chips row sits above the tabs.
// PRESETS walks Chips, Tabs, Grid0..Grid3, Buttons; CUSTOM walks Chips, Tabs,
// Hue, Saturation, Brightness, Buttons. The SV plane is pointer-only.
enum class PaintRow : uint8_t {
    Chips, Tabs, Grid0, Grid1, Grid2, Grid3, Hue, Saturation, Brightness, Buttons
};

struct PaintControl {
    // NOW is drawn in the chips row but is not a control: it only shows the
    // car's paint.
    enum class Kind : uint8_t {
        None, Tab, Swatch, HueTrack, SvPlane, SatTrack, BriTrack, Factory, Previous, Cancel, Respray
    };
    Kind kind = Kind::None;
    int index = 0;  // Tab: 0 PRESETS, 1 CUSTOM. Swatch: preset index. Otherwise 0.
};
constexpr bool operator==(PaintControl a, PaintControl b) {
    return a.kind == b.kind && a.index == b.index;
}
constexpr bool operator!=(PaintControl a, PaintControl b) { return !(a == b); }

// Inclusive at min, exclusive at max.
struct PaintRect {
    glm::vec2 min{0.0f}, max{0.0f};
    bool contains(glm::vec2 p) const {
        return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
    }
    glm::vec2 centre() const { return (min + max) * 0.5f; }
    glm::vec2 size() const { return max - min; }
    PaintRect grown_y(float by) const { return {{min.x, min.y - by}, {max.x, max.y + by}}; }
};

// Authored text sizes at scale 1; draw at size * layout.s.
struct PaintPickerText {
    static constexpr float kTitle = 54.0f, kSubtitle = 18.0f, kStatus = 20.0f, kChip = 20.0f,
                           kTab = 24.0f, kSwatchName = 26.0f, kGroup = 16.0f, kLabel = 18.0f,
                           kReadout = 22.0f, kButton = 26.0f, kHint = 16.0f;
};

inline constexpr const char* kPaintPickerHint =
    "ENTER / A  SELECT    R / X  RESPRAY    F / Y  FACTORY    TAB / LB RB  TABS    ESC / B  CANCEL";

// Authored for the 2560x1440 canvas. The panel hugs the right edge and the car
// is framed in the free region to its left.
inline constexpr float kPaintPanelWidth = 700.0f, kPaintPanelHeight = 1312.0f;
inline constexpr float kPaintPanelMargin = 56.0f;   // right edge
inline constexpr float kPaintFreeGap = 24.0f;       // between free region and panel
inline constexpr float kPaintPressSlop = 10.0f;     // vertical, tracks and plane, press only
inline constexpr float kPaintPanelShare = 0.42f;    // panel plus margin, of the canvas width

struct PaintPickerLayout {
    glm::vec2 canvas{0.0f};
    // 1 at 16:9, 16:10, 4:3 and 5:4; 0.8 at 1:1. Scales every offset and size.
    float s = 1.0f;
    PaintRect panel;
    PaintRect free_region;  // where the bay camera must frame the car

    glm::vec2 title{0.0f};
    glm::vec2 subtitle_right{0.0f};  // "ROOK'S AUTO REPAIR", right-aligned
    PaintRect wanted_strip;
    float stars_right = 0.0f, stars_top = 0.0f;
    glm::vec2 status{0.0f};          // status_line()

    PaintRect chips_row, now_chip, factory_chip, previous_chip;
    std::array<PaintRect, 2> tabs{};

    std::array<PaintRect, 24> swatches{};
    glm::vec2 swatch_name{0.0f}, swatch_group{0.0f};

    glm::vec2 hue_label{0.0f};
    PaintRect hue_track, sv_plane;
    glm::vec2 saturation_label{0.0f};
    PaintRect saturation_track;
    glm::vec2 brightness_label{0.0f};
    PaintRect brightness_track;
    glm::vec2 readout{0.0f};         // "CUSTOM  #RRGGBB"

    PaintRect cancel, respray;
    glm::vec2 hint{0.0f};

    static PaintPickerLayout from_canvas(glm::vec2 canvas_size) {
        PaintPickerLayout l;
        l.canvas = canvas_size;
        const float scale = canvas_size.x > 0.0f
            ? std::min(1.0f, canvas_size.x * kPaintPanelShare / (kPaintPanelWidth + kPaintPanelMargin))
            : 0.0f;
        l.s = scale;
        const glm::vec2 origin{canvas_size.x - (kPaintPanelMargin + kPaintPanelWidth) * scale,
                               (canvas_size.y - kPaintPanelHeight * scale) * 0.5f};
        const auto at = [&](float x0, float y0, float x1, float y1) {
            return PaintRect{origin + glm::vec2{x0, y0} * scale, origin + glm::vec2{x1, y1} * scale};
        };
        const auto pt = [&](float x, float y) { return origin + glm::vec2{x, y} * scale; };

        l.panel = at(0, 0, kPaintPanelWidth, kPaintPanelHeight);
        l.free_region = {{0.0f, 0.0f},
                         {std::max(0.0f, l.panel.min.x - kPaintFreeGap * scale), canvas_size.y}};

        // Content column: x 36..664 (628 wide).
        l.title = pt(36, 24);
        l.subtitle_right = pt(664, 46);
        l.wanted_strip = at(36, 100, 664, 160);
        l.stars_right = origin.x + 166.0f * scale;
        l.stars_top = origin.y + 118.0f * scale;
        l.status = pt(186, 116);

        l.chips_row = at(36, 176, 664, 236);
        l.now_chip = at(36, 176, 232, 236);
        l.factory_chip = at(252, 176, 448, 236);
        l.previous_chip = at(468, 176, 664, 236);
        l.tabs = {at(36, 256, 350, 306), at(350, 256, 664, 306)};

        // 92-unit cells on a 107 pitch: the 15-unit gaps hit nothing.
        for (int r = 0; r < kPaintGridRows; ++r)
            for (int c = 0; c < kPaintGridColumns; ++c) {
                const float x = 36.0f + 107.0f * static_cast<float>(c);
                const float y = 330.0f + 107.0f * static_cast<float>(r);
                l.swatches[static_cast<std::size_t>(r * kPaintGridColumns + c)] = at(x, y, x + 92, y + 92);
            }
        l.swatch_name = pt(36, 763);
        l.swatch_group = pt(36, 800);

        l.hue_label = pt(36, 330);
        l.hue_track = at(36, 356, 664, 400);
        l.sv_plane = at(36, 420, 664, 720);
        l.saturation_label = pt(36, 736);
        l.saturation_track = at(36, 762, 664, 798);
        l.brightness_label = pt(36, 814);
        l.brightness_track = at(36, 840, 664, 876);
        l.readout = pt(36, 896);

        l.cancel = at(36, 1164, 236, 1236);
        l.respray = at(256, 1164, 664, 1236);
        l.hint = pt(36, 1256);
        return l;
    }

    // Right edge of the free region over the canvas width.
    float free_fraction() const {
        return canvas.x > 0.0f ? free_region.max.x / canvas.x : 0.0f;
    }

    PaintRect rect(PaintControl c) const {
        using K = PaintControl::Kind;
        switch (c.kind) {
            case K::Tab: return tabs[static_cast<std::size_t>(std::clamp(c.index, 0, 1))];
            case K::Swatch: return swatches[static_cast<std::size_t>(std::clamp(c.index, 0, 23))];
            case K::HueTrack: return hue_track;
            case K::SvPlane: return sv_plane;
            case K::SatTrack: return saturation_track;
            case K::BriTrack: return brightness_track;
            case K::Factory: return factory_chip;
            case K::Previous: return previous_chip;
            case K::Cancel: return cancel;
            case K::Respray: return respray;
            case K::None: break;
        }
        return {};
    }

    // `press` adds the tracks' and plane's vertical slop, which only a press
    // gets. PREVIOUS exists only while there is one. Outside the panel is never
    // a control: a press there must not cancel.
    PaintControl hit_test(glm::vec2 p, PaintTab tab, bool press, bool has_previous) const {
        using K = PaintControl::Kind;
        if (!panel.contains(p)) return {};
        if (factory_chip.contains(p)) return {K::Factory, 0};
        if (has_previous && previous_chip.contains(p)) return {K::Previous, 0};
        for (int i = 0; i < 2; ++i)
            if (tabs[static_cast<std::size_t>(i)].contains(p)) return {K::Tab, i};
        if (tab == PaintTab::Presets) {
            for (int i = 0; i < 24; ++i)
                if (swatches[static_cast<std::size_t>(i)].contains(p)) return {K::Swatch, i};
        } else {
            const float slop = press ? kPaintPressSlop * s : 0.0f;
            if (hue_track.grown_y(slop).contains(p)) return {K::HueTrack, 0};
            if (sv_plane.grown_y(slop).contains(p)) return {K::SvPlane, 0};
            if (saturation_track.grown_y(slop).contains(p)) return {K::SatTrack, 0};
            if (brightness_track.grown_y(slop).contains(p)) return {K::BriTrack, 0};
        }
        if (cancel.contains(p)) return {K::Cancel, 0};
        if (respray.contains(p)) return {K::Respray, 0};
        return {};
    }

    // Value under a pointer, clamped even outside the rect so a captured drag
    // pins at the ends.
    static float unit_along_x(const PaintRect& r, float x) {
        const float w = r.max.x - r.min.x;
        return w > 0.0f ? std::clamp((x - r.min.x) / w, 0.0f, 1.0f) : 0.0f;
    }
    static float unit_along_y(const PaintRect& r, float y) {
        const float h = r.max.y - r.min.y;
        return h > 0.0f ? std::clamp((y - r.min.y) / h, 0.0f, 1.0f) : 0.0f;
    }
    float hue_at(float x) const { return std::min(unit_along_x(hue_track, x), kPaintHueMax); }
    float saturation_at(float x) const { return unit_along_x(saturation_track, x); }
    float brightness_at(float x) const { return unit_along_x(brightness_track, x); }
    // Saturation along x, value from 1 at the top to 0 at the bottom.
    glm::vec2 plane_at(glm::vec2 p) const {
        return {unit_along_x(sv_plane, p.x), 1.0f - unit_along_y(sv_plane, p.y)};
    }

    // Where the knobs and the hue notch draw; the inverses of the above.
    float hue_notch_x(float h) const { return hue_track.min.x + h * hue_track.size().x; }
    float track_knob_x(const PaintRect& track, float value) const {
        return track.min.x + value * track.size().x;
    }
    glm::vec2 plane_knob(PaintHsv c) const {
        return {sv_plane.min.x + c.s * sv_plane.size().x, sv_plane.min.y + (1.0f - c.v) * sv_plane.size().y};
    }
};

// ---------------------------------------------------------------------------
// Held directions.
// ---------------------------------------------------------------------------
// Hysteresis on the merged UI axis (keys, stick, pad): press beyond 0.5,
// release below 0.35.
inline constexpr float kPaintNavPress = 0.5f, kPaintNavRelease = 0.35f;
// First step on the press, the first repeat 0.35 s later, then every 0.11 s.
inline constexpr float kPaintNavDelay = 0.35f, kPaintNavRepeat = 0.11f;

struct NavRepeat {
    int dir = 0;
    float held = 0.0f;
    float next = 0.0f;
    bool wait_release = false;

    // -1, 0 or +1 this frame. Steps are timed from the press, so 144 Hz and
    // 30 Hz step at the same moments to within a frame. At most one step per
    // call: a hitch never bursts focus across the grid.
    int step(float axis, float dt) {
        const float a = std::isfinite(axis) ? axis : 0.0f;
        const float mag = std::fabs(a);
        if (dir != 0 && (mag < kPaintNavRelease || (a > 0.0f) != (dir > 0))) dir = 0;
        if (wait_release) {
            if (mag >= kPaintNavRelease) return 0;
            wait_release = false;
        }
        if (dir == 0) {
            if (mag <= kPaintNavPress) return 0;
            dir = a > 0.0f ? 1 : -1;
            held = 0.0f;
            next = kPaintNavDelay;
            return dir;
        }
        held += std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
        if (held < next) return 0;
        next += kPaintNavRepeat;
        if (next <= held) next = held + kPaintNavRepeat;
        return dir;
    }

    // Ignore the axis until it releases: focus changed rows under a held stick.
    void hold_until_release() {
        dir = 0;
        wait_release = true;
    }
};

// Sliders: one tap step on the press; after 0.30 s held, a continuous rate that
// ramps from `base` to `max` over the next 0.70 s. A stick scales the rate
// linearly from 0 at the release threshold to 1 at full deflection.
struct PaintSliderRate {
    float tap, base, max;  // units, units per second, units per second
};
inline constexpr PaintSliderRate kPaintHueRate{1.0f / 72.0f, 0.25f, 0.5f};  // 5 deg; 90 -> 180 deg/s
inline constexpr PaintSliderRate kPaintToneRate{0.05f, 0.6f, 1.2f};         // saturation, brightness
inline constexpr float kPaintHoldDelay = 0.30f, kPaintHoldRamp = 0.70f;
// Pad triggers turn the hue in CUSTOM from any focus.
inline constexpr float kPaintTriggerHueRate = 120.0f / 360.0f;  // turns per second at full pull
inline constexpr float kPaintTriggerDeadzone = 0.05f;

// Travel at full deflection between hold times t0 and t1: the closed-form
// integral of the ramp, so the result does not depend on the frame rate.
inline double paint_hold_travel(double t0, double t1, const PaintSliderRate& rate) {
    const auto travelled = [&](double t) {
        const double u = t - static_cast<double>(kPaintHoldDelay);
        if (u <= 0.0) return 0.0;
        const double ramp = kPaintHoldRamp, lo = rate.base, hi = rate.max;
        if (u <= ramp) return lo * u + (hi - lo) * u * u / (2.0 * ramp);
        return lo * ramp + (hi - lo) * ramp * 0.5 + hi * (u - ramp);
    };
    return travelled(t1) - travelled(t0);
}

struct PaintSliderHold {
    int dir = 0;
    float held = 0.0f;
    bool wait_release = false;

    // Signed travel this frame, in the slider's units.
    float step(float axis, float dt, const PaintSliderRate& rate) {
        const float a = std::isfinite(axis) ? axis : 0.0f;
        const float mag = std::fabs(a);
        if (dir != 0 && (mag < kPaintNavRelease || (a > 0.0f) != (dir > 0))) dir = 0;
        if (wait_release) {
            if (mag >= kPaintNavRelease) return 0.0f;
            wait_release = false;
        }
        if (dir == 0) {
            if (mag <= kPaintNavPress) return 0.0f;
            dir = a > 0.0f ? 1 : -1;
            held = 0.0f;
            return static_cast<float>(dir) * rate.tap;
        }
        const float t0 = held;
        held += std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
        const float gain = std::clamp((mag - kPaintNavRelease) / (1.0f - kPaintNavRelease), 0.0f, 1.0f);
        return static_cast<float>(dir) * gain * static_cast<float>(paint_hold_travel(t0, held, rate));
    }

    void hold_until_release() {
        dir = 0;
        wait_release = true;
    }
};

// ---------------------------------------------------------------------------
// The picker.
// ---------------------------------------------------------------------------
struct PaintPickerOpen {
    PaintColor current{};             // the car's paint when it is not factory
    bool current_is_factory = true;
    PaintColor factory{};             // a representative colour of the factory atlas
    std::optional<PaintOrder> previous;  // paint before the last respray this visit
    int wanted_level = 0;
    bool seen = false;                // a cop had eyes on the car as it pulled in
};

enum class PaintAccept : uint8_t { None, SwitchedTab, Selected, Confirmed, Cancelled };

// R / pad X confirm from anywhere, but not in the first 0.15 s: the press that
// opened the booth must not bounce straight into a respray.
inline constexpr float kPaintConfirmDebounce = 0.15f;

class PaintPicker {
public:
    using Kind = PaintControl::Kind;

    void open(const PaintPickerOpen& init) {
        *this = PaintPicker{};
        init_ = init;
        if (init_.previous) init_.previous = normalised_paint_order(*init_.previous);
        open_ = true;
        const int best = paint_contrast_preset(current_colour());
        grid_col_ = best % kPaintGridColumns;
        focus_ = {Kind::Swatch, best};
        new_ = {false, kPaintPresets[static_cast<std::size_t>(best)].rgb};
        hsv_ = rgb8_to_hsv(new_.colour, 0.0f);
        preview_changed_ = true;
    }
    bool is_open() const { return open_; }

    // Once per rendered frame with host UI time and the merged UI axes (+x
    // right, +y down). `hue_trigger` is right trigger minus left, [-1, 1].
    void update(float dt, float axis_x = 0.0f, float axis_y = 0.0f, float hue_trigger = 0.0f) {
        if (!open_) return;
        const float step_dt = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
        open_s_ += step_dt;
        // Recorded first, so a Down and a held Right on the same frame latch.
        last_axis_x_ = std::isfinite(axis_x) ? axis_x : 0.0f;
        if (const int dy = nav_y_.step(axis_y, step_dt)) move_row(dy);
        if (slider(focus_.kind)) {
            if (const float travel = slide_.step(axis_x, step_dt, rate_for(focus_.kind)); travel != 0.0f)
                adjust(focus_.kind, travel);
        } else if (const int dx = nav_x_.step(axis_x, step_dt)) {
            move_across(dx);
        }
        const float trig = std::isfinite(hue_trigger) ? std::clamp(hue_trigger, -1.0f, 1.0f) : 0.0f;
        if (tab_ == PaintTab::Custom && std::fabs(trig) > kPaintTriggerDeadzone)
            adjust(Kind::HueTrack, trig * kPaintTriggerHueRate * step_dt);
    }

    // One discrete step, as a tapped direction would make.
    void navigate(int dx, int dy) {
        if (!open_) return;
        if (dy != 0) move_row(dy > 0 ? 1 : -1);
        if (dx != 0) move_across(dx > 0 ? 1 : -1);
    }

    // Enter, E, Space, pad A: activates the focused control.
    //   Tabs                                  switch tab
    //   swatch, hue/sat/bri, SV plane, RESPRAY confirm (None if refused)
    //   FACTORY, PREVIOUS                     select it and focus RESPRAY; no order
    //   CANCEL                                cancel
    PaintAccept accept() {
        if (!open_) return PaintAccept::None;
        switch (focus_.kind) {
            case Kind::Tab:
                set_tab(tab_ == PaintTab::Presets ? PaintTab::Custom : PaintTab::Presets);
                set_focus({Kind::Tab, tab_index()});
                return PaintAccept::SwitchedTab;
            case Kind::Factory:
                set_new({true, {}});
                set_focus({Kind::Respray, 0});
                return PaintAccept::Selected;
            case Kind::Previous:
                if (!init_.previous) return PaintAccept::None;
                set_new(*init_.previous);
                set_focus({Kind::Respray, 0});
                return PaintAccept::Selected;
            case Kind::Cancel:
                cancel();
                return PaintAccept::Cancelled;
            case Kind::None:
                return PaintAccept::None;
            case Kind::Swatch:
            case Kind::HueTrack:
            case Kind::SvPlane:
            case Kind::SatTrack:
            case Kind::BriTrack:
            case Kind::Respray:
                break;
        }
        return commit() ? PaintAccept::Confirmed : PaintAccept::None;
    }

    // R / pad X: RESPRAY from any focus, after the debounce.
    std::optional<PaintOrder> confirm_shortcut() {
        if (!open_ || open_s_ < kPaintConfirmDebounce) return std::nullopt;
        return commit();
    }

    // The RESPRAY button. Refused (nullopt, still open) when not wanted and the
    // new paint is the paint the car already has. When wanted, the same paint
    // is still a respray, and still runs the wanted rule. Every successful
    // confirmation closes the picker and leaves the order for take_order().
    std::optional<PaintOrder> confirm() { return commit(); }

    // Esc, Backspace, pad B, or CANCEL.
    void cancel() {
        if (!open_) return;
        open_ = false;
        drag_ = Kind::None;
        cancelled_ = true;
    }

    // Tab key or LB / RB, from anywhere. CUSTOM focuses HUE. PRESETS focuses
    // the swatch that matches the new paint, or the tabs row when none does:
    // landing on a swatch would overwrite a colour dialled in by hand.
    void toggle_tab() {
        if (!open_) return;
        set_tab(tab_ == PaintTab::Presets ? PaintTab::Custom : PaintTab::Presets);
        if (tab_ == PaintTab::Custom) {
            set_focus({Kind::HueTrack, 0});
            return;
        }
        const int i = new_.factory ? -1 : find_paint_preset(new_.colour);
        if (i >= 0) {
            grid_col_ = i % kPaintGridColumns;
            set_focus({Kind::Swatch, i});
        } else {
            set_focus({Kind::Tab, tab_index()});
        }
    }

    // F / pad Y: the one-press reset to factory paint, from anywhere.
    void select_factory() {
        if (!open_) return;
        set_new({true, {}});
        set_focus({Kind::Factory, 0});
    }

    // Pointer, in canvas units. Swatches, chips, tabs and buttons act on the
    // press; a press on a track or the plane captures a drag. Outside the panel
    // does nothing.
    PaintAccept pointer_press(glm::vec2 p, const PaintPickerLayout& layout) {
        if (!open_) return PaintAccept::None;
        const PaintControl hit = layout.hit_test(p, tab_, true, has_previous());
        switch (hit.kind) {
            case Kind::None:
                return PaintAccept::None;
            case Kind::Tab: {
                const PaintTab t = hit.index == 0 ? PaintTab::Presets : PaintTab::Custom;
                const bool changed = t != tab_;
                set_tab(t);
                set_focus({Kind::Tab, tab_index()});
                return changed ? PaintAccept::SwitchedTab : PaintAccept::None;
            }
            case Kind::Swatch:
                grid_col_ = hit.index % kPaintGridColumns;
                set_focus(hit);
                select_preset(hit.index);
                return PaintAccept::Selected;
            case Kind::Factory:
                set_new({true, {}});
                set_focus(hit);
                return PaintAccept::Selected;
            case Kind::Previous:
                if (!init_.previous) return PaintAccept::None;
                set_new(*init_.previous);
                set_focus(hit);
                return PaintAccept::Selected;
            case Kind::Cancel:
                set_focus(hit);
                cancel();
                return PaintAccept::Cancelled;
            case Kind::Respray:
                set_focus(hit);
                return commit() ? PaintAccept::Confirmed : PaintAccept::None;
            case Kind::HueTrack:
            case Kind::SvPlane:
            case Kind::SatTrack:
            case Kind::BriTrack:
                set_focus(hit);
                drag_ = hit.kind;
                drag_to(p, layout);
                return PaintAccept::Selected;
        }
        return PaintAccept::None;
    }

    // Hover for the outline, and the captured drag, which follows the pointer
    // even outside its rect.
    void pointer_move(glm::vec2 p, const PaintPickerLayout& layout) {
        if (!open_) return;
        hover_ = layout.hit_test(p, tab_, false, has_previous());
        if (drag_ != Kind::None) drag_to(p, layout);
    }
    void pointer_release() { drag_ = Kind::None; }
    // The window lost focus: a release may never arrive.
    void focus_lost() {
        drag_ = Kind::None;
        hover_ = {};
    }

    // One tap step per notch over a track; positive notches increase.
    void wheel(glm::vec2 p, int notches, const PaintPickerLayout& layout) {
        if (!open_ || notches == 0) return;
        const PaintControl hit = layout.hit_test(p, tab_, false, has_previous());
        if (hit.kind == Kind::HueTrack || hit.kind == Kind::SatTrack || hit.kind == Kind::BriTrack)
            adjust(hit.kind, static_cast<float>(notches) * rate_for(hit.kind).tap);
    }

    // The order of the last successful confirmation, once.
    std::optional<PaintOrder> take_order() {
        std::optional<PaintOrder> out = order_;
        order_.reset();
        return out;
    }
    // True once after a cancel.
    bool take_cancel() {
        const bool out = cancelled_;
        cancelled_ = false;
        return out;
    }

    // The paint the car should wear while the booth is open.
    PaintOrder preview() const { return new_; }
    // True once after the preview changes; read it once per rendered frame and
    // composite only then.
    bool preview_changed() {
        const bool out = preview_changed_;
        preview_changed_ = false;
        return out;
    }

    PaintOrder current() const {
        return init_.current_is_factory ? PaintOrder{true, {}} : PaintOrder{false, init_.current};
    }
    bool already_this_colour() const { return init_.wanted_level <= 0 && new_ == current(); }
    bool confirm_enabled() const { return !already_this_colour(); }

    // What the RESPRAY button says.
    const char* confirm_label() const {
        if (already_this_colour()) return "ALREADY THIS COLOUR";
        if (init_.wanted_level <= 0) return "RESPRAY";
        return init_.seen ? "RESPRAY - STARS STAY" : "RESPRAY + LOSE THE COPS";
    }
    // The line beside the stars.
    const char* status_line() const {
        if (init_.wanted_level <= 0) return "NOT WANTED - PICK ANY COLOUR";
        return init_.seen ? "LEAVE THE BAY, LOSE THEM, PULL IN AGAIN"
                          : "NO COP SAW YOU PULL IN - A RESPRAY LOSES THEM";
    }

    PaintTab tab() const { return tab_; }
    PaintControl focus() const { return focus_; }
    PaintControl hover() const { return hover_; }
    // The SV plane navigates as the SATURATION row, whose value its x sets.
    PaintRow row() const { return row_of(focus_); }
    int column() const { return grid_col_; }
    bool dragging() const { return drag_ != Kind::None; }
    PaintHsv hsv() const { return hsv_; }
    bool has_previous() const { return init_.previous.has_value(); }
    std::optional<PaintOrder> previous() const { return init_.previous; }
    PaintColor factory_colour() const { return init_.factory; }
    int wanted_level() const { return init_.wanted_level; }
    bool seen() const { return init_.seen; }
    float open_seconds() const { return open_s_; }

private:
    static bool slider(Kind k) {
        return k == Kind::HueTrack || k == Kind::SvPlane || k == Kind::SatTrack || k == Kind::BriTrack;
    }
    static const PaintSliderRate& rate_for(Kind k) {
        return k == Kind::HueTrack ? kPaintHueRate : kPaintToneRate;
    }
    static PaintRow row_of(PaintControl c) {
        switch (c.kind) {
            case Kind::Factory:
            case Kind::Previous: return PaintRow::Chips;
            case Kind::Swatch:
                return static_cast<PaintRow>(static_cast<int>(PaintRow::Grid0) +
                                             std::clamp(c.index / kPaintGridColumns, 0, kPaintGridRows - 1));
            case Kind::HueTrack: return PaintRow::Hue;
            case Kind::SvPlane:
            case Kind::SatTrack: return PaintRow::Saturation;
            case Kind::BriTrack: return PaintRow::Brightness;
            case Kind::Cancel:
            case Kind::Respray: return PaintRow::Buttons;
            case Kind::Tab:
            case Kind::None: break;
        }
        return PaintRow::Tabs;
    }

    int tab_index() const { return tab_ == PaintTab::Presets ? 0 : 1; }
    PaintColor current_colour() const { return init_.current_is_factory ? init_.factory : init_.current; }

    void set_focus(PaintControl c) {
        const PaintRow before = row_of(focus_);
        focus_ = c;
        // A held left/right must not carry into a different row's meaning. Armed
        // only while x is held, so a host that skips idle frames loses nothing.
        if (row_of(c) != before && std::fabs(last_axis_x_) >= kPaintNavRelease) {
            nav_x_.hold_until_release();
            slide_.hold_until_release();
        }
    }

    void set_new(PaintOrder o) {
        const PaintOrder n = normalised_paint_order(o);
        if (n == new_) return;
        new_ = n;
        preview_changed_ = true;
    }
    void select_preset(int i) {
        set_new({false, kPaintPresets[static_cast<std::size_t>(std::clamp(i, 0, 23))].rgb});
    }

    // Entering CUSTOM seeds HSV from the new paint, keeping the hue for greys.
    void set_tab(PaintTab t) {
        if (t == tab_) return;
        tab_ = t;
        drag_ = Kind::None;
        if (t == PaintTab::Custom) hsv_ = rgb8_to_hsv(new_.factory ? init_.factory : new_.colour, hsv_.h);
    }

    void adjust(Kind k, float amount) {
        PaintHsv next = hsv_;
        if (k == Kind::HueTrack)
            next.h = std::clamp(next.h + amount, 0.0f, kPaintHueMax);
        else if (k == Kind::BriTrack)
            next.v = std::clamp(next.v + amount, 0.0f, 1.0f);
        else
            next.s = std::clamp(next.s + amount, 0.0f, 1.0f);
        if (next.h == hsv_.h && next.s == hsv_.s && next.v == hsv_.v) return;
        hsv_ = next;
        set_new({false, hsv_to_rgb8(hsv_)});
    }

    void drag_to(glm::vec2 p, const PaintPickerLayout& layout) {
        switch (drag_) {
            case Kind::HueTrack: hsv_.h = layout.hue_at(p.x); break;
            case Kind::SatTrack: hsv_.s = layout.saturation_at(p.x); break;
            case Kind::BriTrack: hsv_.v = layout.brightness_at(p.x); break;
            case Kind::SvPlane: {
                const glm::vec2 sv = layout.plane_at(p);
                hsv_.s = sv.x;
                hsv_.v = sv.y;
                break;
            }
            default: return;
        }
        set_new({false, hsv_to_rgb8(hsv_)});
    }

    static int visual_rows(PaintTab t, std::array<PaintRow, 7>& rows) {
        if (t == PaintTab::Presets) {
            rows = {PaintRow::Chips, PaintRow::Tabs, PaintRow::Grid0, PaintRow::Grid1,
                    PaintRow::Grid2, PaintRow::Grid3, PaintRow::Buttons};
            return 7;
        }
        rows = {PaintRow::Chips, PaintRow::Tabs, PaintRow::Hue, PaintRow::Saturation,
                PaintRow::Brightness, PaintRow::Buttons, PaintRow::Buttons};
        return 6;
    }

    // Up / Down: between rows in drawn order, clamped, no wrap.
    void move_row(int d) {
        if (focus_.kind == Kind::SvPlane) {
            enter_row(d < 0 ? PaintRow::Hue : PaintRow::Saturation);
            return;
        }
        std::array<PaintRow, 7> rows{};
        const int n = visual_rows(tab_, rows);
        int at = 1;  // Tabs, if focus is somehow off the path
        for (int i = 0; i < n; ++i)
            if (rows[static_cast<std::size_t>(i)] == row()) at = i;
        const int to = std::clamp(at + d, 0, n - 1);
        if (to != at) enter_row(rows[static_cast<std::size_t>(to)]);
    }

    // Landing rules: chips land on FACTORY, tabs on the active tab, a grid row
    // keeps the column and selects that swatch, buttons land on RESPRAY.
    void enter_row(PaintRow r) {
        switch (r) {
            case PaintRow::Chips: set_focus({Kind::Factory, 0}); return;
            case PaintRow::Tabs: set_focus({Kind::Tab, tab_index()}); return;
            case PaintRow::Grid0:
            case PaintRow::Grid1:
            case PaintRow::Grid2:
            case PaintRow::Grid3: {
                const int i = (static_cast<int>(r) - static_cast<int>(PaintRow::Grid0)) * kPaintGridColumns + grid_col_;
                set_focus({Kind::Swatch, i});
                select_preset(i);
                return;
            }
            case PaintRow::Hue: set_focus({Kind::HueTrack, 0}); return;
            case PaintRow::Saturation: set_focus({Kind::SatTrack, 0}); return;
            case PaintRow::Brightness: set_focus({Kind::BriTrack, 0}); return;
            case PaintRow::Buttons: set_focus({Kind::Respray, 0}); return;
        }
    }

    // Left / Right: move within the row, switch the tab, or tap a slider.
    void move_across(int d) {
        switch (focus_.kind) {
            case Kind::Factory:
            case Kind::Previous:
                set_focus({d > 0 && has_previous() ? Kind::Previous : Kind::Factory, 0});
                return;
            case Kind::Tab:
                set_tab(d < 0 ? PaintTab::Presets : PaintTab::Custom);
                set_focus({Kind::Tab, tab_index()});
                return;
            case Kind::Swatch: {
                const int col = std::clamp(grid_col_ + d, 0, kPaintGridColumns - 1);
                if (col == grid_col_) return;
                grid_col_ = col;
                const int i = (focus_.index / kPaintGridColumns) * kPaintGridColumns + col;
                set_focus({Kind::Swatch, i});
                select_preset(i);
                return;
            }
            case Kind::HueTrack:
            case Kind::SvPlane:
            case Kind::SatTrack:
            case Kind::BriTrack:
                adjust(focus_.kind, static_cast<float>(d) * rate_for(focus_.kind).tap);
                return;
            case Kind::Cancel:
            case Kind::Respray:
                set_focus({d < 0 ? Kind::Cancel : Kind::Respray, 0});
                return;
            case Kind::None:
                return;
        }
    }

    std::optional<PaintOrder> commit() {
        if (!open_ || already_this_colour()) return std::nullopt;
        open_ = false;
        drag_ = Kind::None;
        order_ = new_;
        return new_;
    }

    PaintPickerOpen init_{};
    bool open_ = false;
    PaintTab tab_ = PaintTab::Presets;
    PaintControl focus_{};
    PaintControl hover_{};
    int grid_col_ = 0;
    PaintOrder new_{};
    PaintHsv hsv_{};
    Kind drag_ = Kind::None;
    float open_s_ = 0.0f;
    float last_axis_x_ = 0.0f;
    NavRepeat nav_x_{}, nav_y_{};
    PaintSliderHold slide_{};
    bool preview_changed_ = false;
    bool cancelled_ = false;
    std::optional<PaintOrder> order_;
};

}  // namespace apricot
