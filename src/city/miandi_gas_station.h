#pragma once

#include "city/start_area.h"
#include <vector>

namespace apricot::city {
// North side of Gateway Drive, midway between Mango and Royal Palm.
// The source forecourt faces south; the store stays at the back of the lot.
inline constexpr StartSite kMiandiGasStationSite{
    "6twelve Gas",{7600,7960},1,0,{0,0},40,56,8,1000};
inline constexpr const char* kMiandiGasStationAssetRoot="models/buildings/miandi_gas_station/";
inline constexpr BuildingPiece kMiandiGasStationLotParts[]={
    {"miandi gas parking lot",{0,0},.01f,40,.06f,56,BuildingFinish::Asphalt,false},
    {"miandi gas interior floor",{-4,-11.94f},.19f,11.5f,.02f,10.4f,BuildingFinish::Concrete,false},
};
inline constexpr BuildingPlan kMiandiGasStationLotPlan{
    "6twelve Gas",nullptr,0,nullptr,0,
    kMiandiGasStationLotParts,std::size(kMiandiGasStationLotParts)};
inline std::vector<StartPart> bake_miandi_gas_station_lot() {
    return bake_building(kMiandiGasStationLotPlan);
}
} // namespace apricot::city
