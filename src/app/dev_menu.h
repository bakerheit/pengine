#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include "app/driving_mechanics.h"
#include "app/player_car_catalog.h"
#include "audio/vehicle_audio.h"
#include "core/input_frame.h"
#include "city/tacomaco.h"
#include "city/florangia_airport.h"

namespace apricot {

// Host-only development menu state. F1 and the teleport itself deliberately
// stay outside InputFrame: debug actions must never become part of a replay
// tape. The navigation bits are only read while this modal menu owns input.
struct DevTeleportLocation {
    const char* name = nullptr;
    glm::vec2 world_xz{0.0f};
    float heading_radians = 0.0f;
};

enum class DevWeatherPreset : uint8_t {
    Clear,
    Overcast,
    Rain,
    Storm,
    Thunderstorm,
    Snow,
    Blizzard,
    Tornado,
    Flood,
    Hail,
    Heatwave,
    Dynamic,
    kCount,
};
enum class DevTimePreset : uint8_t {
    Live,
    Midnight,
    Dawn,
    Morning,
    Noon,
    Afternoon,
    Dusk,
    Night,
    kCount,
};

inline constexpr std::array<const char*, 12> kDevWeatherLabels{{
    "CLEAR", "OVERCAST", "RAIN", "STORM", "THUNDERSTORM", "SNOW",
    "BLIZZARD", "TORNADO", "FLOOD", "HAIL", "HEATWAVE", "DYNAMIC",
}};
inline constexpr std::array<const char*, 8> kDevTimeLabels{{
    "LIVE", "MIDNIGHT", "DAWN", "MORNING", "NOON", "AFTERNOON", "DUSK",
    "NIGHT",
}};
inline constexpr std::array<float, 8> kDevTimeValues{{
    -1.0f, 0.0f, 0.25f, 0.35f, 0.5f, 0.63f, 0.75f, 0.88f,
}};
inline constexpr std::array<float, 10> kDevSnowDepthValues{{
    -1.0f, 0.0f, 0.01f, 0.03f, 0.06f, 0.12f, 0.25f, 0.50f, 1.00f,
    1.50f,
}};
inline constexpr std::array<const char*, 10> kDevSnowDepthLabels{{
    "AUTO (WEATHER)", "NONE", "1 CM", "3 CM", "6 CM", "12 CM",
    "25 CM", "50 CM", "1.00 M", "1.50 M",
}};
inline constexpr std::array<const char*, 3> kDevCameraModeLabels{{
    "NEAR", "CHASE", "FAR",
}};
inline constexpr std::array<const char*, 6> kDevWantedLevelLabels{{
    "CLEAR", "1 STAR", "2 STARS", "3 STARS", "4 STARS", "5 STARS",
}};

// Safe, authored arrival points rather than raw landmark centres. A landmark
// may be inside the thing it names; these points sit on its forecourt, road or
// flight surface so spawn_vehicle can place the car cleanly.
inline constexpr std::array<DevTeleportLocation, 17> kDevTeleportLocations{{
    {"HALLOWAY GAS", {0.0f, 0.0f}, 0.0f},
    {"HALLOWAY CAR WASH", {3.05f, -63.12f}, 0.0f},
    {"CAUSEWAY COURT MOTEL", {-75.5f, 1.0f}, 0.0f},
    {"QUICKBITE GRILL", {14.0f, 33.5f}, 0.0f},
    {"TACOMACO", {
        city::kTacomacoSite.origin.x + city::kTacomacoSite.cos_yaw * 14.0f +
            city::kTacomacoSite.sin_yaw * -13.5f,
        city::kTacomacoSite.origin.z - city::kTacomacoSite.sin_yaw * 14.0f +
            city::kTacomacoSite.cos_yaw * -13.5f}, 0.0f},
    {"HALLOWAY FLATS", {114.0f, 16.0f}, 0.0f},
    {"OSTEND BAIT & TACKLE", {-2033.0f, -600.0f}, 1.745329252f},
    {"O'HAVEN AIRPORT DROPOFF", {115.0f, 2300.0f}, -1.5707963268f},
    {"O'HAVEN AIRPORT APRON", {60.0f, 2195.0f}, -1.5707963268f},
    {"CAMBER GATEWAY HOTEL", {-200.0f, 2360.0f}, 3.1415926536f},
    {"O'HAVEN RENTAL CENTRE", {415.0f, 2360.0f}, 3.1415926536f},
    {"CAMBER AIR CARGO", {520.0f, 2355.0f}, 3.1415926536f},
    {"AIRPORT SECURITY GATE", {-348.0f, 2170.4f}, -1.5707963268f},
    {"O'HAVEN AIRPORT ARRIVAL", {650.0f, 2390.0f}, 1.5707963268f},
    {"FLORANGIA PALM COAST", {4670.0f, 5100.0f}, 0.7853981634f},
    {"FLORANGIA REGIONAL AIRPORT", {4800.0f, 4685.0f}, 3.1415926536f},
    {"O'HAVEN RUNWAY 09", {-260.0f, 2046.0f}, -1.5707963268f},
}};

// The teleport page is the tallest developer page. Keep its panel inside the
// 720-point UI canvas by tightening rows only when the preferred 43-point
// spacing would push the footer off-screen.
struct DevMenuPanelLayout {
    float top = 56.0f;
    float header_height = 78.0f;
    float footer_height = 50.0f;
    float row_height = 43.0f;
    float body_height = 0.0f;
    float bottom = 0.0f;
};

inline DevMenuPanelLayout dev_menu_panel_layout(int item_count,
                                                 float viewport_height) {
    DevMenuPanelLayout out;
    constexpr float kBottomMargin = 20.0f;
    const float available = viewport_height - out.top - out.header_height -
                            out.footer_height - kBottomMargin;
    if (item_count > 0) {
        const float fitted = available / static_cast<float>(item_count);
        if (fitted < out.row_height)
            out.row_height = fitted > 24.0f ? fitted : 24.0f;
    }
    out.body_height = out.row_height * static_cast<float>(item_count);
    out.bottom = out.top + out.header_height + out.body_height +
                 out.footer_height;
    return out;
}

enum class DevMenuPage : uint8_t {
    Root,
    Teleport,
    DrivingMechanics,
    Vehicle,
    VehicleBrands,
    VehicleModels,
    WeatherTime,
    Weather,
    Time,
    SnowDepth,
    Camera,
    Wanted,
    SoundTesting,
    SoundTestingCar,
    SoundTestingCarOptions,
};

enum class DevMenuActionKind : uint8_t {
    None,
    ReportBug,
    Teleport,
    TeleportWaypoint,
    SetDrivingMechanics,
    SetPlayerCar,
    RepairVehicle,
    CopyPlayerPosition,
    SetWantedLevel,
    SetWeather,
    SetTime,
    SetSnowDepth,
    SetCameraMode,
    SetCameraAutoRecenter,
    AuditionCarSound,
};

struct DevMenuAction {
    DevMenuActionKind kind = DevMenuActionKind::None;
    int location_index = -1;
    DrivingMechanicsStyle driving_mechanics =
        DrivingMechanicsStyle::ClassicGta;
    PlayerCarId player_car = PlayerCarId::LegacyCar5;
    CarSoundUse car_sound_use = CarSoundUse::Accelerate;
    int sound_variant = 0;
    DevWeatherPreset weather = DevWeatherPreset::Dynamic;
    DevTimePreset time = DevTimePreset::Live;
    // Negative restores automatic accumulation; otherwise this is a fixed
    // developer override in metres.
    float snow_depth_m = -1.0f;
    int camera_mode = 1;
    bool camera_auto_recenter = true;
    int wanted_level = 0;
};

inline constexpr std::array<const char*, kCarSoundUseCount> kCarSoundUseLabels{{
    "ACCELERATE  >", "BRAKE  >", "CRASH  >", "TYRES  >", "SURFACE  >",
}};

inline constexpr std::array<std::array<const char*, kCarSoundVariantCount>,
                            kCarSoundUseCount>
    kCarSoundVariantLabels{{
        {{"GEAR 1", "GEAR 2", "GEAR 3", "GEAR 4", "GEAR 5 / 6"}},
        {{"WINTER TYRES", "HEAVY BRAKE", "BRAKE RUN", "HANDBRAKE", "GRAVEL STOP"}},
        {{"GLASS CRASH", "FAST HIT", "BODY HIT", "CAR CRASH", "SQUEAL + CRASH"}},
        {{"CHRYSLER SQUEAL 1", "CHRYSLER SQUEAL 2", "CHRYSLER SQUEAL 3", "VOLVO TURN", "MAXIMA BURNOUT"}},
        {{"ASPHALT", "GRAVEL", "DIRT", "WET ROAD", "COBBLESTONE"}},
    }};

class DevMenu {
public:
    bool open() const { return open_; }
    DevMenuPage page() const { return page_; }
    int selection() const { return selection_; }
    DrivingMechanicsStyle driving_mechanics() const {
        return driving_mechanics_;
    }
    PlayerCarId player_car() const { return player_car_; }
    DevWeatherPreset weather() const { return weather_; }
    void set_weather(DevWeatherPreset preset) { weather_=preset; }
    DevTimePreset time() const { return time_; }
    float snow_depth_m() const { return snow_depth_m_; }
    void set_snow_depth_m(float depth_m) {
        snow_depth_m_ = depth_m < 0.0f ? -1.0f :
            std::clamp(depth_m, 0.0f, 1.5f);
    }
    int camera_mode() const { return camera_mode_; }
    bool camera_auto_recenter() const { return camera_auto_recenter_; }
    int wanted_level() const { return wanted_level_; }

