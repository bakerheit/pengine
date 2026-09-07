#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>

#include "city/map.h"
#include "core/input_frame.h"

namespace apricot {

// The game-facing screens. This is sim-side plain state even though it does
// not advance the vehicle: menu behaviour needs headless tests just as much as
// driving does. Rendering lives in app/game_ui.cpp.
enum class UiScreen : uint8_t {
    Title,
    Driving,
    Pause,
    Map,
    Settings,
};

enum class SettingsPage : uint8_t {
    Root,
    Map,
    Graphics,
    Audio,
    Controls,
    Accessibility,
};

// Player-facing preferences. This is deliberately plain state so the menu is
// testable without a window or audio device. App applies the values to the
// host systems when they change.
struct UiSettings {
    bool minimap_points_of_interest = true;
    bool minimap_mission_marker = true;
    bool minimap_waypoint_marker = true;
    bool vsync = true;
    int camera_fov = 60;
    bool weather_effects = true;
    float master_volume = 1.0f;
    float music_volume = 1.0f;
    float sfx_volume = 1.0f;
    bool camera_auto_recenter = true;
    bool camera_shake = true;
    bool subtitles = true;
    bool reduced_motion = false;
};

enum class UiAction : uint8_t {
    None,
    NewGame,
    BeginDrive = NewGame, // Compatibility for bounded dev launches.
    LoadGame,
    SaveGame,
    ResetVehicle,
    QuitGame,
};

struct MapViewport {
    float left = 0.0f;
    float top = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
    float world_center_x = 0.0f;
    float world_center_z = 0.0f;
    float world_span_m = city::kWorldHalfMetres * 2.0f;
};

struct MapPoint {
    float x = 0.0f;
    float y = 0.0f;
};

inline float map_pixels_per_metre(const MapViewport& viewport) {
    return std::max(1.0f, std::min(viewport.width, viewport.height)) /
           std::max(viewport.world_span_m, 1.0f);
}

// +X is east/right and +Z is south/down in O'Haven. Keep that convention on
// the map so a player turning east does not watch their marker move left.
inline MapPoint world_to_map(float world_x, float world_z,
                             const MapViewport& viewport) {
    const float scale = map_pixels_per_metre(viewport);
    return {
        viewport.left +
            (world_x - viewport.world_center_x) * scale +
            viewport.width * 0.5f,
        viewport.top +
            (world_z - viewport.world_center_z) * scale +
            viewport.height * 0.5f,
    };
}

// Smooth, bounded camera for the full-screen city map. It lives beside the UI
// flow because its maths is device- and renderer-free, which keeps zoom/pan
// behaviour directly testable.
class MapCamera {
public:
    static constexpr float kFullSpanM = city::kWorldHalfMetres * 2.0f;
    static constexpr float kDefaultOpenSpanM = 200.0f;
    static constexpr float kClosestSpanM = kDefaultOpenSpanM;

    void set_viewport_size(float width, float height) {
        const float shorter = std::max(1.0f, std::min(width, height));
        aspect_x_ = std::max(1.0f, width / shorter);
        aspect_z_ = std::max(1.0f, height / shorter);
        clamp_target();
        clamp_current();
    }

    void reset() {
        center_x_ = 0.0f;
        center_z_ = 0.0f;
        target_x_ = 0.0f;
        target_z_ = 0.0f;
        span_m_ = kFullSpanM;
        target_span_m_ = kFullSpanM;
    }

    void open_at(float focus_x, float focus_z,
                 float span_m = kDefaultOpenSpanM) {
        const float clamped_span =
            std::clamp(span_m, kClosestSpanM, kFullSpanM);
        center_x_ = target_x_ = focus_x;
        center_z_ = target_z_ = focus_z;
        span_m_ = target_span_m_ = clamped_span;
        clamp_target();
        clamp_current();
    }

