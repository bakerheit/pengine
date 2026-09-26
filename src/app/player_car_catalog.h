#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace apricot {

// Model IDs are stored in checkpoints. Append new IDs to keep old saves valid;
// the separate catalog below sorts brands and models for the menu.
enum class PlayerCarId : uint8_t {
    AlderPip,
    AlderRidge,
    AlderWayfarer,
    GlmLunge,
    GlmZip,
    HalcyonSix,
    HalcyonSovereign,
    HarrowCityliner,
    HarrowParcel,
    HarrowWorkman,
    LegacyCar5,
    LegacyCar8,
    MontroseRegentEight,
    MunicipalAmbulance,
    MunicipalFiretruck,
    MunicipalCruiser91C,  // old police slot now migrates Sentinel saves to 91-C
    VesperMistral,
    VesperScythe,
    VesperVx91,
    HarrowHauler,  // appended: existing checkpoint model ids remain stable
    OrisonCinderGt,
    SpagattiShu,
    MunicipalCruiser91A,
    MunicipalCruiser91B,
    LegacyCruiser91CSlot,  // 91-C preview-build save compatibility
    MunicipalCruiser91D,
    MunicipalCruiser91E,
    FangVenom,  // appended: checkpoint ids above remain stable
    LegacyCar5Next,
    LegacyCar8Ambulance,
    LegacyCar5NextPolice,
    EmberGt,
    RodeoGrazer,
    Bwc360,
    SaddleTango,
    GlmMeridian,
    RodeoSwitchback,
    HarrowHookline,
    // Fitted-equipment variants: the base truck's cooked body, doors, glass
    // and seat with a front plow and a roof service light bar bolted on
    // (app/plow_kit.h). Appended, so every checkpoint id above stays put.
    RodeoGrazerPlow,
    HarrowWorkmanPlow,
    kCount,
};

inline constexpr bool is_municipal_cruiser_91(PlayerCarId id) {
    id = id == PlayerCarId::LegacyCruiser91CSlot
        ? PlayerCarId::MunicipalCruiser91C : id;
    return id == PlayerCarId::MunicipalCruiser91A ||
           id == PlayerCarId::MunicipalCruiser91B ||
           id == PlayerCarId::MunicipalCruiser91C ||
           id == PlayerCarId::MunicipalCruiser91D ||
           id == PlayerCarId::MunicipalCruiser91E;
}

inline constexpr std::size_t kPlayerCarCount =
    static_cast<std::size_t>(PlayerCarId::kCount);
inline constexpr std::size_t kSelectablePlayerCarCount = kPlayerCarCount - 1u;

inline constexpr PlayerCarId canonical_player_car_id(PlayerCarId id) {
    return id == PlayerCarId::LegacyCruiser91CSlot
        ? PlayerCarId::MunicipalCruiser91C : id;
}

// THE BODY A VARIANT DRIVES. A plow truck is its base truck with equipment
// bolted on: the same cooked shell, doors, glass, seat, lamps and plate
// mounts. Everything keyed on that geometry asks this; the catalog row, the
// tuning and the equipment stay keyed on the variant's own id.
inline constexpr PlayerCarId player_car_body_id(PlayerCarId id) {
    id = canonical_player_car_id(id);
    if (id == PlayerCarId::RodeoGrazerPlow) return PlayerCarId::RodeoGrazer;
    if (id == PlayerCarId::HarrowWorkmanPlow) return PlayerCarId::HarrowWorkman;
    return id;
}

inline constexpr bool has_plow_kit(PlayerCarId id) {
    id = canonical_player_car_id(id);
    return id == PlayerCarId::RodeoGrazerPlow || id == PlayerCarId::HarrowWorkmanPlow;
}

// --player-car key for a variant that shares its base's model folder, or
// nullptr when the folder name is the key.
inline constexpr const char* player_car_variant_key(PlayerCarId id) {
    id = canonical_player_car_id(id);
    if (id == PlayerCarId::RodeoGrazerPlow) return "rodeo_grazer_plow";
    if (id == PlayerCarId::HarrowWorkmanPlow) return "harrow_workman_plow";
    return nullptr;
}

