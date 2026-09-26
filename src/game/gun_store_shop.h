#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include "city/building_access.h"
#include "city/gun_store.h"
#include "core/input_frame.h"
#include "game/player_economy.h"
#include "game/weapon.h"

namespace apricot {

// The counter at Brassline Arms: what it sells, who it will sell to, and the
// menu the player buys through. Headless, so every rule below is proven by
// tests/gun_store_shop_tests.cpp against the real economy and the real save;
// app/gun_store_counter.cpp only maps keys onto update() and draws the rows.
//
// TWO ITEMS, ON PURPOSE. The pistol, and boxes of rounds for it. Molotovs are
// not sold here — nobody behind a gun counter sells petrol bombs — and their
// stock still refills on respawn (game/weapon_ownership.h says why).

inline constexpr int64_t kGunStorePistolPrice = 500;
inline constexpr int64_t kGunStoreAmmoPrice = 50;
inline constexpr int kGunStoreAmmoBoxRounds = 24;   // two magazines
// The most spare rounds the counter will leave you carrying: twenty magazines.
// Well under the save's 9999 ceiling, which is a sanity bound, not a design.
// Without a gameplay cap a rich player buys a thousand rounds once and the
// ammunition half of the shop never matters again.
inline constexpr int kGunStoreReserveCap = 240;

enum class GunStoreItem : uint8_t { Pistol, PistolAmmo };
inline constexpr std::size_t kGunStoreItemCount = 2;

inline int64_t gun_store_price(GunStoreItem item) {
    return item == GunStoreItem::Pistol ? kGunStorePistolPrice : kGunStoreAmmoPrice;
}
inline const char* gun_store_item_name(GunStoreItem item) {
    return item == GunStoreItem::Pistol ? "PISTOL" : "PISTOL AMMO";
}
inline const char* gun_store_item_detail(GunStoreItem item) {
    return item == GunStoreItem::Pistol ? "9MM SEMI-AUTO, 12-ROUND MAGAZINE"
                                        : "BOX OF 24 ROUNDS";
}

enum class GunStoreRefusal : uint8_t {
    None,
    NotEnoughCash,
    AlreadyOwned,
    NeedPistol,  // ammunition for a gun you do not have
    AmmoFull,    // the box would take the reserve past kGunStoreReserveCap
};

inline const char* gun_store_refusal_text(GunStoreRefusal r) {
    switch (r) {
        case GunStoreRefusal::NotEnoughCash: return "NOT ENOUGH CASH";
        case GunStoreRefusal::AlreadyOwned:  return "ALREADY OWNED";
        case GunStoreRefusal::NeedPistol:    return "BUY THE PISTOL FIRST";
        case GunStoreRefusal::AmmoFull:      return "AMMO FULL";
        case GunStoreRefusal::None:          break;
    }
    return "";
}

// Why this item cannot be bought right now, or None. The same answer greys a
// row out and refuses the purchase, so the menu can never show a row as
// buyable that the purchase then turns down.
inline GunStoreRefusal gun_store_refusal(const PlayerEconomy& economy, int reserve,
                                         GunStoreItem item) {
    if (item == GunStoreItem::Pistol) {
        if (owns_weapon(economy, WeaponId::Pistol)) return GunStoreRefusal::AlreadyOwned;
    } else {
        if (!owns_weapon(economy, WeaponId::Pistol)) return GunStoreRefusal::NeedPistol;
        if (reserve > kGunStoreReserveCap - kGunStoreAmmoBoxRounds) return GunStoreRefusal::AmmoFull;
    }
    if (!can_afford(economy, gun_store_price(item))) return GunStoreRefusal::NotEnoughCash;
    return GunStoreRefusal::None;
}

// The purchase. All or nothing: on any refusal neither the wallet nor the
// reserve moves. The pistol goes through buy_weapon (debit and grant in one
// call); a box is spend_cash then the rounds, after every check has passed.
//
// A bought pistol comes with the rounds the weapon state already carries. On
// a new game that is a full magazine and 48 spare, and nothing can spend them
// before the pistol is owned, so buying the gun hands you a loaded gun.
inline GunStoreRefusal gun_store_buy(PlayerEconomy& economy, int& reserve, GunStoreItem item) {
    const GunStoreRefusal refusal = gun_store_refusal(economy, reserve, item);
    if (refusal != GunStoreRefusal::None) return refusal;
    if (item == GunStoreItem::Pistol) {
        if (!buy_weapon(economy, WeaponId::Pistol, kGunStorePistolPrice))
            return GunStoreRefusal::NotEnoughCash;  // unreachable after the check
        return GunStoreRefusal::None;
    }
    if (!spend_cash(economy, kGunStoreAmmoPrice)) return GunStoreRefusal::NotEnoughCash;
    reserve += kGunStoreAmmoBoxRounds;
    return GunStoreRefusal::None;
}

// WHERE YOU CAN SHOP FROM: standing on the shop floor, on the customer side
// of the counter, in front of it. Site-local, the same frame the counter is
// authored in (city/gun_store.h: the cabinet spans x ±5, z -7.0..-5.8). The
// strip is inside the shell on every side — the front wall is at z=+2, the
// sides at x=±8, the rear at z=-10 — so no point outside the building, on its
// roof, or behind the counter is in it. That is the "through a wall" rule: it
// is not a line-of-sight test that could leak, it is a box that no wall
// crosses. The island display (x -4.8..-3.2, z -4.0..-1.4) starts where it ends.
inline constexpr float kGunStoreZoneHalfWidth = 4.6f;
inline constexpr float kGunStoreZoneFront = -4.0f;   // toward the door
inline constexpr float kGunStoreZoneBack = -5.8f;    // the counter's face
inline constexpr float kGunStoreFloorLift = 0.2f;    // floor top above site ground

inline glm::vec2 gun_store_local(glm::vec3 world) {
    return city::access_local(city::kGunStoreSite, {world.x, world.z});
}
inline glm::vec3 gun_store_world(glm::vec2 local, float lift = kGunStoreFloorLift) {
    const glm::vec2 p = city::access_world(city::kGunStoreSite, local);
    return {p.x, city::kGunStoreSite.ground_m + lift, p.y};
}

// `feet` is the character's position, which is at its feet.
inline bool at_gun_store_counter(glm::vec3 feet, bool on_foot) {
    if (!on_foot) return false;
    const glm::vec2 p = gun_store_local(feet);
    const float floor = city::kGunStoreSite.ground_m + kGunStoreFloorLift;
    return std::fabs(p.x) <= kGunStoreZoneHalfWidth && p.y <= kGunStoreZoneFront &&
           p.y >= kGunStoreZoneBack && feet.y > floor - 0.3f && feet.y < floor + 0.6f;
}

// The menu. Rows are the two items then LEAVE. Accept on a buyable item asks
// to confirm; Accept again pays; Back from the confirm cancels without
// charging; Back from the list, or Accept on LEAVE, closes the counter.
// A refused item never reaches the confirm: the refusal shows at once.
enum class GunStoreMenuResult : uint8_t { None, Moved, Confirming, Bought, Refused, Cancelled, Closed };

struct GunStoreMenu {
    static constexpr std::size_t kRows = kGunStoreItemCount + 1;  // + LEAVE
    static constexpr std::size_t kLeaveRow = kGunStoreItemCount;