    void set_waypoint_available(bool available) {
        waypoint_available_ = available;
    }

    void set_wanted_level(int level) {
        wanted_level_ = std::clamp(level, 0, 5);
    }

    void set_driving_mechanics(DrivingMechanicsStyle style) {
        driving_mechanics_ = style;
    }

    void set_player_car(PlayerCarId car) { player_car_ = canonical_player_car_id(car); }

    void set_camera_mode(int mode) {
        camera_mode_ = mode % static_cast<int>(kDevCameraModeLabels.size());
        if (camera_mode_ < 0) {
            camera_mode_ += static_cast<int>(kDevCameraModeLabels.size());
        }
    }

    void set_camera_auto_recenter(bool enabled) {
        camera_auto_recenter_ = enabled;
    }

    void toggle() {
        if (open_) {
            close();
            return;
        }
        open_ = true;
        page_ = DevMenuPage::Root;
        selection_ = 0;
    }

    void close() {
        open_ = false;
        page_ = DevMenuPage::Root;
        selection_ = 0;
    }

    int item_count() const {
        if (page_ == DevMenuPage::Root) return 7;
        if (page_ == DevMenuPage::Teleport) {
            return static_cast<int>(kDevTeleportLocations.size()) +
                   (waypoint_available_ ? 1 : 0);
        }
        if (page_ == DevMenuPage::DrivingMechanics) {
            return static_cast<int>(kDrivingMechanicsStyleCount);
        }
        if (page_ == DevMenuPage::Vehicle) return 4;
        if (page_ == DevMenuPage::WeatherTime) return 3;
        if (page_ == DevMenuPage::Weather) {
            return static_cast<int>(DevWeatherPreset::kCount);
        }
        if (page_ == DevMenuPage::Time) {
            return static_cast<int>(DevTimePreset::kCount);
        }
        if (page_ == DevMenuPage::SnowDepth) {
            return static_cast<int>(kDevSnowDepthValues.size());
        }
        if (page_ == DevMenuPage::Camera) return 2;
        if (page_ == DevMenuPage::Wanted) {
            return static_cast<int>(kDevWantedLevelLabels.size());
        }
        if (page_ == DevMenuPage::VehicleBrands) {
            return static_cast<int>(kPlayerCarBrands.size());
        }
        if (page_ == DevMenuPage::VehicleModels) {
            return static_cast<int>(
                kPlayerCarBrands[static_cast<std::size_t>(brand_index_)]
                    .car_count);
        }
        if (page_ == DevMenuPage::SoundTesting) return 1;
        if (page_ == DevMenuPage::SoundTestingCar) {
            return static_cast<int>(kCarSoundUseCount);
        }
        if (page_ == DevMenuPage::SoundTestingCarOptions) {
            return kCarSoundVariantCount;
        }
        return 1;
    }

