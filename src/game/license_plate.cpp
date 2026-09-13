#include "game/license_plate.h"
#include "core/rng.h"
#include "city/map.h"
#include "terrain/heightmap.h"

namespace apricot {
std::string plate_serial(const VehicleRegistration& plate) {
    if (!valid_registration(plate)) return {};
    std::string result=kPlateDesigns[plate_design_index(plate.state,plate.series)].pattern;
    uint64_t value=plate.number;
    for (auto it=result.rbegin();it!=result.rend();++it) {
        if (*it=='@') { *it=kPlateLetters[value%kPlateLetters.size()]; value/=kPlateLetters.size(); }
        else if (*it=='#') { *it=static_cast<char>('0'+value%10u); value/=10u; }
    }
    return result;
}
VehicleRegistration issue_registration(city::StateId state, PlateUse use,
    uint64_t identity, uint32_t slot, int64_t generation, uint64_t domain) {
    const uint64_t h=splitmix64_mix(identity ^ splitmix64_mix(uint64_t{slot} ^ 0x534C4F54ull) ^
        splitmix64_mix(static_cast<uint64_t>(generation) ^ 0x47454Eull) ^ splitmix64_mix(domain));
    const auto series=use==PlateUse::Government ? PlateSeries::Government :
        use==PlateUse::Commercial ? PlateSeries::Commercial :
        (h%5u==0u ? PlateSeries::Heritage : PlateSeries::Standard);
    if (state>=city::StateId::Count) state=city::StateId::OHaven;
    return {state,series,splitmix64_mix(h ^ city::kMapSeed) %
        plate_capacity(kPlateDesigns[plate_design_index(state,series)].pattern)};
}
city::StateId registration_state_at(float x, float z) {
    return florangia_mask(city::kMapSeed,x,z)>0.f
        ? city::StateId::Florangia : city::StateId::OHaven;
}
} // namespace apricot
