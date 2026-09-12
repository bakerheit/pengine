#pragma once

#include "city/start_area.h"
#include <vector>

namespace apricot::city {

// The fourth imported restaurant, and the first from a second supplied shell.
// It takes the last free block on the Wren/Cinder column: between Eighth and
// Ninth, with its forecourt opening north onto Eighth Street (road 38). The
// three BurgerPiz-shell brands sit on the same column at grid -93, -279 and
// (for TacoMaco) +155, so the parameter below is the grid row negated, exactly
// as it is there.
inline constexpr StartSite kChurchOfWafflesSite{
    "Church of Waffles",{70+kGridCos*322-kGridSin*217,
                         -40-kGridSin*322-kGridCos*217},
    kGridCos,kGridSin,{0,0},60,38,12,1400};
inline constexpr const char* kChurchOfWafflesAssetRoot="models/buildings/churchofwaffles/";

// The Quequis shell is a long, shallow diner rather than the BurgerPiz box, so
// it gets its own support plan: the same 60 x 38 m asphalt parcel, and an
// interior floor slab matching the imported Floor mesh's own footprint. The
// furnished building itself is loaded from the cooked asset root.
// Eight nose-in bays, four either side of the door approach, in front of a
// glazed wall that runs the whole frontage. The two supplied lamp posts stand
// further out at local z 15, so the bays stop well short of them.
inline constexpr BuildingPiece kChurchOfWafflesLotParts[]={
    {"church of waffles parking lot",{0,0},.02f,60,.08f,38,BuildingFinish::Asphalt,false},
    {"church of waffles interior floor",{.48f,-2.15f},.10f,30.35f,.13f,9.65f,BuildingFinish::Concrete,false},
    {"church of waffles parking stripe",{-14,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
    {"church of waffles parking stripe",{-11.25f,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
    {"church of waffles parking stripe",{-8.5f,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
    {"church of waffles parking stripe",{-5.75f,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
    {"church of waffles parking stripe",{-3,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
    {"church of waffles parking stripe",{3,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
    {"church of waffles parking stripe",{5.75f,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
    {"church of waffles parking stripe",{8.5f,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
    {"church of waffles parking stripe",{11.25f,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
    {"church of waffles parking stripe",{14,8},.104f,.09f,.008f,5.5f,BuildingFinish::White,false},
};
inline constexpr BuildingPlan kChurchOfWafflesLotPlan{
    "Imported furnished waffle house",nullptr,0,nullptr,0,
    kChurchOfWafflesLotParts,std::size(kChurchOfWafflesLotParts)};
inline std::vector<StartPart> bake_church_of_waffles_lot() {
    return bake_building(kChurchOfWafflesLotPlan);
}

} // namespace apricot::city
