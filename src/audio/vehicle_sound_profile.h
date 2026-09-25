#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace apricot {

// Shared car-sound trim. Tire screech emitters keep their existing gain.
inline constexpr float kVehicleSoundGain = .75f;

// Presentation-only tuning of existing recordings. One multiplier across
// ignition, idle, attack, hold and release keeps each engine's voice coherent.
struct VehicleSoundProfile {
    std::string_view model_key;
    float pitch = 1.0f;
};

inline constexpr std::array<VehicleSoundProfile, 33> kVehicleSoundProfiles{{
    {"bwc_360", 1.02f},
    {"rodeo_grazer", .915f},
    {"ember_gt", 1.105f},
    {"alder_pip", 1.08f},
    {"alder_wayfarer", 0.98f},
    {"alder_ridge", 0.94f},
    {"glm_lunge", 1.06f},
    {"glm_zip", 1.12f},
    {"halcyon_six", 0.92f},
    {"halcyon_sovereign", .86f},
    {"harrow_workman", 0.88f},
    {"harrow_cityliner", 0.81f},
    {"harrow_hauler", 0.83f},
    {"harrow_parcel", 0.85f},
    {"car5", 1.00f},
    {"car5_next", 1.03f},
    {"car5_next_police", 0.96f},
    {"car8", 0.90f},
    {"montrose_regent_eight", 0.84f},
    {"ambulance", 0.82f},
    {"firetruck", 0.80f},
    {"vesper_vx91", 1.04f},
    {"vesper_mistral", 1.10f},
    {"vesper_scythe", 1.11f},
    {"orison_cinder", 1.09f},
    {"spagatti_shu", 1.07f},
    {"municipal_cruiser_91a", .93f},
    {"municipal_cruiser_91b", .95f},
    {"municipal_cruiser_91c", .97f},
    {"municipal_cruiser_91d", .99f},
    {"municipal_cruiser_91e", 1.01f},
    {"fang_venom_v2", 1.115f},
    {"saddle_tango", 1.045f},
}};

// Accept a canonical lowercase model key or a relative/absolute mesh path.
// Exact path components avoid accidental matches such as car5_custom.
inline constexpr VehicleSoundProfile vehicle_sound_profile(std::string_view model) {
    while (!model.empty()) {
        const auto separator = model.find_first_of("/\\");
        const auto component = model.substr(0, separator);
        for (const auto& profile : kVehicleSoundProfiles) {
            if (component == profile.model_key) return profile;
        }
        if (separator == std::string_view::npos) break;
        model.remove_prefix(separator + 1);
    }
    return {"default", 1.0f};
}

// Preserve lane/slot variation. No frame number, submit order or mutable RNG
// participates, so a streamed/reordered actor keeps its voice.
inline float traffic_idle_pitch(uint64_t lane, uint32_t slot, float speed,
                                VehicleSoundProfile profile) {
    const float motion = std::clamp(std::abs(speed) / 10.0f, 0.0f, 1.0f);
    return profile.pitch * (0.97f + 0.01f * static_cast<float>((lane ^ slot) % 7)
                            + 0.08f * motion);
}

}  // namespace apricot
