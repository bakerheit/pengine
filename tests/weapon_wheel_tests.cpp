#include "game/weapon.h"
#include "test_assert.h"
using namespace apricot;
int main() {
    WeaponWheel wheel;
    wheel.begin();wheel.point(1,0);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Pistol && !wheel.open);
    wheel.begin();wheel.point(.01f,.01f);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Pistol);
    wheel.begin();wheel.point(-1,0);wheel.close(false);
    REQUIRE(wheel.equipped==WeaponId::Pistol);
    wheel.begin();wheel.point(-1,0);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Unarmed);
    wheel.point(1,0);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Unarmed);
    return apricot_test::done("weapon_wheel_tests");
}
