#pragma once
#include <algorithm>
#include <cstdint>

#include "game/weapon.h"

namespace apricot {

// The player's money and what they own. One struct, because every consumer
// needs both halves at once: a shop debits the wallet and grants the item in
// the same step, and a save has to store the pair or a load can hand back the
// cash for a gun the player kept.
//
// Cash is whole dollars, never negative. Every change goes through the
// functions below, so there is exactly one place a balance can go wrong.

inline constexpr int64_t kMaxCash = 999'999'999;  // nine digits: the HUD's width
inline constexpr int64_t kNewGameCash = 250;      // short of a pistol; see game/wallet_rules.h

inline constexpr uint8_t weapon_bit(WeaponId id) {
    return static_cast<uint8_t>(1u << static_cast<unsigned>(id));
}
inline constexpr uint8_t kAllWeaponBits = static_cast<uint8_t>((1u << kWeaponSlotCount) - 1u);

// Before the economy, every save could select every weapon. A new game starts
// with bare hands and molotovs; the pistol is the gun store's to sell.
inline constexpr uint8_t kLegacyOwnedWeapons = kAllWeaponBits;
inline constexpr uint8_t kNewGameOwnedWeapons =
    static_cast<uint8_t>(weapon_bit(WeaponId::Unarmed) | weapon_bit(WeaponId::Molotov));

struct PlayerEconomy {
    int64_t cash = kNewGameCash;
    uint8_t owned_weapons = kNewGameOwnedWeapons;
};

inline bool owns_weapon(const PlayerEconomy& e, WeaponId id) {
    // Bare hands cannot be sold, lost or confiscated.
    return id == WeaponId::Unarmed || (e.owned_weapons & weapon_bit(id)) != 0;
}

inline bool can_afford(const PlayerEconomy& e, int64_t price) {
    return price >= 0 && e.cash >= price;
}

// Adds a reward, clamped at kMaxCash. Returns what was actually added, so a
// payout banner can show the real figure when the wallet is nearly full.
inline int64_t earn_cash(PlayerEconomy& e, int64_t amount) {
    if (amount <= 0) return 0;
    const int64_t added = std::min(amount, kMaxCash - e.cash);
    e.cash += added;
    return added;
}

// A purchase: all or nothing. Debits only when the whole price is there.
inline bool spend_cash(PlayerEconomy& e, int64_t price) {
    if (price < 0 || !can_afford(e, price)) return false;
    e.cash -= price;
    return true;
}

// A penalty (fine, hospital bill): takes what it can and never goes negative.
// Returns what was actually taken.
inline int64_t charge_cash(PlayerEconomy& e, int64_t amount) {
    if (amount <= 0) return 0;
    const int64_t taken = std::min(amount, e.cash);
    e.cash -= taken;
    return taken;
}

// Debit and grant in one step, so no path pays without receiving or receives
// without paying. False, and nothing changes, if it is owned or unaffordable.
inline bool buy_weapon(PlayerEconomy& e, WeaponId id, int64_t price) {
    if (owns_weapon(e, id) || !spend_cash(e, price)) return false;
    e.owned_weapons = static_cast<uint8_t>(e.owned_weapons | weapon_bit(id));
    return true;
}

inline bool valid_player_economy(const PlayerEconomy& e) {
    return e.cash >= 0 && e.cash <= kMaxCash &&
           (e.owned_weapons & ~kAllWeaponBits) == 0 &&
           (e.owned_weapons & weapon_bit(WeaponId::Unarmed)) != 0;
}

}  // namespace apricot
