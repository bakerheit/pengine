#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <glm/glm.hpp>

#include "core/fixed_step.h"
#include "game/player_economy.h"

namespace apricot {

// The Pinatty Savings & Trust heist: what is in the vault, what taking it
// costs, and when it becomes money. docs/design/bank-heist.md is the design;
// this is every rule of it, headless, so bank_heist_tests can drive it against
// a real WantedSystem and a real PlayerEconomy.
//
// Positions are bank-local (city::bank_local_position), like game/bank_vault.h.
// The vault room is x 7..18, z 7..15; the table's top is at 1.12 m.

struct BankHeistPile {
    glm::vec2 centre;  // bank-local x, z
    int64_t value;
    bool on_cart;      // a floor cart of bales, not a table stack
};

inline constexpr std::array<BankHeistPile, 5> kBankHeistPiles{{
    {{11.3f, 10.5f}, 6'500, false},
    {{12.5f, 10.5f}, 6'500, false},
    {{13.7f, 10.5f}, 6'500, false},
    {{15.8f, 13.3f}, 12'000, true},
    {{15.8f, 8.6f}, 12'000, true},
}};
inline constexpr uint8_t kBankHeistAllPiles =
    static_cast<uint8_t>((1u << kBankHeistPiles.size()) - 1u);

constexpr int64_t bank_heist_vault_total() {
    int64_t total = 0;
    for (const BankHeistPile& p : kBankHeistPiles) total += p.value;
    return total;
}

// Standing reach to a pile's centre, horizontal. A table stack is reached from
// either long side of the 1.5 m-deep table; a cart from any face.
inline constexpr float kBankHeistReachM = 1.6f;

// The first grab goes straight to three stars (heat 6); every later pile adds
// a point, so the whole room is four. The alarm is on the cash, not the door.
inline constexpr float kBankHeistAlarmHeat = 6.0f;
inline constexpr float kBankHeistGreedHeat = 1.0f;

// Thirty minutes of play after the alarm, and only once the take is settled.
inline constexpr uint64_t kBankHeistRestockSteps =
    static_cast<uint64_t>(30.0 * 60.0 * kSimHz);

// How long the bell rings at the bank after the alarm trips.
inline constexpr float kBankHeistBellSeconds = 45.0f;

struct BankHeistState {
    uint8_t taken = 0;          // bit i: pile i is gone from the room
    int64_t carried = 0;        // grabbed, not yet money
    bool live = false;          // alarm tripped, take not yet settled
    uint64_t restock_step = 0;  // 0: stocked or restocking not scheduled
    float bell_s = 0.0f;        // the alarm bell's remaining ring
};

inline bool bank_heist_pile_taken(const BankHeistState& s, std::size_t i) {
    return i < kBankHeistPiles.size() && (s.taken & (1u << i)) != 0;
}

// The pile the player can grab from here, or -1. On foot, inside the vault
// room at floor height, nearest untaken pile within reach.
inline int bank_heist_target(const BankHeistState& s, glm::vec3 local, bool on_foot) {
    if (!on_foot || local.y < -0.2f || local.y > 1.0f) return -1;
    if (local.x < 7.3f || local.x > 18.0f || local.z < 7.0f || local.z > 15.0f) return -1;
    int best = -1;
    float best_d = kBankHeistReachM;
    for (std::size_t i = 0; i < kBankHeistPiles.size(); ++i) {
        if (bank_heist_pile_taken(s, i)) continue;
        const float d = glm::distance(glm::vec2{local.x, local.z}, kBankHeistPiles[i].centre);
        if (d <= best_d) { best_d = d; best = static_cast<int>(i); }
    }
    return best;
}

struct BankHeistGrab {
    bool taken = false;
    bool tripped_alarm = false;  // this grab started the heist
    int64_t value = 0;
    float heat = 0.0f;           // for WantedSystem::add_heat
};

// Takes pile `index`. The caller hands `heat` to the wanted system; it is
// computed against the current heat so the first grab always lands on at least
// three stars, whatever the player was already wanted for.
inline BankHeistGrab bank_heist_take(BankHeistState& s, int index, float current_heat,
                                     uint64_t now_step) {
    BankHeistGrab out;
    if (index < 0 || static_cast<std::size_t>(index) >= kBankHeistPiles.size() ||
        bank_heist_pile_taken(s, static_cast<std::size_t>(index))) return out;
    out.taken = true;
    out.value = kBankHeistPiles[static_cast<std::size_t>(index)].value;
    s.taken = static_cast<uint8_t>(s.taken | (1u << static_cast<unsigned>(index)));
    s.carried += out.value;
    if (!s.live) {
        s.live = true;
        out.tripped_alarm = true;
        s.bell_s = kBankHeistBellSeconds;
        if (s.restock_step == 0) s.restock_step = now_step + kBankHeistRestockSteps;
        out.heat = current_heat < kBankHeistAlarmHeat
            ? kBankHeistAlarmHeat - current_heat + 0.001f : kBankHeistGreedHeat;
    } else {
        out.heat = kBankHeistGreedHeat;
    }
    return out;
}

enum class BankHeistEvent { None, Banked, Lost, Restocked };
struct BankHeistStep {
    BankHeistEvent event = BankHeistEvent::None;
    int64_t amount = 0;  // Banked: what earn_cash actually added. Lost: the take.
};

// One sim step. `caught` is an arrest or a death since the last step; it wins
// over a same-step clear, because an arrest resets the wanted level too.
inline BankHeistStep step_bank_heist(BankHeistState& s, PlayerEconomy& economy,
                                     int wanted_level, bool caught,
                                     uint64_t now_step, float dt) {
    BankHeistStep out;
    s.bell_s = s.live && !caught ? std::max(0.0f, s.bell_s - dt) : 0.0f;
    if (s.live) {
        if (caught) {
            out = {BankHeistEvent::Lost, s.carried};
            s.carried = 0;
            s.live = false;
        } else if (wanted_level <= 0) {
            out = {BankHeistEvent::Banked, earn_cash(economy, s.carried)};
            s.carried = 0;
            s.live = false;
        }
        return out;
    }
    if (s.restock_step != 0 && now_step >= s.restock_step) {
        s = {};
        out.event = BankHeistEvent::Restocked;
    }
    return out;
}

// A save carries which piles are gone and when the vault restocks; the take in
// hand and the alarm are not saved, like the wanted level.
inline BankHeistState bank_heist_from_save(uint8_t taken, uint64_t restock_step) {
    BankHeistState s;
    s.taken = static_cast<uint8_t>(taken & kBankHeistAllPiles);
    s.restock_step = restock_step;
    return s;
}

// Taken piles and no scheduled restock cannot happen: the first grab always
// schedules it. A restock further than one cooldown from the save's clock
// cannot happen either.
inline bool valid_bank_heist_save(uint8_t taken, uint64_t restock_step, uint64_t sim_step) {
    if ((taken & ~kBankHeistAllPiles) != 0) return false;
    if (taken != 0 && restock_step == 0) return false;
    return restock_step <= sim_step + kBankHeistRestockSteps;
}

}  // namespace apricot
