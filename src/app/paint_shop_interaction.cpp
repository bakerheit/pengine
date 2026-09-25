#include "app/paint_shop_interaction.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/game_ui.h"
#include "core/log.h"
#include "game/ui_canvas.h"
#include "gfx/hud.h"

namespace apricot {
namespace {

constexpr glm::vec4 kInk{0.94f, 0.91f, 0.81f, 1.0f};
constexpr glm::vec4 kMuted{0.63f, 0.66f, 0.64f, 1.0f};
constexpr glm::vec4 kAmber{0.96f, 0.55f, 0.12f, 1.0f};
constexpr glm::vec4 kRed{1.0f, 0.34f, 0.26f, 1.0f};
constexpr glm::vec4 kPanel{0.025f, 0.035f, 0.045f, 0.92f};
constexpr glm::vec4 kWell{0.012f, 0.018f, 0.024f, 0.95f};
constexpr glm::vec4 kControl{0.10f, 0.12f, 0.14f, 1.0f};
constexpr glm::vec4 kBlack{0.0f, 0.0f, 0.0f, 1.0f};
constexpr glm::vec4 kWhite{1.0f, 1.0f, 1.0f, 1.0f};

// Orbit: right-mouse drag outside the panel, or the right stick.
constexpr float kOrbitRadiansPerPixel = 0.006f;
constexpr float kOrbitLimit = 1.1f;
constexpr float kOrbitHomeDelay = 1.5f;

glm::vec4 rgba(PaintColor c) {
    return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f, 1.0f};
}

float trigger_unit(int16_t value) {
    return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
}

void outline_outside(Hud& hud, const PaintRect& r, float thickness, glm::vec4 colour) {
    hud.outline(r.min - glm::vec2{thickness}, r.max + glm::vec2{thickness}, thickness, colour);
}

void knob(Hud& hud, glm::vec2 centre, float s, glm::vec4 fill) {
    hud.circle(centre, 11.0f * s, kBlack);
    hud.circle(centre, 8.0f * s, kWhite);
    hud.circle(centre, 6.0f * s, fill);
}

}  // namespace

void PaintShopInteraction::open(const PaintPickerOpen& init) {
    picker_.open(init);
    trigger_left_ = trigger_right_ = 0.0f;
    orbiting_ = false;
    orbit_yaw_ = 0.0f;
    orbit_idle_s_ = 0.0f;
}

void PaintShopInteraction::event(const SDL_Event& e, glm::vec2 window_size, glm::vec2 canvas_size) {
    if (!modal()) return;
    handle(e, window_size, canvas_size);
    if (!modal()) {
        // One line per close, so a booth that shuts itself says what shut it.
        AP_INFO("respray booth closed by event 0x%x (key %d repeat %d, button %d at %d,%d)",
                e.type, e.type == SDL_KEYDOWN || e.type == SDL_KEYUP ? e.key.keysym.sym : 0,
                e.type == SDL_KEYDOWN ? e.key.repeat : 0,
                e.type == SDL_MOUSEBUTTONDOWN ? e.button.button
                    : e.type == SDL_CONTROLLERBUTTONDOWN ? e.cbutton.button : -1,
                e.type == SDL_MOUSEBUTTONDOWN ? e.button.x : 0,
                e.type == SDL_MOUSEBUTTONDOWN ? e.button.y : 0);
    }
}

