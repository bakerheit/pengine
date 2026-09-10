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
    GlrLunge,
    GlrZip,
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
        {PlayerCarId::FangVenom, "FANG", "VENOM",
         "models/vehicles/fang_venom_v2/body.emesh",
         "textures/vehicles/fang_venom_v2/body.png",
         .35f, .25f, .76f, .72f, .74f, .25f, .345f},
        {PlayerCarId::GlrLunge, "GLR", "LUNGE",
         "models/vehicles/glr_lunge/body_surface.emesh",
         "textures/vehicles/glr_lunge/body_surface.png",
         0.50f, 1.08f, 1.58f, 1.62f},
        {PlayerCarId::GlrZip, "GLR", "ZIP",
         "models/vehicles/glr_zip/body_surface.emesh",
         "textures/vehicles/glr_zip/body_surface.png",
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
        {PlayerCarId::HarrowParcel, "HARROW", "PARCEL",
         "models/vehicles/harrow_parcel/body.emesh",
         "textures/vehicles/harrow_parcel/body.png",
         .43f, .99f, 1.62f, 1.48f},
        {PlayerCarId::HarrowWorkman, "HARROW", "WORKMAN",
         "models/vehicles/harrow_workman/body_surface.emesh",
         "textures/vehicles/harrow_workman/body_surface.png",
         0.45f, 0.94f, 1.65f, 1.55f},
        {PlayerCarId::LegacyCar5, "LEGACY", "CAR 5",
         "models/vehicles/car5/body.emesh",
         "textures/vehicles/car5/body.png",
         0.45f, 1.038f, 2.254f, 1.813f},
        {PlayerCarId::LegacyCar8, "LEGACY", "CAR 8",
         "models/vehicles/car8/body.emesh",
         "textures/vehicles/car8/body.png",
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

inline constexpr std::array<PlayerCarBrand, 11> kPlayerCarBrands{{
    {"ALDER", "ALDER  >", 0u, 3u},
    {"FANG", "FANG  >", 3u, 1u},
    {"GLR", "GLR  >", 4u, 2u},
    {"HALCYON", "HALCYON  >", 6u, 2u},
    {"HARROW", "HARROW  >", 8u, 4u},
    {"LEGACY", "LEGACY  >", 12u, 2u},
    {"MONTROSE", "MONTROSE  >", 14u, 1u},
    {"MUNICIPAL", "MUNICIPAL  >", 15u, 7u},
    {"ORISON", "ORISON  >", 22u, 1u},
    {"SPAGATTI", "SPAGATTI  >", 23u, 1u},
    {"VESPER", "VESPER  >", 24u, 3u},
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