    const char* title() const {
        if (page_ == DevMenuPage::Root) return "DEVELOPER";
        if (page_ == DevMenuPage::Teleport) return "TELEPORT";
        if (page_ == DevMenuPage::DrivingMechanics) {
            return "DRIVING MECHANICS";
        }
        if (page_ == DevMenuPage::VehicleBrands) return "CHOOSE BRAND";
        if (page_ == DevMenuPage::VehicleModels) {
            return kPlayerCarBrands[static_cast<std::size_t>(brand_index_)].name;
        }
        if (page_ == DevMenuPage::WeatherTime) return "WEATHER & TIME";
        if (page_ == DevMenuPage::Weather) return "WEATHER";
        if (page_ == DevMenuPage::Time) return "TIME OF DAY";
        if (page_ == DevMenuPage::SnowDepth) return "SNOW ACCUMULATION";
        if (page_ == DevMenuPage::Camera) return "CAMERA";
        if (page_ == DevMenuPage::Wanted) return "WANTED LEVEL";
        if (page_ == DevMenuPage::SoundTesting) return "SOUND TESTING";
        if (page_ == DevMenuPage::SoundTestingCar) return "SOUND TEST / CAR";
        if (page_ == DevMenuPage::SoundTestingCarOptions) {
            switch (car_sound_use_) {
                case CarSoundUse::Accelerate: return "CAR / ACCELERATE";
                case CarSoundUse::Brake:      return "CAR / BRAKE";
                case CarSoundUse::Crash:      return "CAR / CRASH";
                case CarSoundUse::Tyres:      return "CAR / TYRES";
                case CarSoundUse::Surface:    return "CAR / SURFACE";
                case CarSoundUse::kCount:     break;
            }
        }
        return "PLAYER & VEHICLE";
    }