void PaintShopInteraction::handle(const SDL_Event& e, glm::vec2 window_size, glm::vec2 canvas_size) {
    const PaintPickerLayout layout = PaintPickerLayout::from_canvas(canvas_size);
    const UiCanvas canvas{canvas_size};
    const auto at = [&](int x, int y) {
        return canvas.from_window({static_cast<float>(x), static_cast<float>(y)}, window_size);
    };
    switch (e.type) {
    case SDL_KEYDOWN: {
        // A held key's repeats are not presses. Held directions repeat through
        // the picker's own timing on the UI axis instead.
        if (e.key.repeat != 0) return;
        const SDL_Keycode key = e.key.keysym.sym;
        if (key == SDLK_ESCAPE || key == SDLK_BACKSPACE) picker_.cancel();
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_e || key == SDLK_SPACE)
            picker_.accept();
        else if (key == SDLK_r) picker_.confirm_shortcut();
        else if (key == SDLK_f) picker_.select_factory();
        else if (key == SDLK_TAB) picker_.toggle_tab();
        return;
    }
    case SDL_CONTROLLERBUTTONDOWN:
        switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A: picker_.accept(); break;
        case SDL_CONTROLLER_BUTTON_B: picker_.cancel(); break;
        case SDL_CONTROLLER_BUTTON_X: picker_.confirm_shortcut(); break;
        case SDL_CONTROLLER_BUTTON_Y: picker_.select_factory(); break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: picker_.toggle_tab(); break;
        default: break;
        }
        return;
    case SDL_CONTROLLERAXISMOTION:
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) trigger_left_ = trigger_unit(e.caxis.value);
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) trigger_right_ = trigger_unit(e.caxis.value);
        return;
    case SDL_MOUSEBUTTONDOWN: {
        const glm::vec2 p = at(e.button.x, e.button.y);
        if (e.button.button == SDL_BUTTON_LEFT) picker_.pointer_press(p, layout);
        else if (e.button.button == SDL_BUTTON_RIGHT && !layout.panel.contains(p)) orbiting_ = true;
        return;
    }
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT) picker_.pointer_release();
        else if (e.button.button == SDL_BUTTON_RIGHT) orbiting_ = false;
        return;
    case SDL_MOUSEMOTION:
        picker_.pointer_move(at(e.motion.x, e.motion.y), layout);
        // Relative motion off the event, never a warp or relative mode: the
        // cursor stays the player's while the booth is open.
        if (orbiting_) {
            orbit_yaw_ = std::clamp(orbit_yaw_ + static_cast<float>(e.motion.xrel) * kOrbitRadiansPerPixel,
                                    -kOrbitLimit, kOrbitLimit);
            orbit_idle_s_ = 0.0f;
        }
        return;
    case SDL_MOUSEWHEEL: {
        const int notches = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
        picker_.wheel(at(e.wheel.mouseX, e.wheel.mouseY), notches, layout);
        return;
    }
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            // A release may never arrive: drop every held thing, keep the booth.
            picker_.focus_lost();
            orbiting_ = false;
            trigger_left_ = trigger_right_ = 0.0f;
        }
        return;
    default:
        return;
    }
}

void PaintShopInteraction::update(float dt, float ui_axis_x, float ui_axis_y, float look_dx) {
    if (!modal()) return;
    const float step = std::isfinite(dt) ? std::max(0.0f, dt) : 0.0f;
    picker_.update(step, ui_axis_x, ui_axis_y, trigger_right_ - trigger_left_);
    if (std::isfinite(look_dx) && look_dx != 0.0f) {
        orbit_yaw_ = std::clamp(orbit_yaw_ + look_dx, -kOrbitLimit, kOrbitLimit);
        orbit_idle_s_ = 0.0f;
    } else if (!orbiting_) {
        orbit_idle_s_ += step;
        if (orbit_idle_s_ > kOrbitHomeDelay) orbit_yaw_ *= std::exp(-3.0f * step);
    }
}

