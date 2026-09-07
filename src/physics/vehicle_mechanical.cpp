#include "physics/vehicle_mechanical.h"

#include <cmath>
#include <cstring>
#include "core/rng.h"

namespace apricot {

uint64_t vehicle_mechanical_key(uint64_t seed, float spawn_x, float spawn_z) {
    uint32_t x=0, z=0;
    std::memcpy(&x,&spawn_x,sizeof(x));
    std::memcpy(&z,&spawn_z,sizeof(z));
    return splitmix64_mix(seed ^ (static_cast<uint64_t>(x)<<32u) ^ z ^
                         0x56454849434C454Bull);
}

void step_vehicle_mechanical(VehicleMechanicalState& state,
                             const VehicleDamageState& damage, float dt,
                             uint64_t key) {
    if (!(dt>0.0f) || !std::isfinite(dt)) return;
    for (const auto& leak:vehicle_fluid_leaks(damage)) {
        if (!(leak.severity>0.0f)) continue;
        float* remaining=nullptr;
        float* lifetime=nullptr;
        float min_seconds=0.0f, max_seconds=0.0f;
        uint64_t salt=0;
        if (leak.kind==VehicleFluidKind::Oil) {
            remaining=&state.oil_remaining; lifetime=&state.oil_lifetime_s;
            min_seconds=45.0f; max_seconds=150.0f; salt=0x4F494C4C45414Bull;
        } else if (leak.kind==VehicleFluidKind::Fuel) {
            remaining=&state.fuel_remaining; lifetime=&state.fuel_lifetime_s;
            min_seconds=90.0f; max_seconds=240.0f; salt=0x4655454C4C45414Bull;
        } else {
            continue; // Cooling-system failure is a separate mechanic.
        }
        if (!(*lifetime>0.0f)) {
            Rng rng{splitmix64_mix(key^salt)};
            *lifetime=min_seconds+(max_seconds-min_seconds)*rng.next_float();
        }
        *remaining=std::clamp(*remaining-
            dt*std::clamp(leak.severity,0.0f,1.0f)/ *lifetime,0.0f,1.0f);
    }
    state.engine_failed=vehicle_engine_failed(state);
}

}  // namespace apricot
