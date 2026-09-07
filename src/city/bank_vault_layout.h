#pragma once

#include "city/start_area.h"
#include "game/bank_vault.h"

namespace apricot::city {

inline glm::vec3 bank_local_position(glm::vec3 world) {
    const float x = world.x - kBankSite.origin.x;
    const float z = world.z - kBankSite.origin.z;
    return {kBankSite.cos_yaw * x - kBankSite.sin_yaw * z,
            world.y - kBankSite.ground_m,
            kBankSite.sin_yaw * x + kBankSite.cos_yaw * z};
}

inline std::vector<StartPart> bank_vault_leaf(float openness) {
    std::vector<StartPart> parts{
        {"bank vault moving slab", {7.0f, 11.5f}, 0.23f, 0.34f, 2.96f, 2.60f,
         StartFinish::Steel, true},
        {"bank vault moving face", {6.79f, 11.5f}, 0.39f, 2.34f, 2.64f, 0.04f,
         StartFinish::White, false, 0.0f, -90.0f, 0.0f},
        {"bank vault moving rear face", {7.21f, 11.5f}, 0.39f, 2.34f, 2.64f, 0.04f,
         StartFinish::White, false, 0.0f, 90.0f, 0.0f},
        {"bank vault wheel hub", {6.61f, 11.5f}, 1.43f, 0.25f, 0.25f, 0.25f,
         StartFinish::Yellow, false, 0.0f, 0.0f, 90.0f},
    };
    constexpr float pi = 3.14159265359f;
    for (int i = 0; i < 8; ++i) {
        const float a = static_cast<float>(i) * pi / 4.0f;
        parts.push_back({"bank vault wheel rim",
            {6.56f, 11.5f + std::cos(a) * 0.43f},
            1.56f + std::sin(a) * 0.43f - 0.18f,
            0.08f, 0.36f, 0.08f, StartFinish::Steel, false,
            -static_cast<float>(i) * 45.0f, 0.0f, 0.0f});
    }
    for (int i = 0; i < 3; ++i) {
        parts.push_back({"bank vault wheel spoke", {6.56f, 11.5f},
            1.13f, 0.07f, 0.86f, 0.07f, StartFinish::Yellow, false,
            static_cast<float>(i) * 60.0f, 0.0f, 0.0f});
    }
    const float angle = std::clamp(openness, 0.0f, 1.0f) * pi * 0.5f;
    for (StartPart& p : parts) {
        const float x = p.centre.x - kBankVaultHinge.x;
        const float z = p.centre.z - kBankVaultHinge.y;
        p.centre = {kBankVaultHinge.x + std::cos(angle) * x + std::sin(angle) * z,
                    kBankVaultHinge.y - std::sin(angle) * x + std::cos(angle) * z};
        p.yaw_deg += angle * 180.0f / pi;
    }
    return parts;
}

}  // namespace apricot::city
