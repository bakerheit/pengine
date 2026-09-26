#pragma once
#include "game/player_economy.h"
#include "game/weapon.h"

namespace apricot {

// You can only hold what you own. These three functions are the whole rule,
// and every path that picks a weapon goes through one of them.
//
// WHY A GATE ON THE WHEEL *AND* A GATE ON THE STEP. The wheel is the one
// place a player chooses a weapon, so it refuses an unowned one there, where
// the refusal can be seen. But `equipped` is a plain field, and QA scripts,
// the police check and any future cheat or pickup can write it directly. A
// rule enforced only where the player normally chooses is a rule the next
// shortcut walks straight past. enforce_weapon_ownership() runs every frame
// before the weapons step, so an unowned weapon never gets one tick in hand.

// Release of the wheel. Equips the hovered weapon only when it is owned; an
// unowned sector is shown locked and releasing over it keeps what you had.
inline void close_weapon_wheel(WeaponWheel& wheel, const PlayerEconomy& economy,
                               bool confirm) {
    const WeaponId before = wheel.equipped;
    wheel.close(confirm);
    if (!owns_weapon(economy, wheel.equipped)) wheel.equipped = before;
    // `before` is owned unless something else broke the rule; the per-frame
    // gate below catches that case too.
    if (!owns_weapon(economy, wheel.equipped)) wheel.equipped = WeaponId::Unarmed;
}

// The per-frame backstop. Returns true when it had to holster something. The
// wheel's hover is left alone: pointing at a locked sector is allowed (it is
// drawn locked), only equipping it is not.
inline bool enforce_weapon_ownership(WeaponWheel& wheel, const PlayerEconomy& economy) {
    if (owns_weapon(economy, wheel.equipped)) return false;
    wheel.equipped = WeaponId::Unarmed;
    return true;
}

// DEATH KEEPS WHAT YOU BOUGHT. Ownership is untouched (it lives in the
// economy, not here), and the pistol keeps the rounds it was carrying: the
// magazine and the reserve survive, only the draw, the reload in progress and
// the recoil are dropped. Refilling the pistol on respawn — what death did
// before there was a shop — would make the ammunition the gun store sells
// worth nothing, because dying would be the cheapest box of rounds in town.
//
// The molotov is NOT sold, so its stock still comes back full on respawn;
// taking it away would leave a player who cannot buy more with none at all.
inline WeaponUseState respawn_weapon_use(const WeaponUseState& before) {
    WeaponUseState after{};
    after.magazine = before.magazine;
    after.reserve = before.reserve;
    return after;
}

}  // namespace apricot
