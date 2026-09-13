#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include "city/states.h"

namespace apricot {

// Persisted IDs: append states/series; never reorder existing values.
enum class PlateSeries : uint8_t { Standard, Heritage, Commercial, Government, Count };
enum class PlateUse : uint8_t { Private, Commercial, Government };
struct PlateDesign {
    city::StateId state;
    PlateSeries series;
    const char* name;
    const char* pattern; // @ = A-Z excluding I/O/Q, # = digit; other characters are literals.
    const char* slogan;
};
inline constexpr std::string_view kPlateLetters = "ABCDEFGHJKLMNPRSTUVWXYZ";
inline constexpr std::string_view kPlateGlyphs = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789- ";
inline constexpr std::size_t kPlateSeriesCount = static_cast<std::size_t>(PlateSeries::Count);
inline constexpr std::array<PlateDesign, city::kStateCount * kPlateSeriesCount> kPlateDesigns{{
#include "game/license_plate_catalog.inc"
}};
constexpr bool complete_plate_catalog() {
    for (std::size_t i=0;i<kPlateDesigns.size();++i) {
        const auto& d=kPlateDesigns[i];
        if (!d.name || !d.pattern || !d.slogan ||
            static_cast<std::size_t>(d.state)!=i/kPlateSeriesCount ||
            static_cast<std::size_t>(d.series)!=i%kPlateSeriesCount) return false;
    }
    return true;
}
static_assert(complete_plate_catalog(), "Each state needs every plate series in persisted ID order");

struct VehicleRegistration {
    city::StateId state = city::StateId::OHaven;
    PlateSeries series = PlateSeries::Standard;
    uint64_t number = 0;
    bool operator==(const VehicleRegistration& other) const {
        return state == other.state && series == other.series && number == other.number;
    }
    bool operator!=(const VehicleRegistration& other) const { return !(*this == other); }
};
constexpr std::size_t plate_design_index(city::StateId state, PlateSeries series) {
    return static_cast<std::size_t>(state)*kPlateSeriesCount + static_cast<std::size_t>(series);
}
constexpr uint64_t plate_capacity(std::string_view pattern) {
    uint64_t result=1;
    for (const char c:pattern) result *= c=='@' ? kPlateLetters.size() : c=='#' ? 10u : 1u;
    return result;
}
inline bool valid_registration(const VehicleRegistration& plate) {
    if (plate.state>=city::StateId::Count || plate.series>=PlateSeries::Count) return false;
    return plate.number<plate_capacity(kPlateDesigns[plate_design_index(plate.state,plate.series)].pattern);
}
std::string plate_serial(const VehicleRegistration& plate);
// Pure issuance; never consumes simulation RNG. Identity includes departure
// generation and a domain (owned, moving traffic, or ambient parked traffic).
VehicleRegistration issue_registration(city::StateId state, PlateUse use,
    uint64_t identity, uint32_t slot=0, int64_t generation=0, uint64_t domain=0);
city::StateId registration_state_at(float x, float z);

} // namespace apricot
