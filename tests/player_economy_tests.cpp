#include "game/player_economy.h"
#include "test_assert.h"
using namespace apricot;
namespace {
void purchases_are_all_or_nothing() {
    PlayerEconomy e;
    REQUIRE(e.cash==kNewGameCash);
    REQUIRE(owns_weapon(e,WeaponId::Unarmed) && owns_weapon(e,WeaponId::Molotov));
    REQUIRE(!owns_weapon(e,WeaponId::Pistol));
    e.cash=250;
    REQUIRE(!buy_weapon(e,WeaponId::Pistol,251));           // One short: nothing moves.
    REQUIRE(e.cash==250 && !owns_weapon(e,WeaponId::Pistol));
    REQUIRE(buy_weapon(e,WeaponId::Pistol,250));            // Exact funds are enough.
    REQUIRE(e.cash==0 && owns_weapon(e,WeaponId::Pistol));
    e.cash=500;
    REQUIRE(!buy_weapon(e,WeaponId::Pistol,100));           // Owned: no second charge.
    REQUIRE(e.cash==500);
    REQUIRE(!spend_cash(e,-1) && e.cash==500);              // A negative price is not a refund.
    REQUIRE(spend_cash(e,0) && e.cash==500);
    apricot_test::pass("a purchase debits and grants together, or does neither");
}
void earnings_and_penalties_stay_in_range() {
    PlayerEconomy e;e.cash=kMaxCash-10;
    REQUIRE(earn_cash(e,25)==10 && e.cash==kMaxCash);        // Clamped, and says so.
    REQUIRE(earn_cash(e,-5)==0 && e.cash==kMaxCash);
    e.cash=40;
    REQUIRE(charge_cash(e,100)==40 && e.cash==0);           // A fine takes what is there...
    REQUIRE(charge_cash(e,100)==0 && e.cash==0);            // ...and never goes negative.
    REQUIRE(charge_cash(e,-3)==0 && e.cash==0);
    apricot_test::pass("payouts clamp at the cap; fines stop at zero and report what they took");
}
void validity() {
    PlayerEconomy e;REQUIRE(valid_player_economy(e));
    e.cash=-1;REQUIRE(!valid_player_economy(e));
    e={};e.cash=kMaxCash+1;REQUIRE(!valid_player_economy(e));
    e={};e.owned_weapons=weapon_bit(WeaponId::Pistol);REQUIRE(!valid_player_economy(e)); // Hands are always owned.
    e={};e.owned_weapons=static_cast<uint8_t>(kAllWeaponBits|0x80u);REQUIRE(!valid_player_economy(e));
    e={};e.owned_weapons=kLegacyOwnedWeapons;REQUIRE(valid_player_economy(e));
    apricot_test::pass("cash stays within 0..cap and only known weapons can be owned");
}
}
int main() {
    purchases_are_all_or_nothing();
    earnings_and_penalties_stay_in_range();
    validity();
    return apricot_test::done("player_economy_tests");
}
