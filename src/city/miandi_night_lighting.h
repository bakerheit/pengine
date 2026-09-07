#pragma once

#include <array>

namespace apricot::city {

// This stays deliberately renderer-free.  The world integration translates
// these authored values to its current spotlight representation at runtime.
struct MiandiNightLightVec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct MiandiNightLight {
    const char* name = "";
    MiandiNightLightVec3 world_position{};
    MiandiNightLightVec3 world_direction{};
    MiandiNightLightVec3 linear_rgb{};
    float range_m = 0.0f;
    float base_power = 0.0f;
    float outer_cone_cos = 0.0f;
};

// Six short, wide venue lights per parcel, plus four for Ocean Drive's actual
// beachfront hotel frontage. Positions stay inside their owned sites; the 8..22 m
// ranges keep their pools off nearby roads.
inline constexpr std::array<MiandiNightLight, 28> kMiandiNightLights{{
    {"calle noche candela canopy", {6952.0f, 14.0f, 8450.0f}, {0.0f, -0.8f, -0.6f},
     {1.00f, 0.16f, 0.34f}, 14.0f, 4.4f, 0.62f},
    {"calle noche tropico facade", {7003.0f, 15.0f, 8450.0f}, {0.0f, -0.8f, -0.6f},
     {0.20f, 0.82f, 1.00f}, 16.0f, 4.8f, 0.68f},
    {"calle noche cafecito awning", {7049.0f, 13.0f, 8450.0f}, {0.0f, -0.8f, -0.6f},
     {1.00f, 0.56f, 0.18f}, 12.0f, 3.6f, 0.64f},
    {"calle noche music court", {7021.0f, 15.0f, 8543.0f}, {0.0f, -1.0f, 0.0f},
     {0.88f, 0.18f, 0.72f}, 18.0f, 5.2f, 0.58f},
    {"calle noche domino patio", {6955.0f, 13.0f, 8515.0f}, {0.0f, -1.0f, 0.0f},
    {1.00f, 0.72f, 0.28f}, 11.0f, 3.2f, 0.70f},
    {"calle noche dance strings", {7021.0f, 14.0f, 8530.0f}, {0.0f, -1.0f, 0.0f},
     {0.28f, 0.78f, 1.00f}, 13.0f, 3.6f, 0.66f},

    // Club Mirage's six, re-aimed by the detail pass into one hierarchy:
    // sign, then threshold, then facade, then mural, then the two yards. The
    // first pass pointed four of them down and NORTH, away from the building,
    // so they lit empty paving and left a brick shed black — the same mistake
    // the Ocean Drive lights were moved out and turned around to fix.
    {"club mirage roof sign wash", {7215.0f, 27.5f, 8456.0f},
     {0.0f, -0.735150f, 0.677905f}, {0.74f, 0.30f, 1.00f}, 14.0f, 4.4f, 0.66f},
    {"club mirage club door", {7215.0f, 13.6f, 8455.0f}, {0.0f, -1.0f, 0.0f},
     {0.12f, 0.88f, 1.00f}, 11.0f, 4.6f, 0.68f},
    {"club mirage north facade wash", {7157.0f, 15.0f, 8452.0f},
     {0.0f, -0.498220f, 0.867051f}, {0.42f, 0.20f, 1.00f}, 16.0f, 4.6f, 0.60f},
    {"club mirage mural court", {7136.0f, 15.5f, 8500.0f},
     {0.832050f, -0.554700f, 0.0f}, {0.66f, 0.28f, 1.00f}, 18.0f, 5.0f, 0.56f},
    {"club mirage food yard", {7169.0f, 14.0f, 8535.0f}, {0.0f, -1.0f, 0.0f},
     {1.00f, 0.74f, 0.38f}, 15.0f, 4.2f, 0.60f},
    {"club mirage loading edge", {7260.0f, 13.0f, 8550.0f}, {0.0f, -1.0f, 0.0f},
    {0.32f, 0.76f, 0.92f}, 10.0f, 3.0f, 0.72f},

    {"mariposa motor court", {7800.0f, 14.0f, 8514.0f}, {0.0f, -1.0f, 0.0f},
     {0.16f, 0.92f, 1.00f}, 18.0f, 5.0f, 0.58f},
    {"mariposa pool deck", {7753.0f, 13.0f, 8561.0f}, {0.0f, -1.0f, 0.0f},
     {0.18f, 0.66f, 1.00f}, 16.0f, 4.4f, 0.60f},
    {"mariposa lobby facade", {7800.0f, 14.0f, 8452.0f}, {0.0f, -0.8f, -0.6f},
     {1.00f, 0.34f, 0.28f}, 14.0f, 4.2f, 0.66f},
    {"mariposa gallery west", {7766.0f, 14.0f, 8500.0f}, {0.6f, -0.8f, 0.0f},
     {1.00f, 0.82f, 0.52f}, 11.0f, 3.4f, 0.70f},
    {"mariposa pylon wash", {7866.0f, 20.0f, 8466.0f}, {-0.6f, -0.8f, 0.0f},
    {0.88f, 0.92f, 1.00f}, 13.0f, 3.8f, 0.64f},
    {"mariposa pool umbrellas", {7742.0f, 13.0f, 8561.0f}, {0.0f, -1.0f, 0.0f},
     {0.28f, 0.82f, 1.00f}, 12.0f, 3.6f, 0.68f},

    {"palmera lobby wash", {8033.0f, 14.0f, 8486.0f}, {0.6f, -0.8f, 0.0f},
     {1.00f, 0.32f, 0.54f}, 16.0f, 4.8f, 0.64f},
    {"palmera stepped bay", {8021.0f, 20.0f, 8486.0f}, {0.6f, -0.8f, 0.0f},
     {0.28f, 0.58f, 1.00f}, 18.0f, 5.2f, 0.60f},
    {"palmera cabana court", {8033.0f, 14.0f, 8549.0f}, {0.6f, -0.8f, 0.0f},
     {1.00f, 0.66f, 0.28f}, 14.0f, 4.0f, 0.66f},
    {"palmera pool terrace", {7952.0f, 14.0f, 8549.0f}, {0.0f, -1.0f, 0.0f},
     {0.16f, 0.78f, 1.00f}, 17.0f, 4.6f, 0.58f},
    {"palmera roofline wash", {8031.0f, 28.0f, 8498.0f}, {0.6f, -0.8f, 0.0f},
    {1.00f, 0.80f, 0.46f}, 10.0f, 3.2f, 0.72f},
    {"palmera cabana string", {8005.0f, 14.0f, 8535.0f}, {0.0f, -1.0f, 0.0f},
    {1.00f, 0.28f, 0.56f}, 13.0f, 3.6f, 0.66f},

    // Mount outside the east faces and aim back at the stucco. A light on the
    // wall aimed east only lit the lawn and left the hotel itself black.
    {"Bellmar beach marquee", {8084.0f, 22.0f, 8257.0f},
     {-0.6f, -0.8f, 0.0f}, {1.00f, 0.025f, 0.34f}, 22.0f, 7.6f, 0.50f},
    {"Bellmar Ocean Drive halo", {8084.0f, 22.0f, 8285.0f},
     {-0.6f, -0.8f, 0.0f}, {0.035f, 0.70f, 1.00f}, 22.0f, 7.6f, 0.50f},
    {"Maravelle beach marquee", {8084.0f, 20.0f, 8314.0f},
     {-0.6f, -0.8f, 0.0f}, {0.035f, 0.70f, 1.00f}, 22.0f, 7.6f, 0.50f},
    {"Maravelle Ocean Drive halo", {8084.0f, 20.0f, 8342.0f},
     {-0.6f, -0.8f, 0.0f}, {1.00f, 0.025f, 0.34f}, 22.0f, 7.6f, 0.50f},
}};

// A NaN is treated like day rather than allowed to reach the renderer.
constexpr float miandi_night_light_power(const MiandiNightLight& light,
                                         float night_level) {
    const float clamped_level = night_level >= 1.0f
                                    ? 1.0f
                                    : (night_level > 0.0f ? night_level : 0.0f);
    return clamped_level == 0.0f ? 0.0f : light.base_power * clamped_level;
}

}  // namespace apricot::city
