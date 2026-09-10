#pragma once

#include "city/burgerpiz.h"

namespace apricot::city {
// The original TacoMaco parcel, now using the furnished BurgerPiz shell.
// That shell opens along +Z, so turn it around to retain north-facing access.
inline constexpr StartSite kTacomacoSite{
    "TacoMaco",
    {70.0f+kGridCos*136.0f+kGridSin*155.0f,
     -40.0f-kGridSin*136.0f+kGridCos*155.0f},
    -kGridCos,-kGridSin,{0,0},60,38,12,1400};
inline constexpr const char* kTacomacoAssetRoot="models/buildings/tacomaco/";
inline constexpr BuildingPlan kTacomacoPlan=kBurgerPizLotPlan;
} // namespace apricot::city
