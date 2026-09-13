#pragma once
#include "game/license_plate.h"
#include "app/player_car_catalog.h"
#include "traffic/ambient.h"

namespace apricot {
inline PlateUse player_plate_use(PlayerCarId model) {
    model=canonical_player_car_id(model);
    if (is_municipal_cruiser_91(model) || model==PlayerCarId::MunicipalAmbulance ||
        model==PlayerCarId::MunicipalFiretruck || model==PlayerCarId::LegacyCar8Ambulance ||
        model==PlayerCarId::LegacyCar5NextPolice) return PlateUse::Government;
    if (model==PlayerCarId::HarrowCityliner || model==PlayerCarId::HarrowParcel ||
        model==PlayerCarId::HarrowWorkman || model==PlayerCarId::HarrowHauler ||
        model==PlayerCarId::LegacyCar8) return PlateUse::Commercial;
    return PlateUse::Private;
}
inline VehicleRegistration player_registration(PlayerCarId model, uint64_t key, float x, float z) {
    return issue_registration(registration_state_at(x,z),player_plate_use(model),key,
        static_cast<uint32_t>(canonical_player_car_id(model)),0,0x4F574E4544ull);
}
inline PlateUse traffic_plate_use(TrafficVehicleKind kind) {
    if (kind==TrafficVehicleKind::Police || kind==TrafficVehicleKind::Ambulance ||
        kind==TrafficVehicleKind::Firetruck || kind==TrafficVehicleKind::Snowplow) return PlateUse::Government;
    return kind==TrafficVehicleKind::BoxTruck ? PlateUse::Commercial : PlateUse::Private;
}
} // namespace apricot
