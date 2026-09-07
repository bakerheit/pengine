#pragma once

namespace apricot {

// Simulation stays in SI units. These helpers are only for converting values
// at the player-facing boundary, so physics and traffic tuning keep one
// coherent unit system while the HUD speaks US road language.
inline constexpr float kMetresPerSecondToMilesPerHour = 2.2369362920544f;
inline constexpr float kMilesPerHourToMetresPerSecond = 0.44704f;

constexpr float metres_per_second_to_miles_per_hour(float speed_mps) {
    return speed_mps * kMetresPerSecondToMilesPerHour;
}

constexpr float miles_per_hour_to_metres_per_second(float speed_mph) {
    return speed_mph * kMilesPerHourToMetresPerSecond;
}

}  // namespace apricot