    void zoom(float steps, float focus_x, float focus_z) {
        if (std::fabs(steps) <= 1e-5f) return;
        const float before = target_span_m_;
        target_span_m_ = std::clamp(
            before * std::pow(0.72f, steps), kClosestSpanM, kFullSpanM);
        if (target_span_m_ < before && before >= kFullSpanM - 1.0f) {
            target_x_ = focus_x;
            target_z_ = focus_z;
        }
        clamp_target();
    }

    // Anchor to the visible map, not the pending target: another wheel event
    // may arrive before the previous smooth zoom has finished.
    void zoom_at(float steps, MapPoint pointer, const MapViewport& layout) {
        if (std::fabs(steps) <= 1e-5f ||
            pointer.x < layout.left || pointer.x > layout.left + layout.width ||
            pointer.y < layout.top || pointer.y > layout.top + layout.height)
            return;
        const float next_span = std::clamp(
            target_span_m_ * std::pow(0.72f, steps), kClosestSpanM, kFullSpanM);
        if (next_span == target_span_m_) return;
        const float shorter = std::max(1.0f, std::min(layout.width, layout.height));
        const float offset_x = (pointer.x - layout.left - layout.width * 0.5f) / shorter;
        const float offset_z = (pointer.y - layout.top - layout.height * 0.5f) / shorter;
        target_x_ = center_x_ + offset_x * (span_m_ - next_span);
        target_z_ = center_z_ + offset_z * (span_m_ - next_span);
        target_span_m_ = next_span;
        clamp_target();
    }

    void pan(float axis_x, float axis_z, float dt) {
        const float length = std::sqrt(axis_x * axis_x + axis_z * axis_z);
        if (length <= 1e-5f || dt <= 0.0f) return;
        const float scale = target_span_m_ * 0.62f * std::min(dt, 0.1f) /
                            std::max(length, 1.0f);
        target_x_ += axis_x * scale;
        target_z_ += axis_z * scale;
        clamp_target();
    }

    void pan_world(float delta_x, float delta_z) {
        target_x_ += delta_x;
        target_z_ += delta_z;
        center_x_ += delta_x;
        center_z_ += delta_z;
        clamp_target();
        clamp_current();
    }

    void recenter(float focus_x, float focus_z) {
        target_x_ = focus_x;
        target_z_ = focus_z;
        clamp_target();
    }

    void update(float dt) {
        const float blend = 1.0f - std::exp(-11.0f * std::max(dt, 0.0f));
        span_m_ += (target_span_m_ - span_m_) * blend;
        center_x_ += (target_x_ - center_x_) * blend;
        center_z_ += (target_z_ - center_z_) * blend;
        clamp_current();
    }

    MapViewport viewport(float left, float top, float width,
                         float height) const {
        return {left, top, width, height, center_x_, center_z_, span_m_};
    }

    float center_x() const { return center_x_; }
    float center_z() const { return center_z_; }
    float span_m() const { return span_m_; }
    float target_span_m() const { return target_span_m_; }
    float zoom_level() const { return kFullSpanM / span_m_; }

private:
    static float clamped_center(float value, float span) {
        const float limit =
            std::max(0.0f, city::kWorldHalfMetres - span * 0.5f);
        return std::clamp(value, -limit, limit);
    }

    void clamp_target() {
        target_x_ = clamped_center(target_x_, target_span_m_ * aspect_x_);
        target_z_ = clamped_center(target_z_, target_span_m_ * aspect_z_);
    }

    void clamp_current() {
        center_x_ = clamped_center(center_x_, span_m_ * aspect_x_);
        center_z_ = clamped_center(center_z_, span_m_ * aspect_z_);
    }

    float center_x_ = 0.0f;
    float center_z_ = 0.0f;
    float target_x_ = 0.0f;
    float target_z_ = 0.0f;
    float span_m_ = kFullSpanM;
    float target_span_m_ = kFullSpanM;
    float aspect_x_ = 1.0f;
    float aspect_z_ = 1.0f;
};

enum class MapLayer : uint8_t { Explore, Roads, Places };

class UiFlow {
public:
    UiScreen screen() const { return screen_; }
    UiScreen map_return_screen() const { return map_return_; }
    UiScreen settings_return_screen() const { return settings_return_; }
    MapLayer map_layer() const { return map_layer_; }
    SettingsPage settings_page() const { return settings_page_; }
    int settings_hovered_category() const { return settings_hovered_category_; }
    const UiSettings& settings() const { return settings_; }
    int selection() const { return selection_; }
    bool modal() const { return screen_ != UiScreen::Driving; }

