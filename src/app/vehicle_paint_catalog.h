#pragma once
// Which atlas a drivable car wears, and which cars may exist without a paint
// profile. No GL: the headless suites include it.
#include <initializer_list>

#include "app/player_car_catalog.h"

namespace apricot {

// The atlas a car actually wears, asset-relative. The catalog row names it,
// except Workman: its articulated cab wears the semantic body.png, not the
// body_surface.png charts its legacy mesh used. PlayerCarVisual::load_model
// loads this path. The respray booth is not wired yet; when it is, it will look
// the paint profile up by this same path, so the texture on the car and the
// profile a respray picks can't name different atlases. Today only
// vehicle_paint_profiles_tests joins the two.
inline constexpr const char* player_car_body_texture_path(PlayerCarId id) {
    const PlayerCarDefinition& car = player_car_definition(id);
    return car.id == PlayerCarId::HarrowWorkman
        ? "textures/vehicles/harrow_workman/body.png" : car.texture_path;
}

// Cars allowed to exist without a paint profile, so the respray booth can say
// "not available yet" rather than guess at their paint. Every drivable car
// needs a profile (tools/paint_profiles/) or an entry here, and
// vehicle_paint_profiles_tests fails otherwise; it also fails for an entry
// that has a profile, so delete the entry in the commit that adds one.
inline constexpr std::initializer_list<PlayerCarId> kPaintProfilePending = {};

inline constexpr bool player_car_paint_pending(PlayerCarId id) {
    id = canonical_player_car_id(id);
    for (PlayerCarId pending : kPaintProfilePending) {
        if (canonical_player_car_id(pending) == id) return true;
    }
    return false;
}

}  // namespace apricot
