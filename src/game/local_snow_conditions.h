#pragma once

#include "game/conditions.h"
#include "game/snowpack.h"

namespace apricot {

// Keep rain, hail, water and night effects while replacing only the snow
// under the vehicle. Recompute from the losses to preserve the grip clamp.
inline Conditions conditions_with_local_snow(const Conditions& weather,
                                             float depth_m) {
    Conditions local = weather;
    SnowpackState pack;
    pack.set_depth_m(depth_m);
    local.snow_depth_m = static_cast<float>(pack.depth_m());
    local.snow_cover = visual_snow_cover_from_depth(pack.depth_m());
    const float night = std::clamp(-local.sun_elevation * 2.0f, 0.0f, 1.0f);
    const float loss = kWetGripLoss * local.wetness +
        kRainGripLoss * local.rain + kSnowGripLoss * local.snow_cover +
        kFloodGripLoss * local.flood + kHailGripLoss * local.hail +
        kNightGripLoss * night;
    local.grip = std::clamp(1.0f - loss, kMinGrip, 1.0f);
    return local;
}

}  // namespace apricot
