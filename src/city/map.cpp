#include "city/map.h"

#include "city/airport.h"
#include "city/florangia_airport.h"
#include "city/districts.h"
#include "city/landmarks.h"
#include "city/luxury_neighborhood.h"
#include "city/miandi_layout.h"
#include "city/terrain_ops.h"
#include "city/tidewater_farm.h"

namespace apricot {
namespace city {
namespace {

// The countryside's name. It is not a district — it is everything the ten
// polygons do not claim — but every caller that prints a district would
// otherwise have to special-case it, and a special case at every call site is
// a special case somebody will forget.
constexpr const char* kMeadowsName = "the Meadows";

}  // namespace

DistrictId district_at(float x, float z) {
    // Table order, first hit wins. Two districts that genuinely abut share an
    // edge, and the half-open crossing test in Boundary::contains() already
    // makes a point on that edge belong to exactly one of them — but if a
    // future edit ever overlaps two polygons properly, this makes the outcome
    // AUTHORED (the earlier entry) rather than whatever the compiler laid out
    // first. A pedestrian standing on a boundary needs one police response
    // time, not two, and not a different one each build.
    for (int i = 0; i < kDistrictCount; ++i) {
        if (kDistricts[i].boundary.contains(x, z)) {
            return kDistricts[i].id;
        }
    }
    return DistrictId::Count;
}

const District& district(DistrictId id) {
    const int i = static_cast<int>(id);
    // Clamped rather than asserted. This is called from generation, which has
    // no error channel worth the name; handing back Vellum Row for a corrupt
    // id is wrong, but it is wrong in a way that draws a city, and the id is
    // dense-and-ordered by static_assert so the only way to get here is a cast
    // from an integer that was never a district.
    return kDistricts[(i >= 0 && i < kDistrictCount) ? i : 0];
}

const char* district_name(DistrictId id) {
    const int i = static_cast<int>(id);
    if (i < 0 || i >= kDistrictCount) return kMeadowsName;
    return kDistricts[i].name;
}

float wild_scatter_at(float x, float z) {
    // The airfield is authored open ground. Random trees in front of the
    // terminal or beside a runway are not variety; they are a layout bug.
    if (airport_lot_contains(x, z, 8.0f)) return 0.0f;
    if (florangia_airport_lot_contains(x, z, 8.0f)) return 0.0f;
    if (miandi_city_contains(x, z, 12.0f)) return 0.0f;
    // Crops, the house yard, and barn circulation are authored open ground.
    if (tidewater_farm_lot_contains(x, z, 7.0f)) return 0.0f;
    // Westmere supplies its own mature trees; random forest would block
    // drives, pools and the sightline through the gate.
    if (luxury_neighborhood_contains(x, z, 8.0f)) return 0.0f;
    const DistrictId d = district_at(x, z);
    if (d == DistrictId::Count) return 1.0f;  // open country
    return kDistricts[static_cast<int>(d)].props.wild;
}

}  // namespace city
}  // namespace apricot
