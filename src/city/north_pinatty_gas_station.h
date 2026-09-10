#pragma once
#include "city/miandi_gas_station.h"

namespace apricot::city {
// South side of the Yard Road in north Pinatty's Kepler Flats. The imported
// forecourt faces local +Z; turn it north toward the existing public road.
inline constexpr StartSite kNorthPinattyGasStationSite{
    "Six Twelve",{-260,-1870},-1,0,{0,0},40,56,9,1200};
inline constexpr const char* kNorthPinattyGasStationAssetRoot=
    "models/buildings/north_pinatty_gas_station/";
struct ImportedGasStationSite { const StartSite* site; const char* root; };
inline constexpr ImportedGasStationSite kImportedGasStations[]={
    {&kMiandiGasStationSite,kMiandiGasStationAssetRoot},
    {&kNorthPinattyGasStationSite,kNorthPinattyGasStationAssetRoot},
};
} // namespace apricot::city
