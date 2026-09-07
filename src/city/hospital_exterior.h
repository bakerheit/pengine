#pragma once

#include <vector>

#include "city/hospital_garage_base.h"
#include "city/hospital_exterior_garage.h"
#include "city/hospital_north_parking.h"
#include "city/hospital_overhaul_logistics.h"
#include "city/hospital_overhaul_massing.h"
#include "city/hospital_overhaul_mobility.h"
#include "city/hospital_overhaul_public_realm.h"

namespace apricot::city {

// One authored stream keeps the replacement clinical shell, operational
// routes, public realm, retained garage, collision-bearing props, runtime
// light anchors, and fitted texture receivers together.
inline std::vector<StartPart> bake_polished_hospital_campus() {
    std::vector<StartPart> out = bake_hospital_overhaul_massing();
    const auto logistics = bake_hospital_overhaul_logistics();
    const auto mobility = bake_hospital_overhaul_mobility();
    const auto public_realm = bake_hospital_overhaul_public_realm();
    const auto garage_base = bake_hospital_garage_base();
    const auto garage_polish = bake_hospital_exterior_garage();
    out.reserve(out.size() + logistics.size() + mobility.size() +
                public_realm.size() + garage_base.size() +
                garage_polish.size());
    out.insert(out.end(), logistics.begin(), logistics.end());
    out.insert(out.end(), mobility.begin(), mobility.end());
    out.insert(out.end(), public_realm.begin(), public_realm.end());
    out.insert(out.end(), garage_base.begin(), garage_base.end());
    out.insert(out.end(), garage_polish.begin(), garage_polish.end());
    return out;
}

}  // namespace apricot::city
