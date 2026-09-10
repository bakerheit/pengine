#include "audio/player_car_assets.h"

#include <cstddef>
#include <string>

#include "core/asset_root.h"

namespace apricot {

SfxOverridePaths player_car_audio_overrides() {
    SfxOverridePaths paths;
    paths.engine_start = asset_path("audio/vehicles/player/runtime/engine_start.wav");
    paths.engine_idle = asset_path("audio/vehicles/player/runtime/engine_idle.wav");
    constexpr const char* horns[]={"horn_short.wav","horn_double.wav","horn_truck.wav"};
    static_assert(sizeof(horns)/sizeof(horns[0])==kTrafficHornClipCount);
    for (std::size_t i=0; i<kTrafficHornClipCount; ++i)
        paths.traffic_horns[i]=asset_path(std::string("audio/vehicles/traffic/runtime/")+horns[i]);
    paths.player_throttle_attack = asset_path(
        "audio/vehicles/player/runtime/throttle_attack.wav");
    paths.player_throttle_hold = asset_path(
        "audio/vehicles/player/runtime/throttle_hold.wav");
    paths.player_throttle_release = asset_path(
        "audio/vehicles/player/runtime/throttle_release.wav");
    paths.player_car_collision = asset_path(
        "audio/vehicles/player/runtime/car_collision.wav");
    paths.player_drift_tyres = asset_path(
        "audio/vehicles/player/runtime/tyre_screech_loop.wav");
    paths.player_burnout = asset_path("audio/vehicles/player/runtime/burnout_loop.wav");
    paths.city_ambience = asset_path("audio/world/city_ambience.wav");
    paths.rain = asset_path("audio/world/light_rain_loop.wav");
    paths.mission_success = asset_path("audio/ui/mission-success.wav");
    constexpr const char* groups[] = {
        "accelerate", "brake", "crash", "tyres", "surface",
    };
    static_assert(sizeof(groups) / sizeof(groups[0]) == kCarSoundUseCount);
    for (std::size_t use = 0; use < kCarSoundUseCount; ++use) {
        for (int variant = 0; variant < kCarSoundVariantCount; ++variant) {
            paths.car_sound_audition[use][static_cast<std::size_t>(variant)] =
                asset_path("audio/vehicles/player/auditions/" +
                           std::string(groups[use]) + "_" +
                           std::to_string(variant) + ".wav");
        }
    }
    return paths;
}

}  // namespace apricot