    bool save_available() const { return save_available_; }
    void set_save_available(bool available) { save_available_ = available; set_selection(selection_); }
    void enter_game() { screen_ = UiScreen::Driving; selection_ = 0; }
    void show_title() { screen_ = UiScreen::Title; selection_ = 0; }

    int item_count() const {
        if (screen_ == UiScreen::Title) return save_available_ ? 5 : 4;
        if (screen_ == UiScreen::Pause) return 7;
        if (screen_ == UiScreen::Settings) return settings_item_count();
        return 0;
    }

    int settings_item_count() const {
        switch (settings_page_) {
            case SettingsPage::Root:          return 5;
            case SettingsPage::Map:           return 3;
            case SettingsPage::Graphics:      return 3;
            case SettingsPage::Audio:         return 3;
            case SettingsPage::Controls:      return 2;
            case SettingsPage::Accessibility: return 2;
        }
        return 0;
    }

    const char* settings_page_label(int index) const {
        static constexpr const char* kLabels[] = {
            "MAP", "GRAPHICS", "AUDIO", "CONTROLS", "ACCESSIBILITY"};
        return index >= 0 && index < 5 ? kLabels[index] : "";
    }

    const char* settings_label(int index) const {
        switch (settings_page_) {
            case SettingsPage::Root:
                return settings_page_label(index);
            case SettingsPage::Map: {
                static constexpr const char* kLabels[] = {
                    "MINIMAP POINTS OF INTEREST", "MISSION MARKER",
                    "WAYPOINT MARKER"};
                return index >= 0 && index < 3 ? kLabels[index] : "";
            }
            case SettingsPage::Graphics: {
                static constexpr const char* kLabels[] = {
                    "VSYNC", "CAMERA FOV", "WEATHER EFFECTS"};
                return index >= 0 && index < 3 ? kLabels[index] : "";
            }
            case SettingsPage::Audio: {
                static constexpr const char* kLabels[] = {
                    "MASTER VOLUME", "MUSIC VOLUME", "SFX VOLUME"};
                return index >= 0 && index < 3 ? kLabels[index] : "";
            }
            case SettingsPage::Controls: {
                static constexpr const char* kLabels[] = {
                    "CAMERA AUTO-RECENTER", "CAMERA SHAKE"};
                return index >= 0 && index < 2 ? kLabels[index] : "";
            }
            case SettingsPage::Accessibility: {
                static constexpr const char* kLabels[] = {
                    "SUBTITLES", "REDUCED MOTION"};
                return index >= 0 && index < 2 ? kLabels[index] : "";
            }
        }
        return "";
    }

    const char* settings_value(int index) const {
        static constexpr const char* kOnOff[] = {"OFF", "ON"};
        switch (settings_page_) {
            case SettingsPage::Root:
                return "";
            case SettingsPage::Map:
                if (index == 0) return kOnOff[settings_.minimap_points_of_interest];
                if (index == 1) return kOnOff[settings_.minimap_mission_marker];
                if (index == 2) return kOnOff[settings_.minimap_waypoint_marker];
                break;
            case SettingsPage::Graphics:
                if (index == 0) return kOnOff[settings_.vsync];
                if (index == 1) {
                    switch (settings_.camera_fov) {
                        case 70: return "70 DEG";
                        case 80: return "80 DEG";
                        default: return "60 DEG";
                    }
                }
                if (index == 2) return kOnOff[settings_.weather_effects];
                break;
            case SettingsPage::Audio: {
                static thread_local char value[16];
                const float gain = index == 0 ? settings_.master_volume :
                    index == 1 ? settings_.music_volume : settings_.sfx_volume;
                std::snprintf(value, sizeof(value), "%d%%",
                              static_cast<int>(gain * 100.0f + 0.5f));
                return value;
            }
            case SettingsPage::Controls:
                if (index == 0) return kOnOff[settings_.camera_auto_recenter];
                if (index == 1) return kOnOff[settings_.camera_shake];
                break;
            case SettingsPage::Accessibility:
                if (index == 0) return kOnOff[settings_.subtitles];
                if (index == 1) return kOnOff[settings_.reduced_motion];
                break;
        }
        return "";
    }

