#pragma once
// Which atlas a drivable car wears, and which cars may exist without a paint
// profile. No GL: the headless suites include it.
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>

#include "app/player_car_catalog.h"
#include "app/traffic_paint_paths.h"

namespace apricot {

// The atlas a car actually wears, asset-relative. The catalog row names it,
// except Workman: its articulated cab wears the semantic body.png, not the
// body_surface.png charts its legacy mesh used. PlayerCarVisual::load_model
// loads this path, and the respray booth looks the paint profile up by the
// same path (through the paint base, below), so the texture on the car and the
// profile a respray picks can't name different atlases.
// vehicle_paint_profiles_tests joins the two headlessly; --paint-check does it
// on the real PNGs.
inline constexpr const char* player_car_body_texture_path(PlayerCarId id) {
    const PlayerCarDefinition& car = player_car_definition(player_car_body_id(id));
    return car.id == PlayerCarId::HarrowWorkman
        ? "textures/vehicles/harrow_workman/body.png" : car.texture_path;
}

// Cars allowed to exist without a paint profile, so the respray booth can say
// "not available yet" rather than guess at their paint. Every drivable car
// needs a profile (tools/paint_profiles/) or an entry here, and
// vehicle_paint_profiles_tests fails otherwise; it also fails for an entry
// that has a profile, so delete the entry in the commit that adds one.
inline constexpr std::initializer_list<PlayerCarId> kPaintProfilePending = {
    PlayerCarId::GlmMeridian, PlayerCarId::RodeoSwitchback, PlayerCarId::HarrowHookline};

inline constexpr bool player_car_paint_pending(PlayerCarId id) {
    id = canonical_player_car_id(id);
    for (PlayerCarId pending : kPaintProfilePending) {
        if (canonical_player_car_id(pending) == id) return true;
    }
    return false;
}

// PAINT BASES: the factory liveries a car can wear before any respray. Most
// cars have one, the atlas their catalog row names. Car 5 and Car 8 have the
// traffic liveries a stolen one keeps (green, grey, taxi; grey, purple, mail),
// and the base is saved as an index into those lists, so their order is a
// saved ID (app/traffic_paint_paths.h). A respray recolours the base the car
// wears, not always the stock atlas, because a livery can carry its own art
// inside the paint (car8/mail.png has its stripes there).
inline constexpr uint8_t player_car_paint_base_count(PlayerCarId id) {
    switch (canonical_player_car_id(id)) {
    case PlayerCarId::LegacyCar5: return static_cast<uint8_t>(std::size(kCar5Paints));
    case PlayerCarId::LegacyCar8: return static_cast<uint8_t>(std::size(kCar8Paints));
    default: return 1;
    }
}

inline constexpr bool paint_base_valid(PlayerCarId id, uint8_t base) {
    return base < player_car_paint_base_count(id);
}

// The atlas a base names, or nullptr when the car has no such base.
inline constexpr const char* player_car_paint_base_atlas(PlayerCarId id, uint8_t base) {
    id = canonical_player_car_id(id);
    if (!paint_base_valid(id, base)) return nullptr;
    if (id == PlayerCarId::LegacyCar5) return kCar5Paints[base];
    if (id == PlayerCarId::LegacyCar8) return kCar8Paints[base];
    return player_car_body_texture_path(id);
}

// The base a car taken from traffic keeps. Only Car 5 and Car 8 carry more
// than one livery; a stolen snowplow draws flat white and takes base 0, so a
// respray of it recolours car8/body.png.
inline constexpr uint8_t paint_base_for_traffic(TrafficVehicleKind kind, std::size_t paint_index) {
    if (kind != TrafficVehicleKind::Sedan && kind != TrafficVehicleKind::BoxTruck) return 0;
    return paint_index < traffic_paint_paths(kind).count ? static_cast<uint8_t>(paint_index) : 0;
}

}  // namespace apricot