    bool open = false;
    std::size_t selection = 0;
    bool confirming = false;
    // The line under the rows: what the last action did.
    GunStoreRefusal refusal = GunStoreRefusal::None;
    bool bought = false;
    GunStoreItem last_item = GunStoreItem::Pistol;

    void begin() { *this = GunStoreMenu{}; open = true; }
    void close() { open = false; confirming = false; }

    static GunStoreItem item_at(std::size_t row) { return static_cast<GunStoreItem>(row); }

    GunStoreMenuResult update(uint32_t pressed, PlayerEconomy& economy, int& reserve) {
        if (!open) return GunStoreMenuResult::None;
        if (confirming) {
            if (pressed & kBtnBack) {
                confirming = false;
                refusal = GunStoreRefusal::None;
                bought = false;
                return GunStoreMenuResult::Cancelled;
            }
            if (pressed & kBtnAccept) {
                confirming = false;
                last_item = item_at(selection);
                refusal = gun_store_buy(economy, reserve, last_item);
                bought = refusal == GunStoreRefusal::None;
                return bought ? GunStoreMenuResult::Bought : GunStoreMenuResult::Refused;
            }
            return GunStoreMenuResult::None;
        }
        if (pressed & kBtnBack) { close(); return GunStoreMenuResult::Closed; }
        if (pressed & (kBtnMenuUp | kBtnMenuDown)) {
            if (pressed & kBtnMenuUp) selection = (selection + kRows - 1) % kRows;
            if (pressed & kBtnMenuDown) selection = (selection + 1) % kRows;
            refusal = GunStoreRefusal::None;
            bought = false;
            return GunStoreMenuResult::Moved;
        }
        if (pressed & kBtnAccept) {
            if (selection == kLeaveRow) { close(); return GunStoreMenuResult::Closed; }
            last_item = item_at(selection);
            bought = false;
            refusal = gun_store_refusal(economy, reserve, last_item);
            if (refusal != GunStoreRefusal::None) return GunStoreMenuResult::Refused;
            confirming = true;
            return GunStoreMenuResult::Confirming;
        }
        return GunStoreMenuResult::None;
    }
};

}  // namespace apricot