inline constexpr bool is_motorbike(PlayerCarId id) {
    return canonical_player_car_id(id) == PlayerCarId::FangVenom;
}

struct PlayerCarDefinition {
    PlayerCarId id = PlayerCarId::LegacyCar5;
    const char* brand = nullptr;
    const char* model = nullptr;
    const char* mesh_path = nullptr;
    const char* texture_path = nullptr;
    float arch_centre_y = 0.0f;
    float wheel_x = 1.0f;
    float wheel_front_z = 1.0f;
    float wheel_rear_z = 1.0f;
    // Zero keeps the selected driving preset chassis dimensions.
    float physical_half_wheelbase = 0.0f;
    float physical_half_track = 0.0f;
    float physical_wheel_radius = 0.0f;
};

inline constexpr std::array<PlayerCarDefinition, kSelectablePlayerCarCount>
    kPlayerCars{{
        {PlayerCarId::AlderPip, "ALDER", "PIP",
         "models/vehicles/alder_pip/body.emesh", "textures/vehicles/alder_pip/body.png",
         .34f,.72f,1.10f,1.26f,1.18f,.72f,.31f},
        {PlayerCarId::AlderRidge, "ALDER", "RIDGE",
         "models/vehicles/alder_ridge/body.emesh",
         "textures/vehicles/alder_ridge/body.png",
         .39f, .98f, 1.24f, 1.40f},
        {PlayerCarId::AlderWayfarer, "ALDER", "WAYFARER",
         "models/vehicles/alder_wayfarer/body.emesh",
         "textures/vehicles/alder_wayfarer/body.png",
         0.43f, 1.00f, 1.70f, 1.55f},
        {PlayerCarId::Bwc360, "BWC", "360",
         "models/vehicles/bwc_360/body.emesh", "textures/vehicles/bwc_360/body.png",
         .325f,.755f,1.27f,1.30f,1.285f,.755f,.315f},
        {PlayerCarId::EmberGt, "EMBER", "GT",
         "models/vehicles/ember_gt/body.emesh", "textures/vehicles/ember_gt/body.png",
         .375f,.88f,1.40f,1.36f,1.38f,.88f,.365f},
        {PlayerCarId::FangVenom, "FANG", "VENOM",
         "models/vehicles/fang_venom_v2/body.emesh",
         "textures/vehicles/fang_venom_v2/body.png",
         .35f, .25f, .76f, .72f, .74f, .25f, .345f},
        {PlayerCarId::GlmLunge, "GLM", "LUNGE",
         "models/vehicles/glm_lunge/body_surface.emesh",
         "textures/vehicles/glm_lunge/body_surface.png",
         0.50f, 1.08f, 1.58f, 1.62f},
        {PlayerCarId::GlmMeridian, "GLM", "MERIDIAN",
         "models/vehicles/glm_meridian/body.emesh",
         "textures/vehicles/glm_meridian/body.png",
         .365f,.80f,1.39f,1.39f,1.39f,.80f,.365f},
        {PlayerCarId::GlmZip, "GLM", "ZIP",
         "models/vehicles/glm_zip/body_surface.emesh",
         "textures/vehicles/glm_zip/body_surface.png",
         0.46f, 0.98f, 1.65f, 1.60f},
        {PlayerCarId::HalcyonSix, "HALCYON", "SIX SEDAN",
         "models/vehicles/halcyon_six/body_surface.emesh",
         "textures/vehicles/halcyon_six/body_surface.png",
         0.68f, 1.06f, 2.18f, 1.86f},
        {PlayerCarId::HalcyonSovereign, "HALCYON", "SOVEREIGN LIMO",
         "models/vehicles/halcyon_sovereign/body.emesh", "textures/vehicles/halcyon_sovereign/body.png",
         .41f,.89f,2.85f,2.70f,2.775f,.89f,.36f},
        {PlayerCarId::HarrowCityliner, "HARROW", "CITYLINER BUS",
         "models/vehicles/harrow_cityliner/body.emesh",
         "textures/vehicles/harrow_cityliner/body.png",
         .55f, 1.12f, 3.0f, 2.6f, 2.8f, 1.12f, .48f},
        {PlayerCarId::HarrowHauler, "HARROW", "HAULER SEMI",
         "models/vehicles/harrow_hauler/body.emesh",
         "textures/vehicles/harrow_hauler/body.png",
         .50f, 1.10f, 2.05f, 2.05f, 2.05f, 1.10f, .50f},
        {PlayerCarId::HarrowHookline, "HARROW", "HOOKLINE",
         "models/vehicles/harrow_hookline/body.emesh",
         "textures/vehicles/harrow_hookline/body.png",
         .46f,.90f,1.62f,1.58f,1.60f,.90f,.46f},
        {PlayerCarId::HarrowParcel, "HARROW", "PARCEL",
         "models/vehicles/harrow_parcel/body.emesh",
         "textures/vehicles/harrow_parcel/body.png",
         .43f, .99f, 1.62f, 1.48f},
        {PlayerCarId::HarrowWorkman, "HARROW", "WORKMAN",
         "models/vehicles/harrow_workman/body_surface.emesh",
         "textures/vehicles/harrow_workman/body_surface.png",
         0.45f, 0.94f, 1.65f, 1.55f},
        // Workman's row again, with the plow kit. The fit numbers must stay
        // the base row's: the kit is fitted to that body and placed by them.
        {PlayerCarId::HarrowWorkmanPlow, "HARROW", "WORKMAN PLOW",
         "models/vehicles/harrow_workman/body_surface.emesh",
         "textures/vehicles/harrow_workman/body_surface.png",
         0.45f, 0.94f, 1.65f, 1.55f},
        {PlayerCarId::LegacyCar5, "LEGACY", "CAR 5",
         "models/vehicles/car5/body.emesh",
         "textures/vehicles/car5/body.png",
         0.45f, 1.038f, 2.254f, 1.813f},
        // Car 5's shell with the driver door cut out of it, so the fit numbers
        // are Car 5's numbers. They must stay equal: the door anchors in
        // car5_next_door.h are in that body's source units and the cut in
        // tools/car5_next_spec.py derives its driver rig from this row.
        // Shares Car 5's paint on purpose: the cut does not touch a UV, so a
        // copied atlas could only ever drift away from the body it wraps. Only
        // the mesh path selects a model folder for lamps, snow and engine note.
        {PlayerCarId::LegacyCar5Next, "LEGACY", "CAR 5-NEXT",
         "models/vehicles/car5_next/body.emesh",
         "textures/vehicles/car5/body.png",
         0.45f, 1.038f, 2.254f, 1.813f},
        // Car 5-NEXT again, in black-and-white with a roof lightbar. It needs
        // its own shell rather than a repaint like CAR 8 AMBULANCE, because the
        // lightbar is real bodywork: the emergency glow pass redraws the body
        // mesh and the lit shader keeps only what falls inside the lens box.
        {PlayerCarId::LegacyCar5NextPolice, "LEGACY", "CAR 5-NEXT PATROL",
         "models/vehicles/car5_next_police/body.emesh",
         "textures/vehicles/car5_next_police/body.png",
         0.45f, 1.038f, 2.254f, 1.813f},
        {PlayerCarId::LegacyCar8, "LEGACY", "CAR 8",
         "models/vehicles/car8/body.emesh",
         "textures/vehicles/car8/body.png",
         0.525f, 1.10f, 1.65f, 2.25f},
        // Car 8's own shell in a different paint, so the fit numbers are Car
        // 8's numbers and they must stay equal to the row above. The mesh path
        // is what picks a model folder for lamps, snow and the engine note, so
        // sharing it is the point: this is a repaint, not a second vehicle.
        // tools/make_car8_ambulance_texture.py cooks the atlas.
        {PlayerCarId::LegacyCar8Ambulance, "LEGACY", "CAR 8 AMBULANCE",
         "models/vehicles/car8/body.emesh",
         "textures/vehicles/car8/ambulance.png",
         0.525f, 1.10f, 1.65f, 2.25f},
        {PlayerCarId::MontroseRegentEight, "MONTROSE", "REGENT EIGHT",
         "models/vehicles/montrose_regent_eight/body_surface.emesh",
         "textures/vehicles/montrose_regent_eight/body_surface.png",
         0.68f, 1.10f, 2.30f, 2.05f},
        {PlayerCarId::MunicipalAmbulance, "MUNICIPAL", "AMBULANCE",
         "models/vehicles/ambulance/body.emesh",
         "textures/vehicles/ambulance/body.png",
         .47f, .98f, 1.78f, 1.58f, 1.68f, .98f, .43f},
        {PlayerCarId::MunicipalCruiser91A, "MUNICIPAL", "CRUISER 91-A SQUARE",
         "models/vehicles/municipal_cruiser_91a/body.emesh",
         "textures/vehicles/municipal_cruiser_91a/body.png",
         .42f,.94f,1.63f,1.53f,1.58f,.94f,.43f},
        {PlayerCarId::MunicipalCruiser91B, "MUNICIPAL", "CRUISER 91-B AERO",
         "models/vehicles/municipal_cruiser_91b/body.emesh",
         "textures/vehicles/municipal_cruiser_91b/body.png",
         .42f,.94f,1.63f,1.53f,1.58f,.94f,.34f},
        // Half-track 0.845 against the 1.05 body half-width leaves 0.099 m of
        // fender over the tyre. At 0.94 the tyre sidewall was the outermost
        // surface on the car. Keep wheel_x and physical_half_track equal:
        // municipal_cruiser_91_tests and player_car_visual both rely on it.
        {PlayerCarId::MunicipalCruiser91C, "MUNICIPAL", "CRUISER 91-C PURSUIT",
         "models/vehicles/municipal_cruiser_91c/body.emesh",
         "textures/vehicles/municipal_cruiser_91c/body.png",
         .42f,.845f,1.63f,1.53f,1.58f,.845f,.355f},
        {PlayerCarId::MunicipalCruiser91D, "MUNICIPAL", "CRUISER 91-D METRO",
         "models/vehicles/municipal_cruiser_91d/body.emesh",
         "textures/vehicles/municipal_cruiser_91d/body.png",
         .42f,.91f,1.48f,1.43f,1.455f,.91f,.38f},
        {PlayerCarId::MunicipalCruiser91E, "MUNICIPAL", "CRUISER 91-E HIGHWAY",
         "models/vehicles/municipal_cruiser_91e/body.emesh",
         "textures/vehicles/municipal_cruiser_91e/body.png",
         .41f,.97f,1.72f,1.60f,1.66f,.97f,.41f},
        {PlayerCarId::MunicipalFiretruck, "MUNICIPAL", "FIRETRUCK",
         "models/vehicles/firetruck/body_surface.emesh",
         "textures/vehicles/firetruck/body_surface.png",
         0.72f, 1.13f, 2.18f, 2.12f},
        {PlayerCarId::OrisonCinderGt, "ORISON", "CINDER GT",
         "models/vehicles/orison_cinder/body.emesh",
         "textures/vehicles/orison_cinder/body.png",
         .34f, .80f, 1.30f, 1.24f, 1.27f, .80f, .326f},
        {PlayerCarId::RodeoGrazer, "RODEO", "GRAZER 4X4",
         "models/vehicles/rodeo_grazer/body.emesh", "textures/vehicles/rodeo_grazer/body.png",
         .405f,.82f,1.43f,1.50f,1.465f,.82f,.405f},
        // Grazer's row again, with the plow kit; same body, same fit numbers.
        {PlayerCarId::RodeoGrazerPlow, "RODEO", "GRAZER 4X4 PLOW",
         "models/vehicles/rodeo_grazer/body.emesh", "textures/vehicles/rodeo_grazer/body.png",
         .405f,.82f,1.43f,1.50f,1.465f,.82f,.405f},
        {PlayerCarId::RodeoSwitchback, "RODEO", "SWITCHBACK",
         "models/vehicles/rodeo_switchback/body.emesh",
         "textures/vehicles/rodeo_switchback/body.png",
         .46f,.83f,1.38f,1.35f,1.365f,.83f,.46f},
        {PlayerCarId::SaddleTango, "SADDLE", "TANGO",
         "models/vehicles/saddle_tango/body.emesh",
         "textures/vehicles/saddle_tango/body.png",
         .35f,.84f,1.37f,1.38f,1.375f,.84f,.35f},
        {PlayerCarId::SpagattiShu, "SPAGATTI", "SHŪ",
         "models/vehicles/spagatti_shu/body.emesh",
         "textures/vehicles/spagatti_shu/body.png",
         .43f, .94f, 1.45f, 1.35f, 1.40f, .94f, .34f},
        {PlayerCarId::VesperMistral, "VESPER", "MISTRAL",
         "models/vehicles/vesper_mistral/body.emesh",
         "textures/vehicles/vesper_mistral/body.png",
         .38f, .91f, 1.48f, 1.30f},
        {PlayerCarId::VesperScythe, "VESPER", "SCYTHE",
         "models/vehicles/vesper_scythe/body.emesh", "textures/vehicles/vesper_scythe/body.png",
         .36f,.87f,1.30f,1.25f,1.275f,.87f,.34f},
        {PlayerCarId::VesperVx91, "VESPER", "VX-91",
         "models/vehicles/vesper_vx91/body_surface.emesh",
         "textures/vehicles/vesper_vx91/body_surface.png",
         0.55f, 1.08f, 2.08f, 1.86f},
    }};