    const char* item_label(int index) const {
        if (page_ == DevMenuPage::Root) {
            if (index == 0) return "TELEPORT  >";
            if (index == 1) return "DRIVING MECHANICS  >";
            if (index == 2) return "PLAYER & VEHICLE  >";
            if (index == 3) return "WEATHER & TIME  >";
            if (index == 4) return "CAMERA  >";
            if (index == 5) return "SOUND TESTING  >";
            if (index == 6) return "REPORT BUG  [F2]";
            return "";
        }
        if (index < 0 || index >= item_count()) return "";
        if (page_ == DevMenuPage::Teleport) {
            if (waypoint_available_ && index == 0) return "MAP WAYPOINT";
            const int location_index = index - (waypoint_available_ ? 1 : 0);
            return kDevTeleportLocations[
                static_cast<std::size_t>(location_index)].name;
        }
        if (page_ == DevMenuPage::DrivingMechanics) {
            return driving_mechanics_menu_label(
                static_cast<DrivingMechanicsStyle>(index));
        }
        if (page_ == DevMenuPage::Vehicle) {
            if (index == 0) return "CHOOSE CAR  >";
            if (index == 1) return "REPAIR";
            if (index == 2) return "COPY POSITION";
            return "WANTED LEVEL  >";
        }
        if (page_ == DevMenuPage::WeatherTime) {
            if (index == 0) return "WEATHER  >";
            if (index == 1) return "TIME OF DAY  >";
            return "SNOW ACCUMULATION  >";
        }
        if (page_ == DevMenuPage::Weather) {
            return kDevWeatherLabels[static_cast<std::size_t>(index)];
        }
        if (page_ == DevMenuPage::Time) {
            return kDevTimeLabels[static_cast<std::size_t>(index)];
        }
        if (page_ == DevMenuPage::SnowDepth) {
            return kDevSnowDepthLabels[static_cast<std::size_t>(index)];
        }
        if (page_ == DevMenuPage::Camera) {
            return index == 0 ? "VIEW MODE" : "AUTO RECENTER";
        }
        if (page_ == DevMenuPage::Wanted) {
            return kDevWantedLevelLabels[static_cast<std::size_t>(index)];
        }
        if (page_ == DevMenuPage::VehicleBrands) {
            return kPlayerCarBrands[static_cast<std::size_t>(index)].menu_label;
        }
        if (page_ == DevMenuPage::VehicleModels) {
            const PlayerCarBrand& brand =
                kPlayerCarBrands[static_cast<std::size_t>(brand_index_)];
            return kPlayerCars[brand.first_car +
                               static_cast<std::size_t>(index)].model;
        }
        if (page_ == DevMenuPage::SoundTesting) return "CAR  >";
        if (page_ == DevMenuPage::SoundTestingCar) {
            return kCarSoundUseLabels[static_cast<std::size_t>(index)];
        }
        if (page_ == DevMenuPage::SoundTestingCarOptions) {
            return kCarSoundVariantLabels[static_cast<std::size_t>(car_sound_use_)]
                                          [static_cast<std::size_t>(index)];
        }
        return "";
    }

