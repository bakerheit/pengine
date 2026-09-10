#pragma once

#include "city/start_area.h"
#include <vector>

namespace apricot::city {
// Clear northeast Pinatty block between Wren/Cinder and Ninth/Tenth.
inline constexpr StartSite kBurgerPizSite{
    "BurgerPiz",{70+kGridCos*322-kGridSin*279,
                 -40-kGridSin*322-kGridCos*279},
    kGridCos,kGridSin,{0,0},60,38,12,1400};
inline constexpr const char* kBurgerPizAssetRoot="models/buildings/burgerpiz/";
inline constexpr StartSite kFreakyFranksSite{
    "Freaky Franks",{70+kGridCos*322-kGridSin*93,
                -40-kGridSin*322-kGridCos*93},
    kGridCos,kGridSin,{0,0},60,38,12,1400};
inline constexpr const char* kFreakyFranksAssetRoot="models/buildings/freakyfranks/";

// Shared support plan for each independently branded imported shell. The
// actual furnished building is loaded from the selected cooked asset root.
inline constexpr BuildingPiece kBurgerPizLotParts[]={
    {"imported restaurant parking lot",{0,0},.02f,60,.08f,38,BuildingFinish::Asphalt,false},
    {"imported restaurant interior floor",{-.56f,-5.12f},.10f,29.22f,.13f,17.82f,BuildingFinish::Concrete,false},
};
inline constexpr BuildingPlan kBurgerPizLotPlan{
    "Imported furnished restaurant",nullptr,0,nullptr,0,
    kBurgerPizLotParts,std::size(kBurgerPizLotParts)};
inline std::vector<StartPart> bake_burgerpiz_lot() {
    return bake_building(kBurgerPizLotPlan);
}
} // namespace apricot::city