    void open_settings_page(SettingsPage page) {
        settings_page_ = page;
        settings_hovered_category_ = page == SettingsPage::Root
            ? -1 : static_cast<int>(page) - 1;
        set_selection(0);
    }

    void set_settings_hovered_category(int index) {
        settings_hovered_category_ = index >= 0 && index < 5 ? index : -1;
    }

    void clear_settings_hovered_category() {
        settings_hovered_category_ = -1;
    }

    void set_selection(int index) {
        const int count = item_count();
        if (count <= 0) {
            selection_ = 0;
            return;
        }
        selection_ = index % count;
        if (selection_ < 0) selection_ += count;
    }

    UiAction update(uint32_t pressed) {
        switch (screen_) {
            case UiScreen::Driving:
                if ((pressed & kBtnMap) != 0u) {
                    open_map(UiScreen::Driving);
                } else if ((pressed & (kBtnPause | kBtnBack)) != 0u) {
                    screen_ = UiScreen::Pause;
                    selection_ = 0;
                }
                return UiAction::None;

            case UiScreen::Map:
                if ((pressed & kBtnCamCycle) != 0u) {
                    map_layer_ = static_cast<MapLayer>(
                        (static_cast<int>(map_layer_) + 1) % 3);
                }
                if ((pressed & (kBtnMap | kBtnBack | kBtnPause)) != 0u) {
                    screen_ = map_return_;
                    selection_ = 0;
                }
                return UiAction::None;

            case UiScreen::Settings:
                return update_settings(pressed);

            case UiScreen::Title:
            case UiScreen::Pause:
                break;
        }

        if ((pressed & kBtnMap) != 0u) {
            open_map(screen_);
            return UiAction::None;
        }

        if ((pressed & kBtnMenuUp) != 0u) set_selection(selection_ - 1);
        if ((pressed & kBtnMenuDown) != 0u) set_selection(selection_ + 1);

        if ((pressed & kBtnBack) != 0u) {
            if (screen_ == UiScreen::Pause) screen_ = UiScreen::Driving;
            return UiAction::None;
        }
        if (screen_ == UiScreen::Pause && (pressed & kBtnPause) != 0u) {
            screen_ = UiScreen::Driving;
            return UiAction::None;
        }
        if ((pressed & kBtnAccept) == 0u) return UiAction::None;

        if (screen_ == UiScreen::Title) {
            if (save_available_ && selection_ == 0) return UiAction::LoadGame;
            const int index = selection_ - (save_available_ ? 1 : 0);
            if (index == 0) {
                screen_ = UiScreen::Driving;
                return UiAction::NewGame;
            }
            if (index == 1) { open_map(UiScreen::Title); return UiAction::None; }
            if (index == 2) { open_settings(UiScreen::Title); return UiAction::None; }
            return UiAction::QuitGame;
        }

        // Save/load stay paused until the host confirms successful loading.
        if (selection_ == 0) enter_game();
        else if (selection_ == 1) return UiAction::SaveGame;
        else if (selection_ == 2) return UiAction::LoadGame;
        else if (selection_ == 3) open_map(UiScreen::Pause);
        else if (selection_ == 4) { enter_game(); return UiAction::ResetVehicle; }
        else if (selection_ == 5) open_settings(UiScreen::Pause);
        else show_title();
        return UiAction::None;
    }

private:
    UiAction update_settings(uint32_t pressed) {
        if ((pressed & kBtnBack) != 0u) {
            if (settings_page_ == SettingsPage::Root) {
                screen_ = settings_return_;
                selection_ = 0;
            } else {
                settings_page_ = SettingsPage::Root;
                selection_ = 0;
            }
            return UiAction::None;
        }
        if ((pressed & kBtnMenuUp) != 0u) set_selection(selection_ - 1);
        if ((pressed & kBtnMenuDown) != 0u) set_selection(selection_ + 1);

        if (settings_page_ == SettingsPage::Root) {
            if ((pressed & kBtnAccept) != 0u) {
                open_settings_page(static_cast<SettingsPage>(selection_ + 1));
            }
            return UiAction::None;
        }

        const int direction = (pressed & kBtnMenuRight) != 0u ? 1 :
            (pressed & kBtnMenuLeft) != 0u ? -1 : 0;
        if ((pressed & kBtnAccept) != 0u && direction == 0) {
            adjust_setting(1);
        } else if (direction != 0) {
            adjust_setting(direction);
        }
        return UiAction::None;
    }