    const char* item_value(int index) const {
        if (page_ == DevMenuPage::Root && index == 1) {
            return driving_mechanics_name(driving_mechanics_);
        }
        if (page_ == DevMenuPage::DrivingMechanics &&
            index == static_cast<int>(driving_mechanics_)) {
            return "ACTIVE";
        }
        if (page_ == DevMenuPage::Root && index == 2) {
            return player_car_definition(player_car_).model;
        }
        if (page_ == DevMenuPage::Vehicle && index == 0) {
            return player_car_definition(player_car_).model;
        }
        if (page_ == DevMenuPage::Vehicle && index == 3) {
            return kDevWantedLevelLabels[
                static_cast<std::size_t>(wanted_level_)];
        }
        if (page_ == DevMenuPage::VehicleModels) {
            const PlayerCarBrand& brand =
                kPlayerCarBrands[static_cast<std::size_t>(brand_index_)];
            const PlayerCarId id = kPlayerCars[
                brand.first_car + static_cast<std::size_t>(index)].id;
            if (id == player_car_) return "ACTIVE";
        }
        if (page_ == DevMenuPage::WeatherTime && index == 0) {
            return kDevWeatherLabels[static_cast<std::size_t>(weather_)];
        }
        if (page_ == DevMenuPage::WeatherTime && index == 1) {
            return kDevTimeLabels[static_cast<std::size_t>(time_)];
        }
        if (page_ == DevMenuPage::WeatherTime && index == 2) {
            for (std::size_t i = 0; i < kDevSnowDepthValues.size(); ++i) {
                if (std::fabs(kDevSnowDepthValues[i] - snow_depth_m_) <
                    0.0001f) {
                    return kDevSnowDepthLabels[i];
                }
            }
            return "CUSTOM";
        }
        if (page_ == DevMenuPage::Weather &&
            index == static_cast<int>(weather_)) {
            return "ACTIVE";
        }
        if (page_ == DevMenuPage::Time && index == static_cast<int>(time_)) {
            return "ACTIVE";
        }
        if (page_ == DevMenuPage::SnowDepth &&
            std::fabs(kDevSnowDepthValues[static_cast<std::size_t>(index)] -
                      snow_depth_m_) < 0.0001f) {
            return "ACTIVE";
        }
        if (page_ == DevMenuPage::Camera && index == 0) {
            return kDevCameraModeLabels[static_cast<std::size_t>(camera_mode_)];
        }
        if (page_ == DevMenuPage::Camera && index == 1) {
            return camera_auto_recenter_ ? "ON" : "OFF";
        }
        if (page_ == DevMenuPage::Wanted && index == wanted_level_) {
            return "ACTIVE";
        }
        return "";
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

    DevMenuAction update(uint32_t pressed) {
        if (!open_) return {};

        if ((pressed & kBtnMenuUp) != 0u) set_selection(selection_ - 1);
        if ((pressed & kBtnMenuDown) != 0u) set_selection(selection_ + 1);

        if ((pressed & kBtnBack) != 0u) {
            if (page_ != DevMenuPage::Root) {
                const DevMenuPage old_page = page_;
                if (old_page == DevMenuPage::SoundTestingCarOptions) {
                    page_ = DevMenuPage::SoundTestingCar;
                    selection_ = static_cast<int>(car_sound_use_);
                } else if (old_page == DevMenuPage::SoundTestingCar) {
                    page_ = DevMenuPage::SoundTesting;
                    selection_ = 0;
                } else if (old_page == DevMenuPage::SoundTesting) {
                    page_ = DevMenuPage::Root;
                    selection_ = 5;
                } else if (old_page == DevMenuPage::DrivingMechanics) {
                    page_ = DevMenuPage::Root;
                    selection_ = 1;
                } else if (old_page == DevMenuPage::Weather ||
                           old_page == DevMenuPage::Time ||
                           old_page == DevMenuPage::SnowDepth) {
                    page_ = DevMenuPage::WeatherTime;
                    selection_ = old_page == DevMenuPage::Weather ? 0 :
                        (old_page == DevMenuPage::Time ? 1 : 2);
                } else if (old_page == DevMenuPage::WeatherTime) {
                    page_ = DevMenuPage::Root;
                    selection_ = 3;
                } else if (old_page == DevMenuPage::Camera) {
                    page_ = DevMenuPage::Root;
                    selection_ = 4;
                } else if (old_page == DevMenuPage::Wanted) {
                    page_ = DevMenuPage::Vehicle;
                    selection_ = 3;
                } else if (old_page == DevMenuPage::VehicleModels) {
                    page_ = DevMenuPage::VehicleBrands;
                    selection_ = brand_index_;
                } else if (old_page == DevMenuPage::VehicleBrands) {
                    page_ = DevMenuPage::Vehicle;
                    selection_ = 0;
                } else if (old_page == DevMenuPage::Vehicle) {
                    page_ = DevMenuPage::Root;
                    selection_ = 2;
                } else {
                    page_ = DevMenuPage::Root;
                    selection_ = 0;
                }
            } else {
                close();
            }
            return {};
        }

        if ((pressed & kBtnAccept) == 0u) return {};
        if (page_ == DevMenuPage::Root) {
            if (selection_ == 0) {
                page_ = DevMenuPage::Teleport;
            } else if (selection_ == 1) {
                page_ = DevMenuPage::DrivingMechanics;
            } else if (selection_ == 2) {
                page_ = DevMenuPage::Vehicle;
            } else if (selection_ == 3) {
                page_ = DevMenuPage::WeatherTime;
            } else if (selection_ == 4) {
                page_ = DevMenuPage::Camera;
            } else if (selection_ == 5) {
                page_ = DevMenuPage::SoundTesting;
            } else {
                DevMenuAction action;
                action.kind = DevMenuActionKind::ReportBug;
                return action;
            }
            selection_ = 0;
            return {};
        }
        if (page_ == DevMenuPage::Teleport) {
            if (waypoint_available_ && selection_ == 0) {
                DevMenuAction action;
                action.kind = DevMenuActionKind::TeleportWaypoint;
                return action;
            }
            DevMenuAction action;
            action.kind = DevMenuActionKind::Teleport;
            action.location_index =
                selection_ - (waypoint_available_ ? 1 : 0);
            return action;
        }
        if (page_ == DevMenuPage::DrivingMechanics) {
            driving_mechanics_ =
                static_cast<DrivingMechanicsStyle>(selection_);
            DevMenuAction action;
            action.kind = DevMenuActionKind::SetDrivingMechanics;
            action.driving_mechanics = driving_mechanics_;
            return action;
        }
        if (page_ == DevMenuPage::Vehicle) {
            if (selection_ == 0) {
                page_ = DevMenuPage::VehicleBrands;
                selection_ = player_car_brand_index(player_car_);
                return {};
            }
            DevMenuAction action;
            if (selection_ == 1) {
                action.kind = DevMenuActionKind::RepairVehicle;
            } else if (selection_ == 2) {
                action.kind = DevMenuActionKind::CopyPlayerPosition;
            } else {
                page_ = DevMenuPage::Wanted;
                selection_ = wanted_level_;
                return {};
            }
            return action;
        }
        if (page_ == DevMenuPage::VehicleBrands) {
            brand_index_ = selection_;
            const PlayerCarBrand& brand =
                kPlayerCarBrands[static_cast<std::size_t>(brand_index_)];
            std::size_t active = 0;
            while (active < kPlayerCars.size() && kPlayerCars[active].id != player_car_) ++active;
            selection_ = active >= brand.first_car &&
                         active < brand.first_car + brand.car_count
                ? static_cast<int>(active - brand.first_car) : 0;
            page_ = DevMenuPage::VehicleModels;
            return {};
        }
        if (page_ == DevMenuPage::VehicleModels) {
            const PlayerCarBrand& brand =
                kPlayerCarBrands[static_cast<std::size_t>(brand_index_)];
            player_car_ = kPlayerCars[
                brand.first_car + static_cast<std::size_t>(selection_)].id;
            DevMenuAction action;
            action.kind = DevMenuActionKind::SetPlayerCar;
            action.player_car = player_car_;
            return action;
        }
        if (page_ == DevMenuPage::WeatherTime) {
            if (selection_ == 0) {
                page_ = DevMenuPage::Weather;
                selection_ = static_cast<int>(weather_);
            } else if (selection_ == 1) {
                page_ = DevMenuPage::Time;
                selection_ = static_cast<int>(time_);
            } else {
                page_ = DevMenuPage::SnowDepth;
                selection_ = 0;
                for (std::size_t i = 0; i < kDevSnowDepthValues.size(); ++i) {
                    if (std::fabs(kDevSnowDepthValues[i] - snow_depth_m_) <
                        std::fabs(kDevSnowDepthValues[
                            static_cast<std::size_t>(selection_)] - snow_depth_m_)) {
                        selection_ = static_cast<int>(i);
                    }
                }
            }
            return {};
        }
        if (page_ == DevMenuPage::Weather) {
            weather_ = static_cast<DevWeatherPreset>(selection_);
            DevMenuAction action;
            action.kind = DevMenuActionKind::SetWeather;
            action.weather = weather_;
            return action;
        }
        if (page_ == DevMenuPage::Time) {
            time_ = static_cast<DevTimePreset>(selection_);
            DevMenuAction action;
            action.kind = DevMenuActionKind::SetTime;
            action.time = time_;
            return action;
        }
        if (page_ == DevMenuPage::SnowDepth) {
            snow_depth_m_ =
                kDevSnowDepthValues[static_cast<std::size_t>(selection_)];
            DevMenuAction action;
            action.kind = DevMenuActionKind::SetSnowDepth;
            action.snow_depth_m = snow_depth_m_;
            return action;
        }
        if (page_ == DevMenuPage::Camera) {
            DevMenuAction action;
            if (selection_ == 0) {
                set_camera_mode(camera_mode_ + 1);
                action.kind = DevMenuActionKind::SetCameraMode;
                action.camera_mode = camera_mode_;
            } else {
                camera_auto_recenter_ = !camera_auto_recenter_;
                action.kind = DevMenuActionKind::SetCameraAutoRecenter;
                action.camera_auto_recenter = camera_auto_recenter_;
            }
            return action;
        }
        if (page_ == DevMenuPage::Wanted) {
            wanted_level_ = selection_;
            DevMenuAction action;
            action.kind = DevMenuActionKind::SetWantedLevel;
            action.wanted_level = wanted_level_;
            return action;
        }
        if (page_ == DevMenuPage::SoundTesting) {
            page_ = DevMenuPage::SoundTestingCar;
            selection_ = 0;
            return {};
        }
        if (page_ == DevMenuPage::SoundTestingCar) {
            car_sound_use_ = static_cast<CarSoundUse>(selection_);
            page_ = DevMenuPage::SoundTestingCarOptions;
            selection_ = 0;
            return {};
        }
        if (page_ == DevMenuPage::SoundTestingCarOptions) {
            DevMenuAction action;
            action.kind = DevMenuActionKind::AuditionCarSound;
            action.car_sound_use = car_sound_use_;
            action.sound_variant = selection_;
            return action;
        }

        return {};
    }

private:
    bool open_ = false;
    DevMenuPage page_ = DevMenuPage::Root;
    int selection_ = 0;
    DrivingMechanicsStyle driving_mechanics_ =
        DrivingMechanicsStyle::ClassicGta;
    PlayerCarId player_car_ = PlayerCarId::LegacyCar5;
    DevWeatherPreset weather_ = DevWeatherPreset::Dynamic;
    DevTimePreset time_ = DevTimePreset::Live;
    float snow_depth_m_ = -1.0f;
    int camera_mode_ = 1;
    bool camera_auto_recenter_ = true;
    bool waypoint_available_ = false;
    int wanted_level_ = 0;
    int brand_index_ = 0;
    CarSoundUse car_sound_use_ = CarSoundUse::Accelerate;
};

}  // namespace apricot
