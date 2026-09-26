#pragma once

// Which trucks carry the plow kit, how it is sized for each, and the blade
// numbers the sim reads without opening a mesh. GL-free; the headless suites
// include it.
//
// The kit itself is fitted to the cooked body at load (app/plow_kit_mesh.h).
// The sim cannot wait for a mesh, so the blade mount and the collision reach
// are ALSO written here as numbers, and plow_kit_tests rebuilds the kit on the
// real cooked body and fails if the two disagree by more than a centimetre.
// That is the whole contract: the strip a blade clears and the box a blade
// hits with are the blade that draws. Recook a truck and the suite tells you
// which number to change.

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "app/player_car_catalog.h"
#include "app/plow_kit_mesh.h"
#include "core/transform.h"
#include "game/plow_blade.h"
#include "physics/vehicle.h"

namespace apricot {

// The body's placement under the chassis, exactly as PlayerCarVisual::select
// fits it: wheel arches onto the physical axles, nose to -Z.
inline Transform player_car_body_transform(const PlayerCarDefinition& definition,
                                           const VehicleTuning& tuning) {
    const float scale_x = tuning.half_track / definition.wheel_x;
    const float scale_z = (2.0f * tuning.half_wheelbase) /
        (definition.wheel_front_z + definition.wheel_rear_z);
    Transform body;
    body.scale = {scale_x, scale_z, scale_z};
    body.rotation = glm::angleAxis(3.14159265358979323846f, glm::vec3{0.0f, 1.0f, 0.0f});
    body.position.y = -tuning.com_height_above_mount - static_suspension_length(tuning) -
                      definition.arch_centre_y * scale_z;
    body.position.z = (definition.wheel_front_z - definition.wheel_rear_z) * scale_z * 0.5f;
    return body;
}

// Where the tyres touch the road, in the body's source units.
inline float player_car_source_ground_y(const PlayerCarDefinition& definition,
                                        const VehicleTuning& tuning) {
    const Transform body = player_car_body_transform(definition, tuning);
    return definition.arch_centre_y - tuning.wheel_radius / body.scale.y;
}

// Equipment choices. A compact pickup runs a 7'6" blade; a 3/4-ton utility
// truck an 8' blade a little taller. Western red on the Grazer, the fleet
// yellow a lot contractor paints everything on the Workman.
inline PlowKitSpec plow_kit_spec(PlayerCarId id) {
    PlowKitSpec spec;
    if (canonical_player_car_id(id) == PlayerCarId::HarrowWorkmanPlow) {
        spec.blade_width_m = 2.44f;
        spec.blade_height_m = 0.71f;
        spec.bar_length_m = 1.22f;
        spec.blade_paint = {0.93f, 0.63f, 0.035f};
    }
    return spec;
}

// Pinned against the fitted kit by plow_kit_tests (1 cm). The edge's DROP is
// not pinned: it is wherever the road is under the chassis origin, which the
// driving style's ride height moves; plow_blade_mount_for() derives it.
struct PlowKitNumbers {
    PlowBladeMount blade;
    // Chassis metres ahead of the origin the lowered blade reaches.
    float reach_m = 0.0f;
};

inline constexpr PlowKitNumbers plow_kit_numbers(PlayerCarId id) {
    switch (canonical_player_car_id(id)) {
        case PlayerCarId::RodeoGrazerPlow:
            return {{3.421f, 0.0f, 1.150f, 0.240f}, 3.463f};
        case PlayerCarId::HarrowWorkmanPlow:
            return {{3.166f, 0.0f, 1.220f, 0.240f}, 3.208f};
        default:
            return {};
    }
}

// The blade the sim scrapes with, for a truck driving on this tune: the
// pinned edge, dropped to the road under the chassis at rest. The kit puts the
// cutting edge on the ground by construction, and plow_kit_tests holds that.
inline PlowBladeMount plow_blade_mount_for(PlayerCarId id, const VehicleTuning& tuning) {
    PlowBladeMount mount = plow_kit_numbers(id).blade;
    mount.edge_drop_m = tuning.com_height_above_mount + static_suspension_length(tuning) +
                        tuning.wheel_radius;
    return mount;
}

// The truck a lot crew drives: the plow variant's own footprint, wheelbase,
// steering lock, ride height and blade, on the tune it drives with.
inline LotPlowTruckSpec lot_plow_truck_spec(PlayerCarId id, const VehicleTuning& tuning) {
    LotPlowTruckSpec spec;
    spec.half_width_m = tuning.car_collision_half_width;
    spec.rear_m = tuning.car_collision_half_length;
    spec.wheelbase_m = 2.0f * tuning.half_wheelbase;
    // A lot driver does not run the lock to the stops: 85% of it.
    spec.min_turn_radius_m = std::max(5.0f, spec.wheelbase_m /
        std::tan(std::max(0.1f, 0.85f * tuning.max_steer)));
    spec.blade = plow_blade_mount_for(id, tuning);
    spec.ride_height_m = spec.blade.edge_drop_m;
    return spec;
}

}  // namespace apricot
