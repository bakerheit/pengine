// Brassline Arms' counter: prices, refusals, the confirm step, the customer
// zone, the ammunition cap, and that a purchase survives the real save and
// still equips through the real ownership gate afterwards.
#include <string>

#include "game/gun_store_shop.h"
#include "game/save_game.h"
#include "game/weapon_ownership.h"
#include "test_assert.h"

using namespace apricot;
namespace {

// Presses the way the counter sees them: one edge per frame.
GunStoreMenuResult press(GunStoreMenu& menu, uint32_t button, PlayerEconomy& e, int& reserve) {
    return menu.update(button, e, reserve);
}

void exact_funds_buy_the_pistol() {
    PlayerEconomy e;
    e.cash = kGunStorePistolPrice;
    int reserve = WeaponUseState::kInitialReserve;
    GunStoreMenu menu;
    menu.begin();
    REQUIRE(menu.selection == 0);  // the pistol row
    REQUIRE(press(menu, kBtnAccept, e, reserve) == GunStoreMenuResult::Confirming);
    REQUIRE(e.cash == kGunStorePistolPrice);  // asking to confirm costs nothing
    REQUIRE(press(menu, kBtnAccept, e, reserve) == GunStoreMenuResult::Bought);
    REQUIRE(e.cash == 0 && owns_weapon(e, WeaponId::Pistol));
    REQUIRE(menu.bought && menu.open);
    REQUIRE(reserve == WeaponUseState::kInitialReserve);  // the gun is not a box of rounds
    apricot_test::pass("exact funds buy the pistol, and only on the confirm");
}

void one_dollar_short_moves_nothing() {
    PlayerEconomy e;
    e.cash = kGunStorePistolPrice - 1;
    int reserve = 48;
    REQUIRE(gun_store_refusal(e, reserve, GunStoreItem::Pistol) == GunStoreRefusal::NotEnoughCash);
    GunStoreMenu menu;
    menu.begin();
    REQUIRE(press(menu, kBtnAccept, e, reserve) == GunStoreMenuResult::Refused);
    REQUIRE(!menu.confirming && menu.refusal == GunStoreRefusal::NotEnoughCash);
    REQUIRE(std::string(gun_store_refusal_text(menu.refusal)) == "NOT ENOUGH CASH");
    REQUIRE(e.cash == kGunStorePistolPrice - 1 && !owns_weapon(e, WeaponId::Pistol));
    // And the direct purchase path refuses the same way.
    REQUIRE(gun_store_buy(e, reserve, GunStoreItem::Pistol) == GunStoreRefusal::NotEnoughCash);
    REQUIRE(e.cash == kGunStorePistolPrice - 1 && !owns_weapon(e, WeaponId::Pistol));
    apricot_test::pass("one dollar short: refused before the confirm, wallet and ownership untouched");
}

void a_second_pistol_is_refused() {
    PlayerEconomy e;
    e.cash = 5000;
    int reserve = 48;
    REQUIRE(gun_store_buy(e, reserve, GunStoreItem::Pistol) == GunStoreRefusal::None);
    REQUIRE(e.cash == 5000 - kGunStorePistolPrice);
    REQUIRE(gun_store_buy(e, reserve, GunStoreItem::Pistol) == GunStoreRefusal::AlreadyOwned);
    REQUIRE(e.cash == 5000 - kGunStorePistolPrice);
    // A legacy save owns it already, so the counter will not charge it either.
    PlayerEconomy legacy;
    legacy.owned_weapons = kLegacyOwnedWeapons;
    legacy.cash = 5000;
    REQUIRE(gun_store_buy(legacy, reserve, GunStoreItem::Pistol) == GunStoreRefusal::AlreadyOwned);
    REQUIRE(legacy.cash == 5000);
    apricot_test::pass("a pistol you own is never sold to you twice");
}

void cancel_charges_nothing() {
    PlayerEconomy e;
    e.cash = 1000;
    int reserve = 48;
    GunStoreMenu menu;
    menu.begin();
    REQUIRE(press(menu, kBtnAccept, e, reserve) == GunStoreMenuResult::Confirming);
    REQUIRE(press(menu, kBtnBack, e, reserve) == GunStoreMenuResult::Cancelled);
    REQUIRE(menu.open && !menu.confirming);  // back to the list, not out of the shop
    REQUIRE(e.cash == 1000 && !owns_weapon(e, WeaponId::Pistol));
    // Movement while confirming does nothing; only Accept or Back decide.
    REQUIRE(press(menu, kBtnAccept, e, reserve) == GunStoreMenuResult::Confirming);
    REQUIRE(press(menu, kBtnMenuDown, e, reserve) == GunStoreMenuResult::None);
    REQUIRE(menu.selection == 0 && menu.confirming);
    REQUIRE(press(menu, kBtnBack, e, reserve) == GunStoreMenuResult::Cancelled);
    // Back from the list closes the counter; LEAVE does too.
    REQUIRE(press(menu, kBtnBack, e, reserve) == GunStoreMenuResult::Closed);
    REQUIRE(!menu.open);
    menu.begin();
    press(menu, kBtnMenuUp, e, reserve);  // wraps to LEAVE
    REQUIRE(menu.selection == GunStoreMenu::kLeaveRow);
    REQUIRE(press(menu, kBtnAccept, e, reserve) == GunStoreMenuResult::Closed);
    REQUIRE(!menu.open && e.cash == 1000);
    // A closed menu ignores everything.
    REQUIRE(press(menu, kBtnAccept, e, reserve) == GunStoreMenuResult::None);
    apricot_test::pass("cancel at the confirm charges nothing; Back and LEAVE close the counter");
}

void ammunition_needs_the_gun_and_stops_at_the_cap() {
    PlayerEconomy e;
    e.cash = 100000;
    int reserve = 48;
    REQUIRE(gun_store_buy(e, reserve, GunStoreItem::PistolAmmo) == GunStoreRefusal::NeedPistol);
    REQUIRE(e.cash == 100000 && reserve == 48);
    REQUIRE(gun_store_buy(e, reserve, GunStoreItem::Pistol) == GunStoreRefusal::None);
    const int64_t after_gun = e.cash;
    REQUIRE(gun_store_buy(e, reserve, GunStoreItem::PistolAmmo) == GunStoreRefusal::None);
    REQUIRE(reserve == 48 + kGunStoreAmmoBoxRounds && e.cash == after_gun - kGunStoreAmmoPrice);
    int boxes = 1;
    while (gun_store_buy(e, reserve, GunStoreItem::PistolAmmo) == GunStoreRefusal::None) ++boxes;
    REQUIRE(reserve <= kGunStoreReserveCap);
    REQUIRE(reserve > kGunStoreReserveCap - kGunStoreAmmoBoxRounds);
    REQUIRE(e.cash == after_gun - kGunStoreAmmoPrice * boxes);  // every box paid, none extra
    const int64_t full_cash = e.cash;
    REQUIRE(gun_store_buy(e, reserve, GunStoreItem::PistolAmmo) == GunStoreRefusal::AmmoFull);
    REQUIRE(e.cash == full_cash);
    // A reserve already past the cap (an old save, say) is full, never trimmed.
    int big = 900;
    REQUIRE(gun_store_buy(e, big, GunStoreItem::PistolAmmo) == GunStoreRefusal::AmmoFull);
    REQUIRE(big == 900 && e.cash == full_cash);
    // Exact funds for one box, then nothing left for a second.
    PlayerEconomy poor;
    poor.owned_weapons = kLegacyOwnedWeapons;
    poor.cash = kGunStoreAmmoPrice;
    int r = 0;
    REQUIRE(gun_store_buy(poor, r, GunStoreItem::PistolAmmo) == GunStoreRefusal::None);
    REQUIRE(poor.cash == 0 && r == kGunStoreAmmoBoxRounds);
    REQUIRE(gun_store_buy(poor, r, GunStoreItem::PistolAmmo) == GunStoreRefusal::NotEnoughCash);
    REQUIRE(r == kGunStoreAmmoBoxRounds);
    apricot_test::pass("ammo needs the pistol, costs per box and stops at the carry cap");
}

void only_the_customer_side_of_the_counter_shops() {
    const auto at = [](float x, float z, float lift = kGunStoreFloorLift) {
        return at_gun_store_counter(gun_store_world({x, z}, lift), true);
    };
    REQUIRE(at(0, -4.7f));       // where the walk test stands, facing the clerk
    REQUIRE(at(0, -5.65f));      // pressed against the counter
    REQUIRE(at(-4.4f, -5.2f) && at(4.4f, -5.2f));
    REQUIRE(!at(0, -7.5f));      // behind the counter, on the clerk's side
    REQUIRE(!at(5.8f, -4.7f));   // the staff passage round the counter's end
    REQUIRE(!at(0, -10.6f));     // outside the rear wall, behind the clerk
    REQUIRE(!at(-8.4f, -5.f));   // outside the west wall, level with the counter
    REQUIRE(!at(0, -1.f));       // browsing the shop floor, too far from the counter
    REQUIRE(!at(0, 3.2f));       // the storefront walk, through the front wall
    REQUIRE(!at(0, -4.7f, 4.4f));  // on the roof above the counter
    REQUIRE(!at(0, -4.7f, -1.0f)); // under the floor
    REQUIRE(!at_gun_store_counter(gun_store_world({0, -4.7f}), false));  // in a car
    apricot_test::pass("the counter serves only the shop floor in front of it: not the staff side, "
                       "the roof, or anywhere through a wall");
}

void a_purchase_survives_the_save_and_equips() {
    // A new game: no pistol, so the wheel cannot equip it.
    PlayerEconomy e;
    e.cash = 1000;
    WeaponUseState use;
    WeaponWheel wheel;
    wheel.begin();
    wheel.hovered = WeaponId::Pistol;
    close_weapon_wheel(wheel, e, true);
    REQUIRE(wheel.equipped == WeaponId::Unarmed);
    // The direct write is caught by the per-frame gate.
    wheel.equipped = WeaponId::Pistol;
    REQUIRE(enforce_weapon_ownership(wheel, e));
    REQUIRE(wheel.equipped == WeaponId::Unarmed);
    // Molotovs are a new game's, and equip as before.
    wheel.begin();
    wheel.hovered = WeaponId::Molotov;
    close_weapon_wheel(wheel, e, true);
    REQUIRE(wheel.equipped == WeaponId::Molotov);
    // Releasing over the locked pistol keeps the molotov in hand.
    wheel.begin();
    wheel.hovered = WeaponId::Pistol;
    close_weapon_wheel(wheel, e, true);
    REQUIRE(wheel.equipped == WeaponId::Molotov);

    REQUIRE(gun_store_buy(e, use.reserve, GunStoreItem::Pistol) == GunStoreRefusal::None);
    REQUIRE(gun_store_buy(e, use.reserve, GunStoreItem::PistolAmmo) == GunStoreRefusal::None);
    use.magazine = 7;

    // Through the real encoder and decoder.
    GameSave saved;
    saved.economy = e;
    saved.pistol_magazine = use.magazine;
    saved.pistol_reserve = use.reserve;
    std::string bytes, error;
    REQUIRE_MSG(encode_game_save(saved, bytes, error), error.c_str(), "encode");
    GameSave loaded;
    REQUIRE_MSG(decode_game_save(bytes, loaded, error), error.c_str(), "decode");
    REQUIRE(loaded.economy.cash == 1000 - kGunStorePistolPrice - kGunStoreAmmoPrice);
    REQUIRE(owns_weapon(loaded.economy, WeaponId::Pistol));
    REQUIRE(loaded.pistol_magazine == 7);
    REQUIRE(loaded.pistol_reserve == WeaponUseState::kInitialReserve + kGunStoreAmmoBoxRounds);

    // A load starts holstered; the wheel then equips the bought pistol.
    WeaponWheel after_load;
    REQUIRE(after_load.equipped == WeaponId::Unarmed);
    after_load.begin();
    after_load.hovered = WeaponId::Pistol;
    close_weapon_wheel(after_load, loaded.economy, true);
    REQUIRE(after_load.equipped == WeaponId::Pistol);
    REQUIRE(!enforce_weapon_ownership(after_load, loaded.economy));
    // Cancelling the wheel still equips nothing new.
    after_load.begin();
    after_load.hovered = WeaponId::Unarmed;
    close_weapon_wheel(after_load, loaded.economy, false);
    REQUIRE(after_load.equipped == WeaponId::Pistol);

    // A save without the pistol comes back without it.
    GameSave fresh;
    REQUIRE(encode_game_save(fresh, bytes, error));
    REQUIRE(decode_game_save(bytes, loaded, error));
    REQUIRE(!owns_weapon(loaded.economy, WeaponId::Pistol));
    WeaponWheel fresh_wheel;
    fresh_wheel.begin();
    fresh_wheel.hovered = WeaponId::Pistol;
    close_weapon_wheel(fresh_wheel, loaded.economy, true);
    REQUIRE(fresh_wheel.equipped == WeaponId::Unarmed);
    apricot_test::pass("bought pistol and rounds round-trip the real save and equip; unbought stays locked");
}

void death_keeps_the_rounds_you_bought() {
    WeaponUseState use;
    use.magazine = 3;
    use.reserve = 120;
    use.reloading = true;
    use.reload_elapsed = .5f;
    use.cooldown = .1f;
    const WeaponUseState after = respawn_weapon_use(use);
    REQUIRE(after.magazine == 3 && after.reserve == 120);
    REQUIRE(!after.reloading && after.reload_elapsed == 0.f && after.cooldown == 0.f);
    REQUIRE(after.equipped == WeaponId::Unarmed);
    apricot_test::pass("respawn keeps magazine and reserve, drops the reload and the draw");
}

}  // namespace

int main() {
    exact_funds_buy_the_pistol();
    one_dollar_short_moves_nothing();
    a_second_pistol_is_refused();
    cancel_charges_nothing();
    ammunition_needs_the_gun_and_stops_at_the_cap();
    only_the_customer_side_of_the_counter_shops();
    a_purchase_survives_the_save_and_equips();
    death_keeps_the_rounds_you_bought();
    return apricot_test::done("gun_store_shop_tests");
}
