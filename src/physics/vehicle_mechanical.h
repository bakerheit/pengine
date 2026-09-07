#pragma once

#include <cstdint>
#include "physics/vehicle_damage.h"

namespace apricot {

// Persistent fluid reserves and the one-time lifetime sampled for each leak.
// Values travel with the vehicle through parking, possession and replay.
struct VehicleMechanicalState {
    float oil_remaining = 1.0f;
    float fuel_remaining = 1.0f;
    float oil_lifetime_s = 0.0f;
    float fuel_lifetime_s = 0.0f;
    bool engine_failed = false;
};

inline bool vehicle_engine_failed(const VehicleMechanicalState& state) {
    return state.engine_failed || state.oil_remaining <= 0.0f ||
           state.fuel_remaining <= 0.0f;
}

// Call exactly once per simulated interval, including for parked vehicles.
// Damage controls current leak rate; stopping a leak does not refill a reserve.
void step_vehicle_mechanical(VehicleMechanicalState& state,
                             const VehicleDamageState& damage, float dt,
                             uint64_t key);

// Fallback identity for a newly spawned physics vehicle. Host-owned vehicles
// may replace this with their stable authored/traffic identity before a leak.
uint64_t vehicle_mechanical_key(uint64_t seed, float spawn_x, float spawn_z);

}  // namespace apricot