    void adjust_setting(int direction) {
        const bool flip = direction != 0;
        switch (settings_page_) {
            case SettingsPage::Map:
                if (selection_ == 0 && flip) settings_.minimap_points_of_interest = !settings_.minimap_points_of_interest;
                if (selection_ == 1 && flip) settings_.minimap_mission_marker = !settings_.minimap_mission_marker;
                if (selection_ == 2 && flip) settings_.minimap_waypoint_marker = !settings_.minimap_waypoint_marker;
                break;
            case SettingsPage::Graphics:
                if (selection_ == 0 && flip) settings_.vsync = !settings_.vsync;
                if (selection_ == 1 && flip) {
                    static constexpr int kFov[] = {60, 70, 80};
                    int current = settings_.camera_fov == 80 ? 2 :
                        settings_.camera_fov == 70 ? 1 : 0;
                    current = (current + (direction > 0 ? 1 : 2)) % 3;
                    settings_.camera_fov = kFov[current];
                }
                if (selection_ == 2 && flip) settings_.weather_effects = !settings_.weather_effects;
                break;
            case SettingsPage::Audio: {
                float* value = selection_ == 0 ? &settings_.master_volume :
                    selection_ == 1 ? &settings_.music_volume : &settings_.sfx_volume;
                if (direction == 0) direction = *value > 0.0f ? -1 : 1;
                *value = std::clamp(*value + static_cast<float>(direction) * 0.1f,
                                    0.0f, 1.0f);
                break;
            }
            case SettingsPage::Controls:
                if (selection_ == 0 && flip) settings_.camera_auto_recenter = !settings_.camera_auto_recenter;
                if (selection_ == 1 && flip) settings_.camera_shake = !settings_.camera_shake;
                break;
            case SettingsPage::Accessibility:
                if (selection_ == 0 && flip) settings_.subtitles = !settings_.subtitles;
                if (selection_ == 1 && flip) settings_.reduced_motion = !settings_.reduced_motion;
                break;
            case SettingsPage::Root:
                break;
        }
    }

    void open_map(UiScreen return_to) {
        map_return_ = return_to;
        screen_ = UiScreen::Map;
        selection_ = 0;
    }

    void open_settings(UiScreen return_to) {
        settings_return_ = return_to;
        settings_page_ = SettingsPage::Root;
        settings_hovered_category_ = -1;
        screen_ = UiScreen::Settings;
        selection_ = 0;
    }

    bool save_available_ = false;
    UiScreen screen_ = UiScreen::Title;
    UiScreen map_return_ = UiScreen::Title;
    UiScreen settings_return_ = UiScreen::Title;
    MapLayer map_layer_ = MapLayer::Explore;
    SettingsPage settings_page_ = SettingsPage::Root;
    int settings_hovered_category_ = -1;
    UiSettings settings_;
    int selection_ = 0;
};

}  // namespace apricot