void PaintShopInteraction::draw(Hud& hud, const GameUi& ui, glm::vec2 canvas,
                                const PaintShopView& view) const {
    if (!modal()) return;
    using Kind = PaintControl::Kind;
    const PaintPickerLayout l = PaintPickerLayout::from_canvas(canvas);
    const float s = l.s;
    const auto size = [s](float authored) { return authored * s; };
    const PaintOrder next = picker_.preview();
    const PaintColor next_rgb = next.factory ? picker_.factory_colour() : next.colour;
    const PaintOrder now = picker_.current();
    const PaintColor now_rgb = now.factory ? picker_.factory_colour() : now.colour;

    // Chrome.
    hud.rect(l.panel.min, l.panel.max, kPanel);
    hud.outline(l.panel.min, l.panel.max, 2.0f, {kAmber.r, kAmber.g, kAmber.b, 0.55f});
    hud.title_text("RESPRAY", l.title, size(PaintPickerText::kTitle), kInk);
    const float subtitle = size(PaintPickerText::kSubtitle);
    const char* const kSubtitle = "ROOK'S AUTO REPAIR";
    hud.text(kSubtitle, {l.subtitle_right.x - hud.measure_text(kSubtitle, subtitle), l.subtitle_right.y},
             subtitle, kMuted);

    // What a respray will do to the stars.
    hud.rect(l.wanted_strip.min, l.wanted_strip.max, kWell);
    if (view.wanted_level > 0)
        ui.draw_wanted_badge(hud, view.wanted_level, view.flash, view.step, l.stars_right, l.stars_top);
    const glm::vec4 status_colour = view.wanted_level <= 0 ? kMuted : (picker_.seen() ? kRed : kAmber);
    hud.text(picker_.status_line(), l.status, size(PaintPickerText::kStatus), status_colour);

    // Chips: the paint the car wears, the factory reset, and the one before
    // the last respray this visit.
    const auto chip = [&](const PaintRect& r, const char* label, const char* name, PaintColor colour) {
        hud.rect(r.min, r.max, kControl);
        const float pad = size(12.0f);
        const float swatch = r.size().y - 2.0f * pad;
        hud.rect(r.min + glm::vec2{pad}, r.min + glm::vec2{pad + swatch}, rgba(colour));
        hud.outline(r.min + glm::vec2{pad}, r.min + glm::vec2{pad + swatch}, 1.0f, kBlack);
        const float x = r.min.x + 2.0f * pad + swatch;
        hud.text(label, {x, r.min.y + size(6.0f)}, size(14.0f), kMuted);
        hud.text(name, {x, r.min.y + size(26.0f)}, size(PaintPickerText::kChip), kInk);
    };
    chip(l.now_chip, "NOW", paint_order_name(now), now_rgb);
    chip(l.factory_chip, "F / Y", "FACTORY PAINT", picker_.factory_colour());
    if (const auto previous = picker_.previous()) {
        chip(l.previous_chip, "PREVIOUS", paint_order_name(*previous),
             previous->factory ? picker_.factory_colour() : previous->colour);
    }

    // Tabs.
    const float tab_text = size(PaintPickerText::kTab);
    for (int i = 0; i < 2; ++i) {
        const PaintRect& r = l.tabs[static_cast<std::size_t>(i)];
        const bool active = (i == 0) == (picker_.tab() == PaintTab::Presets);
        hud.rect(r.min, r.max, active ? kAmber : kControl);
        hud.text_centered(i == 0 ? "PRESETS" : "CUSTOM", r.centre().x, r.centre().y - tab_text * 0.5f,
                          tab_text, active ? glm::vec4{0.05f, 0.04f, 0.03f, 1.0f} : kInk);
    }

    if (picker_.tab() == PaintTab::Presets) {
        for (int i = 0; i < 24; ++i) {
            const PaintRect& r = l.swatches[static_cast<std::size_t>(i)];
            const PaintPreset& preset = kPaintPresets[static_cast<std::size_t>(i)];
            hud.rect(r.min, r.max, rgba(preset.rgb));
            hud.outline(r.min, r.max, 1.0f, {0.0f, 0.0f, 0.0f, 0.8f});
            if (!next.factory && preset.rgb == next.colour) hud.outline(r.min, r.max, 3.0f * s, kWhite);
        }
        hud.text(paint_order_name(next), l.swatch_name, size(PaintPickerText::kSwatchName), kInk);
        const int preset = next.factory ? -1 : find_paint_preset(next.colour);
        if (preset >= 0) {
            hud.text(kPaintPresets[static_cast<std::size_t>(preset)].group, l.swatch_group,
                     size(PaintPickerText::kGroup), kMuted);
        }
    } else {
        const PaintHsv hsv = picker_.hsv();
        const float label = size(PaintPickerText::kLabel);
        hud.text("HUE", l.hue_label, label, kMuted);
        // 36 segments: the hue bar's kinks fall on sixths, so this is exact.
        const float segment = l.hue_track.size().x / static_cast<float>(kPaintHueSegments);
        for (int i = 0; i < kPaintHueSegments; ++i) {
            const glm::vec4 a = hue_segment_colour(i), b = hue_segment_colour(i + 1);
            const float x0 = l.hue_track.min.x + segment * static_cast<float>(i);
            hud.gradient_rect({x0, l.hue_track.min.y}, {x0 + segment, l.hue_track.max.y}, a, b, b, a);
        }
        const float notch = l.hue_notch_x(hsv.h);
        hud.rect({notch - 5.0f * s, l.hue_track.min.y - 6.0f * s}, {notch + 5.0f * s, l.hue_track.max.y + 6.0f * s}, kBlack);
        hud.rect({notch - 3.0f * s, l.hue_track.min.y - 4.0f * s}, {notch + 3.0f * s, l.hue_track.max.y + 4.0f * s}, kWhite);

        // Bilinear in (s, v) at a fixed hue; a 12x12 grid keeps it within half a level.
        hud.gradient_rect(l.sv_plane.min, l.sv_plane.max,
                          sv_plane_vertex_colour(hsv.h, 0, 0, 1), sv_plane_vertex_colour(hsv.h, 1, 0, 1),
                          sv_plane_vertex_colour(hsv.h, 1, 1, 1), sv_plane_vertex_colour(hsv.h, 0, 1, 1),
                          kPaintPlaneGrid, kPaintPlaneGrid);
        knob(hud, l.plane_knob(hsv), s, rgba(next_rgb));

        const auto track = [&](const char* name, glm::vec2 label_at, const PaintRect& r,
                               std::array<glm::vec4, 2> ends, float value) {
            hud.text(name, label_at, label, kMuted);
            hud.gradient_rect(r.min, r.max, ends[0], ends[1], ends[1], ends[0]);
            knob(hud, {l.track_knob_x(r, value), r.centre().y}, s, rgba(next_rgb));
        };
        track("SATURATION", l.saturation_label, l.saturation_track, saturation_track_colours(hsv), hsv.s);
        track("BRIGHTNESS", l.brightness_label, l.brightness_track, brightness_track_colours(hsv), hsv.v);
        char readout[32];
        std::snprintf(readout, sizeof readout, "%s  #%02X%02X%02X", paint_order_name(next),
                      next_rgb.r, next_rgb.g, next_rgb.b);
        hud.text(readout, l.readout, size(PaintPickerText::kReadout), kInk);
    }

    // Buttons.
    const float button_text = size(PaintPickerText::kButton);
    hud.rect(l.cancel.min, l.cancel.max, kControl);
    hud.text_centered("CANCEL", l.cancel.centre().x, l.cancel.centre().y - button_text * 0.5f, button_text, kInk);
    const bool enabled = picker_.confirm_enabled();
    hud.rect(l.respray.min, l.respray.max, enabled ? kAmber : kControl);
    const float chip_pad = size(14.0f);
    const float chip_size = l.respray.size().y - 2.0f * chip_pad;
    hud.rect(l.respray.min + glm::vec2{chip_pad}, l.respray.min + glm::vec2{chip_pad + chip_size}, rgba(next_rgb));
    hud.outline(l.respray.min + glm::vec2{chip_pad}, l.respray.min + glm::vec2{chip_pad + chip_size}, 2.0f, kBlack);
    const float label_left = l.respray.min.x + 2.0f * chip_pad + chip_size;
    const float label_size = std::min(button_text, button_text * (l.respray.max.x - label_left - chip_pad) /
        std::max(1.0f, hud.measure_text(picker_.confirm_label(), button_text)));
    hud.text_centered(picker_.confirm_label(), (label_left + l.respray.max.x - chip_pad) * 0.5f,
                      l.respray.centre().y - label_size * 0.5f, label_size,
                      enabled ? glm::vec4{0.05f, 0.04f, 0.03f, 1.0f} : kMuted);
    hud.text(kPaintPickerHint, l.hint, size(PaintPickerText::kHint), kMuted);

    // Hover, then focus on top of it.
    if (picker_.hover().kind != Kind::None && picker_.hover() != picker_.focus())
        hud.outline(l.rect(picker_.hover()).min, l.rect(picker_.hover()).max, 2.0f, {kInk.r, kInk.g, kInk.b, 0.6f});
    if (picker_.focus().kind != Kind::None) outline_outside(hud, l.rect(picker_.focus()), 4.0f * s, kAmber);
}

}  // namespace apricot
