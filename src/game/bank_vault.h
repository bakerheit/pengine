#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

#include <glm/glm.hpp>

namespace apricot {

inline constexpr char kBankVaultCode[] = "7491";
inline constexpr glm::vec2 kBankNotePosition{-12.5f, 11.25f};
inline constexpr glm::vec2 kBankKeypadPosition{6.65f, 9.0f};
inline constexpr glm::vec2 kBankInsideControl{8.2f, 13.4f};
inline constexpr glm::vec2 kBankVaultHinge{7.0f, 10.2f};

enum class BankTarget { None, Note, Vault };
struct BankVaultState {
    bool unlocked = false;
    bool target_open = false;
    bool note_read = false;
    bool blocked = false;
    float openness = 0.0f;
};

inline BankTarget bank_target(glm::vec3 local, bool on_foot) {
    if (!on_foot || local.y < -0.2f || local.y > 1.0f) return BankTarget::None;
    const glm::vec2 p{local.x, local.z};
    if (local.x < -7.2f && local.z > 7.2f &&
        glm::distance(p, kBankNotePosition) < 2.7f) return BankTarget::Note;
    if ((local.x < 6.7f && local.z > 7.2f &&
         glm::distance(p, kBankKeypadPosition) < 2.3f) ||
        (local.x > 7.3f && local.z > 12.9f &&
         glm::distance(p, kBankInsideControl) < 1.7f)) return BankTarget::Vault;
    return BankTarget::None;
}

inline bool bank_vault_submit(BankVaultState& state, std::string_view code) {
    if (code != kBankVaultCode) return false;
    state.unlocked = true;
    state.target_open = true;
    return true;
}

inline bool bank_vault_toggle(BankVaultState& state) {
    if (!state.unlocked) return false;
    state.target_open = !state.target_open;
    return true;
}

// Conservative quarter-swing safety envelope. Stop both directions while a
// body is inside it; the controls are outside this area on either side.
inline bool bank_vault_swing_occupied(glm::vec3 local, float radius) {
    return local.y < 3.4f && local.y > -1.0f &&
           local.x + radius > 6.75f && local.x - radius < 9.85f &&
           local.z + radius > 9.95f && local.z - radius < 13.05f;
}

inline void step_bank_vault(BankVaultState& state, float dt, bool occupied) {
    const float target = state.unlocked && state.target_open ? 1.0f : 0.0f;
    state.blocked = occupied && std::fabs(target - state.openness) > 0.0001f;
    if (state.blocked || dt <= 0.0f || !std::isfinite(dt)) return;
    const float delta = dt / 2.2f;
    state.openness += std::clamp(target - state.openness, -delta, delta);
    state.openness = std::clamp(state.openness, 0.0f, 1.0f);
}

}  // namespace apricot