struct PlayerCarBrand {
    const char* name = nullptr;
    const char* menu_label = nullptr;
    std::size_t first_car = 0;
    std::size_t car_count = 0;
};

inline constexpr std::array<PlayerCarBrand, 15> kPlayerCarBrands{{
    {"ALDER", "ALDER  >", 0u, 3u},
    {"BWC", "BWC  >", 3u, 1u},
    {"EMBER", "EMBER  >", 4u, 1u},
    {"FANG", "FANG  >", 5u, 1u},
    {"GLM", "GLM  >", 6u, 3u},
    {"HALCYON", "HALCYON  >", 9u, 2u},
    {"HARROW", "HARROW  >", 11u, 6u},
    {"LEGACY", "LEGACY  >", 17u, 5u},
    {"MONTROSE", "MONTROSE  >", 22u, 1u},
    {"MUNICIPAL", "MUNICIPAL  >", 23u, 7u},
    {"ORISON", "ORISON  >", 30u, 1u},
    {"RODEO", "RODEO  >", 31u, 3u},
    {"SADDLE", "SADDLE  >", 34u, 1u},
    {"SPAGATTI", "SPAGATTI  >", 35u, 1u},
    {"VESPER", "VESPER  >", 36u, 3u},
}};

inline constexpr const PlayerCarDefinition& player_car_definition(
    PlayerCarId id) {
    id = canonical_player_car_id(id);
    for (const auto& car : kPlayerCars) if (car.id == id) return car;
    return kPlayerCars[0];
}

inline constexpr int player_car_brand_index(PlayerCarId id) {
    id = canonical_player_car_id(id);
    std::size_t car = 0;
    while (car < kPlayerCars.size() && kPlayerCars[car].id != id) ++car;
    for (std::size_t brand = 0; brand < kPlayerCarBrands.size(); ++brand) {
        const PlayerCarBrand& entry = kPlayerCarBrands[brand];
        if (car >= entry.first_car && car < entry.first_car + entry.car_count) {
            return static_cast<int>(brand);
        }
    }
    return 0;
}

}  // namespace apricot
