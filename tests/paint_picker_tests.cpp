// The respray booth's picker model (game/paint_picker.h), headless: presets,
// layout and hit-testing through the real UiCanvas mapping, focus, held
// repeat, the Accept table and the confirm rules.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "game/paint_picker.h"
#include "game/ui_canvas.h"
#include "game/vehicle_paint.h"
#include "test_assert.h"

using namespace apricot;

namespace {

using Kind = PaintControl::Kind;

constexpr PaintColor kSignalRed{0xB5, 0x12, 0x1B};
constexpr PaintColor kFactoryBlue{0x30, 0x48, 0x70};

const PaintPreset& preset(int i) { return kPaintPresets[static_cast<std::size_t>(i)]; }
const PaintRect& swatch(const PaintPickerLayout& l, int i) { return l.swatches[static_cast<std::size_t>(i)]; }

PaintPickerLayout layout_16_9() {
    return PaintPickerLayout::from_canvas(UiCanvas::from_drawable({1920, 1080}).size);
}

// A car painted SIGNAL RED with a blue factory atlas.
PaintPickerOpen booth(int wanted = 0, bool seen = false) {
    PaintPickerOpen o;
    o.current = kSignalRed;
    o.current_is_factory = false;
    o.factory = kFactoryBlue;
    o.wanted_level = wanted;
    o.seen = seen;
    return o;
}

std::vector<PaintControl> controls(PaintTab tab, bool has_previous) {
    std::vector<PaintControl> out{{Kind::Factory, 0}, {Kind::Tab, 0}, {Kind::Tab, 1},
                                  {Kind::Cancel, 0}, {Kind::Respray, 0}};
    if (has_previous) out.push_back({Kind::Previous, 0});
    if (tab == PaintTab::Presets) {
        for (int i = 0; i < 24; ++i) out.push_back({Kind::Swatch, i});
    } else {
        out.push_back({Kind::HueTrack, 0});
        out.push_back({Kind::SvPlane, 0});
        out.push_back({Kind::SatTrack, 0});
        out.push_back({Kind::BriTrack, 0});
    }
    return out;
}

bool overlaps(const PaintRect& a, const PaintRect& b) {
    return a.min.x < b.max.x && b.min.x < a.max.x && a.min.y < b.max.y && b.min.y < a.max.y;
}
bool within(const PaintRect& inner, const PaintRect& outer) {
    return inner.min.x >= outer.min.x && inner.min.y >= outer.min.y &&
           inner.max.x <= outer.max.x && inner.max.y <= outer.max.y;
}

void up_to_chips(PaintPicker& p) {
    for (int i = 0; i < 10; ++i) p.navigate(0, -1);
}
void down_to_buttons(PaintPicker& p) {
    for (int i = 0; i < 10; ++i) p.navigate(0, 1);
}

// Independent of paint_redmean_distance_sq, so the test does not grade its own
// homework.
int farthest_preset(PaintColor c) {
    int best = 0;
    double best_d = -1;
    for (int i = 0; i < 24; ++i) {
        const PaintColor q = preset(i).rgb;
        const double rmean = 0.5 * (static_cast<double>(c.r) + q.r);
        const double dr = static_cast<double>(c.r) - q.r, dg = static_cast<double>(c.g) - q.g,
                     db = static_cast<double>(c.b) - q.b;
        const double d = (2 + rmean / 256) * dr * dr + 4 * dg * dg + (2 + (255 - rmean) / 256) * db * db;
        if (d > best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

void presets_are_named_distinct_and_round_trip_through_hsv() {
    REQUIRE(kPaintPresets.size() == 24);
    for (int i = 0; i < 24; ++i) {
        const PaintPreset& p = preset(i);
        REQUIRE(p.name != nullptr && p.group != nullptr);
        REQUIRE_MSG(std::strlen(p.name) <= 14, "preset name is longer than the chip holds", p.name);
        for (int j = i + 1; j < 24; ++j) {
            REQUIRE_MSG(std::strcmp(p.name, preset(j).name) != 0, "duplicate preset name", p.name);
            REQUIRE_MSG(p.rgb != preset(j).rgb, "duplicate preset colour", p.name);
        }
        REQUIRE_MSG(hsv_to_rgb8(rgb8_to_hsv(p.rgb, 0.0f)) == p.rgb,
                    "preset does not survive rgb -> hsv -> rgb", p.name);
        REQUIRE(find_paint_preset(p.rgb) == i);
        REQUIRE(std::strcmp(paint_order_name(PaintOrder{false, p.rgb}), p.name) == 0);
        REQUIRE_MSG(std::strcmp(p.name, "CREAM") != 0 && std::strcmp(p.name, "DESERT TAN") != 0,
                    "a replaced preset is back", p.name);
    }
    // The replacements sit in the slots DESERT TAN and CREAM had.
    REQUIRE(std::strcmp(preset(18).name, "ULTRA VIOLET") == 0);
    REQUIRE((preset(18).rgb == PaintColor{0x8A, 0x3F, 0xD1}));
    REQUIRE(std::strcmp(preset(23).name, "COBALT BLUE") == 0);
    REQUIRE((preset(23).rgb == PaintColor{0x23, 0x56, 0xC9}));

    REQUIRE(std::strcmp(paint_order_name(PaintOrder{true, {1, 2, 3}}), "FACTORY PAINT") == 0);
    REQUIRE(std::strcmp(paint_order_name(PaintOrder{false, {1, 2, 3}}), "CUSTOM") == 0);
    REQUIRE(find_paint_preset(PaintColor{1, 2, 3}) == -1);

    // The conversion's fixed points.
    REQUIRE((hsv_to_rgb8({0.0f, 1.0f, 1.0f}) == PaintColor{255, 0, 0}));
    REQUIRE((hsv_to_rgb8({1.0f / 3.0f, 1.0f, 1.0f}) == PaintColor{0, 255, 0}));
    REQUIRE((hsv_to_rgb8({2.0f / 3.0f, 1.0f, 1.0f}) == PaintColor{0, 0, 255}));
    REQUIRE((hsv_to_rgb8({1.0f, 1.0f, 1.0f}) == PaintColor{255, 0, 0}));
    REQUIRE((hsv_to_rgb8({0.4f, 0.0f, 1.0f}) == PaintColor{255, 255, 255}));
    REQUIRE_NEAR(rgb8_to_hsv({0, 0, 255}, 0.0f).h, 2.0 / 3.0, 1e-6);
    // Greys have no hue of their own and keep the one they are given.
    REQUIRE(rgb8_to_hsv({128, 128, 128}, 0.4f).h == 0.4f);
    REQUIRE(rgb8_to_hsv({0, 0, 0}, 0.7f).h == 0.7f);
    apricot_test::pass("24 named presets, two replacements in place, exact rgb/hsv round trip");
}

void presets_stay_apart_in_oklab() {
    double closest = 1e9;
    int ci = 0, cj = 0;
    for (int i = 0; i < 24; ++i)
        for (int j = i + 1; j < 24; ++j) {
            const PaintLab a = paint_srgb8_to_oklab(preset(i).rgb);
            const PaintLab b = paint_srgb8_to_oklab(preset(j).rgb);
            const double d = std::sqrt((a.L - b.L) * (a.L - b.L) + (a.a - b.a) * (a.a - b.a) +
                                       (a.b - b.b) * (a.b - b.b));
            if (d < closest) {
                closest = d;
                ci = i;
                cj = j;
            }
        }
    std::printf("  closest presets: %s / %s at %.4f OKLab\n", preset(ci).name, preset(cj).name, closest);
    REQUIRE_MSG(closest >= 0.06, "two presets are too close to tell apart on a car", preset(ci).name);
    apricot_test::pass("every preset pair is at least 0.06 apart in OKLab");
}

void layout_leaves_the_car_clear_at_every_aspect() {
    for (const glm::vec2 drawable : {glm::vec2{1280, 720}, glm::vec2{1600, 1000}, glm::vec2{1280, 1000},
                                     glm::vec2{3440, 1440}, glm::vec2{1440, 1440}}) {
        char tag[48];
        std::snprintf(tag, sizeof tag, "%.0fx%.0f", static_cast<double>(drawable.x),
                      static_cast<double>(drawable.y));
        const glm::vec2 vp = UiCanvas::from_drawable(drawable).size;
        const PaintPickerLayout l = PaintPickerLayout::from_canvas(vp);
        const PaintRect canvas{{0.0f, 0.0f}, vp};
        REQUIRE_MSG(l.s > 0.0f && l.s <= 1.0f, "scale out of range", tag);
        REQUIRE_MSG(within(l.panel, canvas), "panel leaves the canvas", tag);
        REQUIRE_MSG(l.free_region.min.x == 0.0f && l.free_region.max.y == vp.y, "free region is not the left side", tag);
        REQUIRE_MSG(l.free_region.max.x + kPaintFreeGap * l.s <= l.panel.min.x + 1e-3f,
                    "the car's free region runs under the panel", tag);
        // respray_camera_tests frames the car into a 0.46 free fraction at 1:1.
        REQUIRE_MSG(l.free_fraction() >= 0.46f, "free region narrower than the bay camera frames for", tag);
        for (const PaintTab tab : {PaintTab::Presets, PaintTab::Custom}) {
            const auto list = controls(tab, true);
            for (std::size_t i = 0; i < list.size(); ++i) {
                const PaintRect r = l.rect(list[i]);
                REQUIRE_MSG(r.size().x > 0.0f && r.size().y > 0.0f, "empty control", tag);
                REQUIRE_MSG(within(r, l.panel), "control outside the panel", tag);
                REQUIRE_MSG(!overlaps(r, l.free_region), "control over the car", tag);
                for (std::size_t j = i + 1; j < list.size(); ++j)
                    REQUIRE_MSG(!overlaps(r, l.rect(list[j])), "two controls overlap", tag);
            }
        }
        REQUIRE_MSG(within(l.now_chip, l.chips_row) && within(l.factory_chip, l.chips_row) &&
                        within(l.previous_chip, l.chips_row) && !overlaps(l.now_chip, l.factory_chip),
                    "chips row", tag);
        for (const glm::vec2 anchor : {l.title, l.subtitle_right, l.status, l.swatch_name, l.swatch_group,
                                       l.hue_label, l.saturation_label, l.brightness_label, l.readout, l.hint})
            REQUIRE_MSG(l.panel.contains(anchor), "text anchor outside the panel", tag);
        // The hint line, at its drawn height, ends inside the panel.
        REQUIRE_MSG(l.hint.y + PaintPickerText::kHint * 1.21f * l.s < l.panel.max.y, "hint line clipped", tag);
        // Tab content starts below the tabs and the chips sit above them.
        REQUIRE(l.chips_row.max.y <= l.tabs[0].min.y);
        REQUIRE(l.tabs[0].max.y <= swatch(l, 0).min.y && l.tabs[0].max.y <= l.hue_track.min.y);
        REQUIRE(swatch(l, 23).max.y <= l.swatch_name.y && l.readout.y < l.cancel.min.y);
    }

    // 16:9 at full scale, authored numbers exactly.
    const PaintPickerLayout wide = layout_16_9();
    REQUIRE_NEAR(wide.s, 1.0, 1e-6);
    REQUIRE_NEAR(wide.panel.min.x, 2560.0 - 756.0, 1e-3);
    REQUIRE_NEAR(wide.panel.min.y, 64.0, 1e-3);
    REQUIRE_NEAR(wide.panel.max.y, 1376.0, 1e-3);
    REQUIRE_NEAR(wide.free_fraction(), (2560.0 - 780.0) / 2560.0, 1e-5);
    REQUIRE_NEAR(swatch(wide, 7).min.x, wide.panel.min.x + 36.0 + 107.0, 1e-3);
    // 1:1 shrinks to 0.8, centred vertically, and keeps 57% of the width for the car.
    const PaintPickerLayout square = PaintPickerLayout::from_canvas(UiCanvas::from_drawable({1440, 1440}).size);
    REQUIRE_NEAR(square.s, 0.8, 1e-5);
    REQUIRE_NEAR(square.free_fraction(), (1440.0 - 780.0 * 0.8) / 1440.0, 1e-4);
    REQUIRE_NEAR(square.panel.min.y, 1440.0 - square.panel.max.y, 1e-3);
    REQUIRE_NEAR(square.panel.max.x, 1440.0 - 56.0 * 0.8, 1e-3);
    // No drawable yet: an empty layout that hits nothing.
    const PaintPickerLayout none = PaintPickerLayout::from_canvas(UiCanvas::from_drawable({0, 0}).size);
    REQUIRE(none.hit_test({0.0f, 0.0f}, PaintTab::Presets, true, true).kind == Kind::None);
    REQUIRE(none.free_fraction() == 0.0f);
    apricot_test::pass("panel stays right of the car's free region at 16:9, 16:10, 21:9, 1.28 and 1:1");
}

void every_control_is_hit_testable_at_1x_and_2x_dpi() {
    int checked = 0;
    for (const glm::vec2 drawable : {glm::vec2{2560, 1440}, glm::vec2{1280, 720}, glm::vec2{1440, 1440}}) {
        for (const float dpi : {1.0f, 2.0f}) {
            char tag[64];
            std::snprintf(tag, sizeof tag, "%.0fx%.0f at %.0fx", static_cast<double>(drawable.x),
                          static_cast<double>(drawable.y), static_cast<double>(dpi));
            const glm::vec2 window = drawable / dpi;  // the logical size pointer events use
            const UiCanvas canvas = UiCanvas::from_drawable(drawable);
            const PaintPickerLayout l = PaintPickerLayout::from_canvas(canvas.size);
            for (const PaintTab tab : {PaintTab::Presets, PaintTab::Custom}) {
                for (const PaintControl c : controls(tab, true)) {
                    const PaintRect r = l.rect(c);
                    for (const glm::vec2 f : {glm::vec2{0.5f, 0.5f}, glm::vec2{0.25f, 0.25f}, glm::vec2{0.75f, 0.25f},
                                              glm::vec2{0.25f, 0.75f}, glm::vec2{0.75f, 0.75f}}) {
                        const glm::vec2 target = r.min + r.size() * f;
                        // The pointer arrives in whole logical points.
                        const glm::vec2 event = glm::floor(target / canvas.size * window);
                        const glm::vec2 p = canvas.from_window(event, window);
                        REQUIRE_MSG(l.hit_test(p, tab, true, true) == c, "a press on a control does not pick it", tag);
                        REQUIRE_MSG(l.hit_test(p, tab, false, true) == c, "hover disagrees with the press", tag);
                        ++checked;
                    }
                }
                // The car's region is never a control.
                for (int ix = 0; ix < 8; ++ix)
                    for (int iy = 0; iy < 8; ++iy) {
                        const glm::vec2 p{l.free_region.max.x * (static_cast<float>(ix) + 0.5f) / 8.0f,
                                          canvas.size.y * (static_cast<float>(iy) + 0.5f) / 8.0f};
                        REQUIRE_MSG(l.hit_test(p, tab, true, true).kind == Kind::None, "a press over the car hit a control", tag);
                    }
            }
            // The gaps between swatches hit nothing; PREVIOUS hides when empty;
            // the NOW chip only shows paint.
            const glm::vec2 gap{swatch(l, 0).max.x + 7.5f * l.s, swatch(l, 0).centre().y};
            REQUIRE_MSG(l.hit_test(gap, PaintTab::Presets, true, true).kind == Kind::None, "swatch gap", tag);
            REQUIRE_MSG(l.hit_test(l.previous_chip.centre(), PaintTab::Presets, true, false).kind == Kind::None,
                        "empty PREVIOUS is still hittable", tag);
            REQUIRE_MSG(l.hit_test(l.now_chip.centre(), PaintTab::Custom, true, true).kind == Kind::None, "NOW chip", tag);
            // Tracks take a press a little above them; hover does not.
            const glm::vec2 above{l.hue_track.centre().x, l.hue_track.min.y - 5.0f * l.s};
            REQUIRE_MSG(l.hit_test(above, PaintTab::Custom, true, true).kind == Kind::HueTrack, "press slop", tag);
            REQUIRE_MSG(l.hit_test(above, PaintTab::Custom, false, true).kind == Kind::None, "hover slop", tag);
            // Tab content only answers on its own tab.
            REQUIRE(l.hit_test(swatch(l, 0).centre(), PaintTab::Custom, true, true).kind != Kind::Swatch);
            REQUIRE(l.hit_test(l.sv_plane.centre(), PaintTab::Presets, true, true).kind != Kind::SvPlane);
        }
    }
    std::printf("  %d pointer positions mapped through UiCanvas::from_window\n", checked);
    apricot_test::pass("every control is hit-testable at 1x and 2x DPI, and the car's region never is");
}

void plane_corners_and_drag_clamp() {
    const PaintPickerLayout l = layout_16_9();
    PaintPicker p;
    p.open(booth());
    p.toggle_tab();
    REQUIRE(p.tab() == PaintTab::Custom);

    // Top-left: no saturation, full value.
    REQUIRE(p.pointer_press(l.sv_plane.min, l) == PaintAccept::Selected);
    REQUIRE(p.dragging());
    REQUIRE(p.focus().kind == Kind::SvPlane);
    REQUIRE(p.hsv().s == 0.0f && p.hsv().v == 1.0f);
    REQUIRE((p.preview() == PaintOrder{false, {255, 255, 255}}));
    // Far past bottom-right: pinned, not wrapped.
    p.pointer_move(l.sv_plane.max + glm::vec2{300.0f, 300.0f}, l);
    REQUIRE(p.hsv().s == 1.0f && p.hsv().v == 0.0f);
    REQUIRE((p.preview().colour == PaintColor{0, 0, 0}));
    // Just inside bottom-right.
    p.pointer_move(l.sv_plane.max - glm::vec2{0.01f}, l);
    REQUIRE_NEAR(p.hsv().s, 1.0, 1e-3);
    REQUIRE_NEAR(p.hsv().v, 0.0, 1e-3);
    // The knob draws where the value reads.
    const glm::vec2 mid = l.sv_plane.centre();
    p.pointer_move(mid, l);
    REQUIRE_NEAR(p.hsv().s, 0.5, 1e-4);
    REQUIRE_NEAR(p.hsv().v, 0.5, 1e-4);
    REQUIRE_NEAR(l.plane_knob(p.hsv()).x, mid.x, 1e-2);
    REQUIRE_NEAR(l.plane_knob(p.hsv()).y, mid.y, 1e-2);
    // Release ends the capture.
    p.pointer_release();
    REQUIRE(!p.dragging());
    const PaintHsv held = p.hsv();
    p.pointer_move(l.sv_plane.min, l);
    REQUIRE(p.hsv().s == held.s && p.hsv().v == held.v);
    REQUIRE(p.hover().kind == Kind::SvPlane);
    // A press just above the plane still catches it.
    REQUIRE(p.pointer_press({mid.x, l.sv_plane.min.y - 5.0f}, l) == PaintAccept::Selected);
    REQUIRE(p.hsv().v == 1.0f);
    // Losing window focus ends a drag whose release never arrives.
    p.focus_lost();
    REQUIRE(!p.dragging());
    REQUIRE(p.hover().kind == Kind::None);

    // The hue bar pins at both ends and never reaches 1.
    REQUIRE(p.pointer_press(l.hue_track.centre(), l) == PaintAccept::Selected);
    REQUIRE(p.focus().kind == Kind::HueTrack);
    REQUIRE_NEAR(p.hsv().h, 0.5, 1e-4);
    REQUIRE_NEAR(l.hue_notch_x(p.hsv().h), l.hue_track.centre().x, 1e-2);
    p.pointer_move({l.hue_track.min.x - 500.0f, 0.0f}, l);
    REQUIRE(p.hsv().h == 0.0f);
    p.pointer_move({l.hue_track.max.x + 500.0f, 0.0f}, l);
    REQUIRE(p.hsv().h == kPaintHueMax);
    p.pointer_release();

    // Saturation and brightness tracks.
    const float sat_y = l.saturation_track.centre().y, bri_y = l.brightness_track.centre().y;
    p.pointer_press({l.saturation_track.min.x, sat_y}, l);
    REQUIRE(p.hsv().s == 0.0f && p.focus().kind == Kind::SatTrack);
    p.pointer_move({l.saturation_track.max.x + 40.0f, sat_y + 400.0f}, l);
    REQUIRE(p.hsv().s == 1.0f);
    p.pointer_release();
    p.pointer_press({l.brightness_track.max.x - 0.001f, bri_y}, l);
    REQUIRE_NEAR(p.hsv().v, 1.0, 1e-4);
    REQUIRE_NEAR(l.track_knob_x(l.brightness_track, p.hsv().v), l.brightness_track.max.x, 1e-2);
    p.pointer_release();

    // The wheel taps a track and ignores the plane.
    const float v0 = p.hsv().v;
    p.wheel(l.brightness_track.centre(), -2, l);
    REQUIRE_NEAR(p.hsv().v, v0 - 2 * kPaintToneRate.tap, 1e-5);
    const PaintHsv before_wheel = p.hsv();
    p.wheel(mid, 3, l);
    REQUIRE(p.hsv().s == before_wheel.s && p.hsv().v == before_wheel.v);

    // A press outside the panel does nothing, and never cancels.
    REQUIRE(p.pointer_press({100.0f, 700.0f}, l) == PaintAccept::None);
    REQUIRE(p.is_open() && !p.take_cancel());
    apricot_test::pass("SV plane corners, hue and slider ends pin, drags end on release and focus loss");
}

void pointer_presses_act_on_press() {
    const PaintPickerLayout l = layout_16_9();
    PaintPicker p;
    p.open(booth());
    REQUIRE(p.pointer_press(l.tabs[1].centre(), l) == PaintAccept::SwitchedTab);
    REQUIRE(p.tab() == PaintTab::Custom && p.focus() == (PaintControl{Kind::Tab, 1}));
    REQUIRE(p.pointer_press(l.tabs[1].centre(), l) == PaintAccept::None);
    REQUIRE(p.pointer_press(l.tabs[0].centre(), l) == PaintAccept::SwitchedTab);
    REQUIRE(p.pointer_press(swatch(l, 15).centre(), l) == PaintAccept::Selected);
    REQUIRE((p.preview() == PaintOrder{false, preset(15).rgb}));
    REQUIRE(p.focus() == (PaintControl{Kind::Swatch, 15}) && p.column() == 3 && p.row() == PaintRow::Grid2);
    REQUIRE(p.pointer_press(l.factory_chip.centre(), l) == PaintAccept::Selected);
    REQUIRE(p.preview().factory && p.focus().kind == Kind::Factory && p.is_open());
    REQUIRE(p.pointer_press(l.respray.centre(), l) == PaintAccept::Confirmed);
    const auto order = p.take_order();
    REQUIRE(order.has_value() && order->factory);
    REQUIRE(!p.is_open());
    // A closed picker ignores everything.
    REQUIRE(p.pointer_press(l.cancel.centre(), l) == PaintAccept::None);
    REQUIRE(!p.take_cancel());

    p.open(booth());
    REQUIRE(!p.take_order());
    REQUIRE(p.pointer_press(l.cancel.centre(), l) == PaintAccept::Cancelled);
    REQUIRE(!p.is_open() && p.take_cancel() && !p.take_cancel() && !p.take_order());
    apricot_test::pass("tabs, swatches, chips, RESPRAY and CANCEL act on the press");
}

void focus_model_moves_by_rows_in_drawn_order() {
    PaintPicker p;
    p.open(booth());
    REQUIRE(p.tab() == PaintTab::Presets && p.focus().kind == Kind::Swatch);
    for (int i = 0; i < 4; ++i) p.navigate(0, -1);
    REQUIRE(p.row() == PaintRow::Chips);
    p.navigate(0, 1);
    p.navigate(0, 1);
    REQUIRE(p.row() == PaintRow::Grid0);
    for (int i = 0; i < 6; ++i) p.navigate(-1, 0);
    REQUIRE(p.focus() == (PaintControl{Kind::Swatch, 0}) && p.column() == 0);
    REQUIRE((p.preview() == PaintOrder{false, preset(0).rgb}));

    // Left / right in the grid select as they land, clamped.
    for (int i = 0; i < 3; ++i) p.navigate(1, 0);
    REQUIRE(p.focus() == (PaintControl{Kind::Swatch, 3}) && p.column() == 3);
    REQUIRE((p.preview().colour == preset(3).rgb));
    for (int i = 0; i < 9; ++i) p.navigate(1, 0);
    REQUIRE(p.focus().index == 5);
    p.navigate(-1, 0);
    p.navigate(-1, 0);
    // Down keeps the column; the bottom grid row leads to RESPRAY.
    p.navigate(0, 1);
    REQUIRE(p.focus() == (PaintControl{Kind::Swatch, 9}) && p.row() == PaintRow::Grid1);
    REQUIRE((p.preview().colour == preset(9).rgb));
    p.navigate(0, 1);
    p.navigate(0, 1);
    REQUIRE(p.focus().index == 21 && p.row() == PaintRow::Grid3);
    p.navigate(0, 1);
    REQUIRE(p.focus().kind == Kind::Respray && p.row() == PaintRow::Buttons);
    p.navigate(0, 1);
    REQUIRE(p.focus().kind == Kind::Respray);
    p.navigate(-1, 0);
    REQUIRE(p.focus().kind == Kind::Cancel);
    p.navigate(-1, 0);
    REQUIRE(p.focus().kind == Kind::Cancel);
    p.navigate(1, 0);
    REQUIRE(p.focus().kind == Kind::Respray);
    p.navigate(0, -1);
    REQUIRE(p.focus().index == 21 && p.column() == 3);
    // Up past the grid: the active tab, then FACTORY; clamped; no PREVIOUS to reach.
    for (int i = 0; i < 3; ++i) p.navigate(0, -1);
    REQUIRE(p.focus().index == 3);
    p.navigate(0, -1);
    REQUIRE(p.focus() == (PaintControl{Kind::Tab, 0}));
    p.navigate(0, -1);
    REQUIRE(p.focus().kind == Kind::Factory && p.row() == PaintRow::Chips);
    const PaintOrder at_chip = p.preview();
    p.navigate(0, -1);
    p.navigate(1, 0);
    REQUIRE(p.focus().kind == Kind::Factory);
    REQUIRE(p.preview() == at_chip);  // landing on a chip selects nothing

    // Left / right on the tabs switch the tab and keep focus on the active one.
    p.navigate(0, 1);
    p.navigate(1, 0);
    REQUIRE(p.tab() == PaintTab::Custom && p.focus() == (PaintControl{Kind::Tab, 1}));
    p.navigate(1, 0);
    REQUIRE(p.tab() == PaintTab::Custom);
    const PaintRow custom_path[] = {PaintRow::Hue, PaintRow::Saturation, PaintRow::Brightness, PaintRow::Buttons};
    for (const PaintRow r : custom_path) {
        p.navigate(0, 1);
        REQUIRE(p.row() == r);
    }
    for (int i = 2; i >= 0; --i) {
        p.navigate(0, -1);
        REQUIRE(p.row() == custom_path[i]);
    }
    p.navigate(0, -1);
    REQUIRE(p.focus() == (PaintControl{Kind::Tab, 1}));

    // The plane is pointer-only: from it, up is HUE and down is SATURATION.
    const PaintPickerLayout l = layout_16_9();
    p.pointer_press(l.sv_plane.centre(), l);
    p.pointer_release();
    REQUIRE(p.focus().kind == Kind::SvPlane && p.row() == PaintRow::Saturation);
    p.navigate(0, -1);
    REQUIRE(p.focus().kind == Kind::HueTrack);
    p.pointer_press(l.sv_plane.centre(), l);
    p.pointer_release();
    p.navigate(0, 1);
    REQUIRE(p.focus().kind == Kind::SatTrack);

    // Left / right tap a slider.
    const float s0 = p.hsv().s;
    p.navigate(-1, 0);
    REQUIRE_NEAR(p.hsv().s, std::max(0.0f, s0 - kPaintToneRate.tap), 1e-6);
    p.navigate(0, -1);
    const float h0 = p.hsv().h;
    p.navigate(1, 0);
    REQUIRE_NEAR(p.hsv().h, h0 + kPaintHueRate.tap, 1e-6);

    // Tab from anywhere. NEW is now a hand-dialled colour, so PRESETS lands on
    // the tabs row and leaves it alone.
    REQUIRE(find_paint_preset(p.preview().colour) < 0);
    const PaintOrder dialled = p.preview();
    p.toggle_tab();
    REQUIRE(p.tab() == PaintTab::Presets && p.focus() == (PaintControl{Kind::Tab, 0}));
    REQUIRE(p.preview() == dialled);
    p.navigate(0, 1);  // onto the grid: that selects swatch 3
    REQUIRE((p.preview().colour == preset(3).rgb));
    p.toggle_tab();
    REQUIRE(p.focus().kind == Kind::HueTrack);
    p.toggle_tab();
    REQUIRE(p.focus() == (PaintControl{Kind::Swatch, 3}));
    // F / Y from anywhere focuses FACTORY.
    p.select_factory();
    REQUIRE(p.preview().factory && p.focus().kind == Kind::Factory);
    apricot_test::pass("focus walks rows in drawn order, keeps the grid column and lands where it should");
}

int nav_steps(float hz, float seconds, float axis) {
    NavRepeat n;
    const float dt = 1.0f / hz;
    const long frames = 1 + std::lround(seconds * hz);  // the press, then `seconds` of hold
    int steps = 0;
    for (long f = 0; f < frames; ++f) steps += std::abs(n.step(axis, dt));
    return steps;
}

float held_hue_travel(float hz, float seconds, float axis) {
    const PaintPickerLayout l = layout_16_9();
    PaintPicker p;
    p.open(booth());
    p.toggle_tab();
    p.pointer_press({l.hue_track.min.x + 0.1f * l.hue_track.size().x, l.hue_track.centre().y}, l);
    p.pointer_release();
    REQUIRE(p.focus().kind == Kind::HueTrack);
    const float h0 = p.hsv().h;
    const float dt = 1.0f / hz;
    const long frames = 1 + std::lround(seconds * hz);
    for (long f = 0; f < frames; ++f) p.update(dt, axis, 0.0f);
    return p.hsv().h - h0;
}

void repeat_timing_matches_at_144_and_30_hz() {
    for (const float hz : {144.0f, 30.0f}) {
        REQUIRE(nav_steps(hz, 0.34f, 1.0f) == 1);
        REQUIRE(nav_steps(hz, 0.36f, 1.0f) == 2);
        REQUIRE(nav_steps(hz, 1.0f, 1.0f) == 7);  // press, then 0.35, .46, .57, .68, .79, .90
        REQUIRE(nav_steps(hz, 1.0f, -0.8f) == 7);
        REQUIRE(nav_steps(hz, 1.0f, 0.45f) == 0);  // never pressed
    }

    NavRepeat n;
    REQUIRE(n.step(1.0f, 0.0f) == 1);
    REQUIRE(n.step(0.4f, 0.2f) == 0);   // still held at 0.4: above release
    REQUIRE(n.step(0.4f, 0.2f) == 1);   // first repeat
    REQUIRE(n.step(0.3f, 0.01f) == 0);  // released
    REQUIRE(n.step(0.45f, 0.01f) == 0); // not a press
    REQUIRE(n.step(-0.6f, 0.01f) == -1);
    REQUIRE(n.step(0.9f, 0.01f) == 1);  // a flip is a new press
    REQUIRE(n.step(0.9f, 1.0f) == 1);   // a one-second hitch steps once
    REQUIRE(n.step(0.9f, 0.01f) == 0);
    n.hold_until_release();
    REQUIRE(n.step(1.0f, 0.5f) == 0);
    REQUIRE(n.step(0.0f, 0.01f) == 0);
    REQUIRE(n.step(1.0f, 0.01f) == 1);

    // A held slider: one tap, then the ramp, the same at both rates.
    const double expected = static_cast<double>(kPaintHueRate.tap) + paint_hold_travel(0.0, 1.0, kPaintHueRate);
    // The rates are float constants, so this pins to float precision.
    REQUIRE_NEAR(expected, 1.0 / 72.0 + 0.25 * 0.7 + 0.25 * 0.49 / 1.4, 1e-6);
    const float fast = held_hue_travel(144.0f, 1.0f, 1.0f), slow = held_hue_travel(30.0f, 1.0f, 1.0f);
    REQUIRE_NEAR(fast, expected, 1e-4);
    REQUIRE_NEAR(slow, expected, 1e-4);
    // Past the ramp the rate is flat at max.
    REQUIRE_NEAR(paint_hold_travel(2.0, 3.0, kPaintToneRate), kPaintToneRate.max, 1e-6);
    REQUIRE_NEAR(paint_hold_travel(0.0, 0.3, kPaintToneRate), 0.0, 1e-9);
    // A half-deflected stick moves slower than a key.
    const float stick = held_hue_travel(60.0f, 1.0f, 0.675f);
    REQUIRE_NEAR(stick, kPaintHueRate.tap + 0.5 * paint_hold_travel(0.0, 1.0, kPaintHueRate), 1e-4);

    // Update drives rows from the y axis; a row change latches a held x until it
    // releases, so a held Right does not carry into the next row's meaning.
    PaintPicker p;
    p.open(booth());
    p.toggle_tab();
    REQUIRE(p.focus().kind == Kind::HueTrack);
    p.update(0.016f, 0.0f, 1.0f);
    REQUIRE(p.focus().kind == Kind::SatTrack);
    p.update(0.016f, 0.0f, 0.0f);
    const float s0 = p.hsv().s;
    p.update(0.016f, 1.0f, 1.0f);  // Right and Down on the same frame
    REQUIRE(p.focus().kind == Kind::BriTrack);
    const float v0 = p.hsv().v;
    for (int i = 0; i < 60; ++i) p.update(0.016f, 1.0f, 0.0f);
    REQUIRE(p.hsv().v == v0 && p.hsv().s == s0);
    p.update(0.016f, 0.0f, 0.0f);
    p.update(0.016f, -1.0f, 0.0f);
    REQUIRE_NEAR(p.hsv().v, std::max(0.0f, v0 - kPaintToneRate.tap), 1e-6);

    // Pad triggers turn the hue from any focus, in CUSTOM only.
    const float h0 = p.hsv().h;
    p.update(0.5f, 0.0f, 0.0f, 0.02f);  // inside the dead zone
    REQUIRE(p.hsv().h == h0);
    p.update(0.5f, 0.0f, 0.0f, -1.0f);
    REQUIRE_NEAR(p.hsv().h, std::max(0.0f, h0 - kPaintTriggerHueRate * 0.5f), 1e-5);
    p.toggle_tab();
    const PaintOrder presets_new = p.preview();
    p.update(0.5f, 0.0f, 0.0f, 1.0f);
    REQUIRE(p.preview() == presets_new);
    apricot_test::pass("repeat and slider ramps land the same at 144 Hz and 30 Hz; row changes latch x");
}

void default_focus_is_the_most_contrasting_preset() {
    for (const PaintColor c : {kSignalRed, PaintColor{17, 18, 20}, PaintColor{255, 255, 255},
                               PaintColor{128, 128, 128}, PaintColor{0x8A, 0x3F, 0xD1}}) {
        PaintPickerOpen o = booth();
        o.current = c;
        PaintPicker p;
        p.open(o);
        const int want = farthest_preset(c);
        REQUIRE(p.focus() == (PaintControl{Kind::Swatch, want}));
        REQUIRE(p.column() == want % 6);
        REQUIRE((p.preview() == PaintOrder{false, preset(want).rgb}));
        REQUIRE(!p.already_this_colour());
        // The first preview is announced once, so the host layer composites it once.
        REQUIRE(p.preview_changed());
        REQUIRE(!p.preview_changed());
        PaintPicker again;
        again.open(o);
        REQUIRE(again.focus() == p.focus());
    }
    // A factory car contrasts against its factory colour, not a stale `current`.
    PaintPickerOpen f = booth();
    f.current_is_factory = true;
    f.factory = PaintColor{234, 231, 223};
    f.current = PaintColor{17, 18, 20};
    REQUIRE(farthest_preset(f.factory) != farthest_preset(f.current));
    PaintPicker p;
    p.open(f);
    REQUIRE(p.focus().index == farthest_preset(f.factory));

    // Reopening forgets the last visit: tab, focus, order and cancel.
    p.toggle_tab();
    p.update(1.0f);
    REQUIRE(p.confirm().has_value());
    p.open(booth());
    REQUIRE(p.is_open() && p.tab() == PaintTab::Presets && !p.take_order() && !p.take_cancel());
    REQUIRE(p.open_seconds() == 0.0f && !p.confirm_shortcut());
    apricot_test::pass("the booth opens on the farthest preset, deterministically");
}

void same_paint_rules_depend_on_the_wanted_level() {
    const PaintPickerLayout l = layout_16_9();
    // Not wanted: re-picking the car's own paint is refused, and says so.
    PaintPicker p;
    p.open(booth(0));
    REQUIRE(p.pointer_press(swatch(l, 2).centre(), l) == PaintAccept::Selected);  // SIGNAL RED
    REQUIRE(p.already_this_colour() && !p.confirm_enabled());
    REQUIRE(std::strcmp(p.confirm_label(), "ALREADY THIS COLOUR") == 0);
    REQUIRE(!p.confirm().has_value());
    REQUIRE(p.accept() == PaintAccept::None);
    REQUIRE(p.pointer_press(l.respray.centre(), l) == PaintAccept::None);
    p.update(1.0f);
    REQUIRE(!p.confirm_shortcut().has_value());
    REQUIRE(p.is_open() && !p.take_order());

    // Wanted: the same paint is still a respray, and still runs the wanted rule.
    PaintPicker w;
    w.open(booth(2));
    w.pointer_press(swatch(l, 2).centre(), l);
    REQUIRE(!w.already_this_colour() && w.confirm_enabled());
    const auto order = w.confirm();
    REQUIRE(order.has_value() && !order->factory && order->colour == kSignalRed);
    REQUIRE(!w.is_open());
    REQUIRE(w.take_order() == order);
    REQUIRE(!w.take_order());

    // FACTORY on a factory car follows the same rule.
    PaintPickerOpen f0 = booth(0);
    f0.current_is_factory = true;
    PaintPicker a;
    a.open(f0);
    a.select_factory();
    REQUIRE(a.already_this_colour() && !a.confirm());
    PaintPickerOpen f2 = f0;
    f2.wanted_level = 2;
    PaintPicker b;
    b.open(f2);
    b.select_factory();
    const auto factory_order = b.confirm();
    REQUIRE(factory_order.has_value() && factory_order->factory);

    // A painted colour that matches the factory swatch is still a change: the
    // car goes from factory paint to a respray.
    PaintPickerOpen teal = f0;
    teal.factory = preset(17).rgb;
    PaintPicker c;
    c.open(teal);
    c.pointer_press(swatch(l, 17).centre(), l);
    REQUIRE(!c.already_this_colour() && c.confirm().has_value());
    // And a painted car choosing FACTORY is a change.
    PaintPicker d;
    d.open(booth(0));
    d.select_factory();
    REQUIRE(!d.already_this_colour());
    apricot_test::pass("same paint: refused when not wanted, a respray when wanted; factory follows suit");
}

void custom_tab_seeds_from_new_and_keeps_hue() {
    const PaintPickerLayout l = layout_16_9();
    PaintPicker p;
    p.open(booth());
    p.pointer_press(swatch(l, 12).centre(), l);  // SUNSET ORANGE
    (void)p.preview_changed();
    p.toggle_tab();
    REQUIRE(p.tab() == PaintTab::Custom && p.focus().kind == Kind::HueTrack);
    const PaintHsv seeded = rgb8_to_hsv(preset(12).rgb, 0.0f);
    REQUIRE(p.hsv().h == seeded.h && p.hsv().s == seeded.s && p.hsv().v == seeded.v);
    REQUIRE(!p.preview_changed());  // switching tabs never repaints
    REQUIRE((p.preview() == PaintOrder{false, preset(12).rgb}));

    // Down to black and back up: the hue stays.
    const float hue = p.hsv().h;
    const float bri_y = l.brightness_track.centre().y;
    p.pointer_press({l.brightness_track.min.x, bri_y}, l);
    REQUIRE(p.hsv().v == 0.0f && (p.preview().colour == PaintColor{0, 0, 0}));
    REQUIRE(p.preview_changed());
    p.pointer_move({l.brightness_track.max.x + 5.0f, bri_y}, l);
    p.pointer_release();
    REQUIRE(p.hsv().h == hue && p.hsv().v == 1.0f);

    // Out to PRESETS and back with a grey: the reseed keeps the hue too.
    p.pointer_press({l.saturation_track.min.x, l.saturation_track.centre().y}, l);
    p.pointer_release();
    REQUIRE((p.preview().colour == PaintColor{255, 255, 255}));
    (void)p.preview_changed();
    p.toggle_tab();
    REQUIRE(p.focus() == (PaintControl{Kind::Tab, 0}));
    REQUIRE(!p.preview_changed() && (p.preview().colour == PaintColor{255, 255, 255}));
    p.toggle_tab();
    REQUIRE(p.hsv().h == hue && p.hsv().s == 0.0f && p.hsv().v == 1.0f);

    // Factory paint seeds from the factory colour, and stays FACTORY until a
    // control actually moves.
    p.select_factory();
    p.toggle_tab();
    p.toggle_tab();
    const PaintHsv factory_seed = rgb8_to_hsv(kFactoryBlue, hue);
    REQUIRE(p.hsv().h == factory_seed.h && p.hsv().s == factory_seed.s && p.hsv().v == factory_seed.v);
    REQUIRE(p.preview().factory);
    p.navigate(1, 0);
    REQUIRE(!p.preview().factory);
    REQUIRE((p.preview().colour == hsv_to_rgb8(p.hsv())));
    apricot_test::pass("CUSTOM seeds from the new paint and keeps the hue through greys");
}

glm::vec3 rgb(const glm::vec4& c) { return glm::vec3(c); }

void plane_and_hue_bar_tessellate_within_half_a_level() {
    const auto worst_for = [](int n) {
        double worst = 0;
        const float cells = static_cast<float>(n);
        for (const float hue : {0.0f, 1.0f / 12.0f, 1.0f / 6.0f, 0.3f, 0.5f, 0.7f, 0.95f})
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) {
                    const glm::vec3 tl = rgb(sv_plane_vertex_colour(hue, i, j, n));
                    const glm::vec3 tr = rgb(sv_plane_vertex_colour(hue, i + 1, j, n));
                    const glm::vec3 bl = rgb(sv_plane_vertex_colour(hue, i, j + 1, n));
                    const glm::vec3 br = rgb(sv_plane_vertex_colour(hue, i + 1, j + 1, n));
                    for (int k = 0; k <= 8; ++k)
                        for (int m = 0; m <= 8; ++m) {
                            const float u = static_cast<float>(k) / 8.0f, w = static_cast<float>(m) / 8.0f;
                            const glm::vec3 want = paint_hsv_to_rgb(
                                {hue, (static_cast<float>(i) + u) / cells, 1.0f - (static_cast<float>(j) + w) / cells});
                            // The host layer may split a quad along either diagonal.
                            const glm::vec3 diag_a = u >= w ? tl + u * (tr - tl) + w * (br - tr)
                                                            : tl + w * (bl - tl) + u * (br - bl);
                            const glm::vec3 diag_b = u + w <= 1.0f
                                ? tl + u * (tr - tl) + w * (bl - tl)
                                : br + (1.0f - u) * (bl - br) + (1.0f - w) * (tr - br);
                            for (int ch = 0; ch < 3; ++ch) {
                                worst = std::max(worst, 255.0 * std::fabs(static_cast<double>(diag_a[ch] - want[ch])));
                                worst = std::max(worst, 255.0 * std::fabs(static_cast<double>(diag_b[ch] - want[ch])));
                            }
                        }
                }
        return worst;
    };
    const double two_triangles = worst_for(1), grid = worst_for(kPaintPlaneGrid);
    std::printf("  SV plane error: %.2f/255 as two triangles, %.3f/255 as %dx%d\n", two_triangles, grid,
                kPaintPlaneGrid, kPaintPlaneGrid);
    REQUIRE(two_triangles > 60.0);
    REQUIRE(grid <= 0.5);

    double hue_worst = 0, track_worst = 0;
    for (int seg = 0; seg < kPaintHueSegments; ++seg) {
        const glm::vec3 a = rgb(hue_segment_colour(seg)), b = rgb(hue_segment_colour(seg + 1));
        for (int k = 0; k <= 16; ++k) {
            const float t = static_cast<float>(k) / 16.0f;
            const glm::vec3 want = paint_hsv_to_rgb(
                {(static_cast<float>(seg) + t) / static_cast<float>(kPaintHueSegments), 1.0f, 1.0f});
            const glm::vec3 got = a + t * (b - a);
            for (int ch = 0; ch < 3; ++ch)
                hue_worst = std::max(hue_worst, 255.0 * std::fabs(static_cast<double>(got[ch] - want[ch])));
        }
    }
    REQUIRE((rgb(hue_segment_colour(0)) == glm::vec3(1, 0, 0)));
    REQUIRE((rgb(hue_segment_colour(36)) == glm::vec3(1, 0, 0)));
    for (const PaintHsv c : {PaintHsv{0.1f, 0.3f, 0.8f}, PaintHsv{0.62f, 0.9f, 0.4f}}) {
        const auto sat = saturation_track_colours(c);
        const auto bri = brightness_track_colours(c);
        for (int k = 0; k <= 16; ++k) {
            const float t = static_cast<float>(k) / 16.0f;
            const glm::vec3 sat_want = paint_hsv_to_rgb({c.h, t, c.v});
            const glm::vec3 bri_want = paint_hsv_to_rgb({c.h, c.s, t});
            const glm::vec3 sat_got = rgb(sat[0]) + t * (rgb(sat[1]) - rgb(sat[0]));
            const glm::vec3 bri_got = rgb(bri[0]) + t * (rgb(bri[1]) - rgb(bri[0]));
            for (int ch = 0; ch < 3; ++ch) {
                track_worst = std::max(track_worst, 255.0 * std::fabs(static_cast<double>(sat_got[ch] - sat_want[ch])));
                track_worst = std::max(track_worst, 255.0 * std::fabs(static_cast<double>(bri_got[ch] - bri_want[ch])));
            }
        }
    }
    REQUIRE(hue_worst < 1e-3);
    REQUIRE(track_worst < 1e-3);
    apricot_test::pass("12x12 SV plane within 0.5/255; 36 hue segments and both tracks exact");
}

void accept_activates_the_focused_control() {
    const PaintPickerLayout l = layout_16_9();
    {  // CANCEL cancels, and leaves no order.
        PaintPicker p;
        p.open(booth());
        down_to_buttons(p);
        p.navigate(-1, 0);
        REQUIRE(p.focus().kind == Kind::Cancel);
        REQUIRE(p.accept() == PaintAccept::Cancelled);
        REQUIRE(!p.is_open() && p.take_cancel() && !p.take_order());
        REQUIRE(p.accept() == PaintAccept::None);
    }
    {  // Tabs switch the tab, both ways, without closing.
        PaintPicker p;
        p.open(booth());
        up_to_chips(p);
        p.navigate(0, 1);
        REQUIRE(p.row() == PaintRow::Tabs);
        REQUIRE(p.accept() == PaintAccept::SwitchedTab);
        REQUIRE(p.tab() == PaintTab::Custom && p.focus() == (PaintControl{Kind::Tab, 1}));
        REQUIRE(p.accept() == PaintAccept::SwitchedTab);
        REQUIRE(p.tab() == PaintTab::Presets && p.focus() == (PaintControl{Kind::Tab, 0}));
        REQUIRE(p.is_open() && !p.take_order());
    }
    {  // FACTORY selects without an order and hands focus to RESPRAY.
        PaintPicker p;
        p.open(booth());
        up_to_chips(p);
        REQUIRE(p.focus().kind == Kind::Factory);
        REQUIRE(p.accept() == PaintAccept::Selected);
        REQUIRE(p.is_open() && !p.take_order() && p.preview().factory);
        REQUIRE(p.focus().kind == Kind::Respray);
        REQUIRE(p.accept() == PaintAccept::Confirmed);
        const auto order = p.take_order();
        REQUIRE(order.has_value() && order->factory);
    }
    {  // PREVIOUS selects the earlier paint without an order.
        PaintPickerOpen o = booth();
        o.previous = PaintOrder{false, {10, 200, 30}};
        PaintPicker p;
        p.open(o);
        up_to_chips(p);
        p.navigate(1, 0);
        REQUIRE(p.focus().kind == Kind::Previous);
        REQUIRE(p.accept() == PaintAccept::Selected);
        REQUIRE((p.preview() == PaintOrder{false, {10, 200, 30}}));
        REQUIRE(p.is_open() && !p.take_order());
    }
    {  // A swatch confirms it.
        PaintPicker p;
        p.open(booth());
        const PaintOrder want = p.preview();
        REQUIRE(p.focus().kind == Kind::Swatch);
        REQUIRE(p.accept() == PaintAccept::Confirmed);
        const auto order = p.take_order();
        REQUIRE(!p.is_open() && order.has_value() && *order == want);
    }
    // HUE, SATURATION, BRIGHTNESS, the plane and RESPRAY confirm.
    for (const Kind k : {Kind::HueTrack, Kind::SatTrack, Kind::BriTrack, Kind::SvPlane, Kind::Respray}) {
        PaintPicker p;
        p.open(booth());
        p.toggle_tab();
        if (k == Kind::Respray) {
            down_to_buttons(p);
        } else {
            p.pointer_press(l.rect({k, 0}).centre(), l);
            p.pointer_release();
        }
        REQUIRE(p.focus().kind == k);
        const PaintOrder want = p.preview();
        REQUIRE(p.accept() == PaintAccept::Confirmed);
        const auto order = p.take_order();
        REQUIRE(order.has_value() && *order == want);
    }
    apricot_test::pass("Accept: tabs switch, FACTORY and PREVIOUS select, CANCEL cancels, the rest confirm");
}

void confirm_shortcut_waits_out_the_opening_press() {
    PaintPicker p;
    p.open(booth());
    REQUIRE(!p.confirm_shortcut());  // the same frame as the press that opened it
    p.update(0.1f);
    REQUIRE(!p.confirm_shortcut() && p.is_open());
    p.update(0.1f);
    const auto order = p.confirm_shortcut();
    REQUIRE(order.has_value() && !p.is_open());
    REQUIRE(p.take_order() == order);

    // From any focus, without moving to RESPRAY.
    PaintPicker q;
    q.open(booth());
    up_to_chips(q);
    q.update(0.2f);
    REQUIRE(q.focus().kind == Kind::Factory);
    REQUIRE(q.confirm_shortcut().has_value());

    // Host UI time, however it is sliced.
    PaintPicker r;
    r.open(booth());
    for (int i = 0; i < 14; ++i) r.update(0.01f);
    REQUIRE(!r.confirm_shortcut());
    for (int i = 0; i < 6; ++i) r.update(0.01f);
    REQUIRE(r.confirm_shortcut().has_value());
    apricot_test::pass("R / X confirm from anywhere, but not within 0.15 s of opening");
}

void previous_chip_exists_only_after_a_respray() {
    const PaintPickerLayout l = layout_16_9();
    PaintPicker p;
    p.open(booth());
    REQUIRE(!p.has_previous());
    up_to_chips(p);
    p.navigate(1, 0);
    REQUIRE(p.focus().kind == Kind::Factory);
    REQUIRE(p.pointer_press(l.previous_chip.centre(), l) == PaintAccept::None);
    REQUIRE(p.focus().kind == Kind::Factory);
    p.pointer_move(l.previous_chip.centre(), l);
    REQUIRE(p.hover().kind == Kind::None);

    // With one, it is on the focus path; a factory previous drops any stray colour.
    PaintPickerOpen o = booth();
    o.previous = PaintOrder{true, {9, 9, 9}};
    p.open(o);
    REQUIRE(p.has_previous() && (p.previous() == PaintOrder{true, {}}));
    up_to_chips(p);
    p.navigate(1, 0);
    REQUIRE(p.focus().kind == Kind::Previous);
    p.navigate(1, 0);
    REQUIRE(p.focus().kind == Kind::Previous);
    REQUIRE(p.accept() == PaintAccept::Selected);
    REQUIRE((p.preview() == PaintOrder{true, {}}));

    // Selecting it by pointer previews that paint.
    o.previous = PaintOrder{false, {40, 90, 160}};
    p.open(o);
    (void)p.preview_changed();
    p.pointer_move(l.previous_chip.centre(), l);
    REQUIRE(p.hover().kind == Kind::Previous);
    REQUIRE(p.pointer_press(l.previous_chip.centre(), l) == PaintAccept::Selected);
    REQUIRE((p.preview() == PaintOrder{false, {40, 90, 160}}));
    REQUIRE(p.preview_changed() && p.is_open() && !p.take_order());
    apricot_test::pass("PREVIOUS is off the focus path and unhittable until a respray gives it a paint");
}

void labels_and_status_for_heat_and_sight() {
    struct Case {
        int wanted;
        bool seen;
        const char* label;
        const char* status;
    };
    const Case cases[] = {
        {0, false, "RESPRAY", "NOT WANTED - PICK ANY COLOUR"},
        {0, true, "RESPRAY", "NOT WANTED - PICK ANY COLOUR"},
        {2, false, "RESPRAY + LOSE THE COPS", "NO COP SAW YOU PULL IN - A RESPRAY LOSES THEM"},
        {2, true, "RESPRAY - STARS STAY", "LEAVE THE BAY, LOSE THEM, PULL IN AGAIN"},
    };
    for (const Case& c : cases) {
        PaintPicker p;
        p.open(booth(c.wanted, c.seen));
        REQUIRE(p.wanted_level() == c.wanted && p.seen() == c.seen);
        REQUIRE(!p.already_this_colour());
        REQUIRE_MSG(std::strcmp(p.confirm_label(), c.label) == 0, "confirm label", c.label);
        REQUIRE_MSG(std::strcmp(p.status_line(), c.status) == 0, "status line", c.status);
    }
    REQUIRE(std::strstr(kPaintPickerHint, "ENTER / A  SELECT") != nullptr);
    REQUIRE(std::strstr(kPaintPickerHint, "R / X  RESPRAY") != nullptr);
    REQUIRE(std::strstr(kPaintPickerHint, "ESC / B  CANCEL") != nullptr);
    apricot_test::pass("confirm label and status line for (0,-), (2,unseen) and (2,seen)");
}

}  // namespace

int main() {
    std::printf("paint_picker_tests\n");
    presets_are_named_distinct_and_round_trip_through_hsv();
    presets_stay_apart_in_oklab();
    layout_leaves_the_car_clear_at_every_aspect();
    every_control_is_hit_testable_at_1x_and_2x_dpi();
    plane_corners_and_drag_clamp();
    pointer_presses_act_on_press();
    focus_model_moves_by_rows_in_drawn_order();
    repeat_timing_matches_at_144_and_30_hz();
    default_focus_is_the_most_contrasting_preset();
    same_paint_rules_depend_on_the_wanted_level();
    custom_tab_seeds_from_new_and_keeps_hue();
    plane_and_hue_bar_tessellate_within_half_a_level();
    accept_activates_the_focused_control();
    confirm_shortcut_waits_out_the_opening_press();
    previous_chip_exists_only_after_a_respray();
    labels_and_status_for_heat_and_sight();
    return apricot_test::done("paint_picker_tests");
}
