#pragma once

#include "city/start_area.h"

namespace apricot::city {

// Tacomaco is a second tenant of the Cloggers fast-food shell. The geometry
// stays shared on purpose: this is a real copy of the authored building and
// drive-through layout, while the site and brand materials stay independent.
// It occupies the Vellum Row block two blocks east of its original parcel,
// between Sixth and Seventh Street, with the same north-facing frontage.
inline constexpr StartSite kTacomacoSite{
    "Tacomaco",
    {70.0f + kGridCos * 136.0f + kGridSin * 155.0f,
     -40.0f - kGridSin * 136.0f + kGridCos * 155.0f},
    kGridCos, kGridSin, {0.0f, 0.0f}, 64.0f, 38.0f};

inline constexpr BuildingPlan kTacomacoPlan{
    "Tacomaco", kFastFoodWalls, 4, kFastFoodRoofs, 1,
    kFastFoodParts, kFastFoodPartCount, nullptr, 0};

static_assert(kTacomacoPlan.fixture_count == kFastFoodPartCount,
              "Tacomaco must retain the complete copied restaurant shell");

}  // namespace apricot::city
