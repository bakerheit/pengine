#pragma once
#include <vector>
#include "city/bellwether_layout.h"
#include "city/start_area.h"

namespace apricot::city {
inline constexpr StartSite kBellwetherGasSite{
    "Bellwether Service",{570,-739},1,0,{0,0},50,46,kBellwetherGroundM,1100};
inline constexpr StartSite kBellwetherChurchSite{
    "Bellwether Parish",{438,-743},1,0,{0,0},18,30,kBellwetherGroundM,1500};
inline constexpr StartSite kBellwetherTowerSite{
    "Bellwether Water Tower",{378,-655},1,0,{0,0},12,12,kBellwetherGroundM,1800};
inline constexpr StartSite kBellwetherShopsSite{
    "Bellwether Main Street",{495,-666},1,0,{0,0},63,27,kBellwetherGroundM,1100};
inline std::vector<StartPart> bake_bellwether_gas_lot() {
    return {{"bellwether gas lot",{0,0},.015f,50,.06f,46,
             StartFinish::Asphalt,false}};
}
} // namespace apricot::city
