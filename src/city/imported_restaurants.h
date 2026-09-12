#pragma once

#include "city/burgerpiz.h"
#include "city/church_of_waffles.h"
#include "city/tacomaco.h"

namespace apricot::city {

// Every restaurant whose furnished building is a cooked private import rather
// than creator geometry. Each row is a site, the asset root its cooked meshes
// live under, and the support plan that paves its parcel — the BurgerPiz shell
// shares one plan across three brands; the Quequis shell has its own.
struct ImportedRestaurantSite {
    const StartSite* site;
    const char* root;
    const BuildingPlan* plan;
};
inline constexpr ImportedRestaurantSite kImportedRestaurants[]={
    {&kBurgerPizSite,kBurgerPizAssetRoot,&kBurgerPizLotPlan},
    {&kFreakyFranksSite,kFreakyFranksAssetRoot,&kBurgerPizLotPlan},
    {&kTacomacoSite,kTacomacoAssetRoot,&kTacomacoPlan},
    {&kChurchOfWafflesSite,kChurchOfWafflesAssetRoot,&kChurchOfWafflesLotPlan},
};

} // namespace apricot::city
