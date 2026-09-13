// The weapon wheel's SELECTION rules and its SECTOR LAYOUT.
//
// This suite owns the layout: which direction picks which item. app/weapon_
// wheel.h paints the same sectors from the same kWeaponWheelOrder, and
// molotov_tests.cpp deliberately does not restate any of it — two suites
// asserting one layout is two places for it to be wrong in different ways.
//
// Directions are screen space with y running DOWN, which is what both the
// mouse and the UI stick hand in. Unarmed is straight up; pistol is down and
// to the right; molotov is down and to the left.

#include <cmath>
#include <cstdio>

#include "game/weapon.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    // --- the three sectors --------------------------------------------------
    WeaponWheel wheel;
    const struct { float x, y; WeaponId want; const char* name; } kPicks[] = {
        {0.f, -1.f, WeaponId::Unarmed, "up"},
        {.87f, .5f, WeaponId::Pistol, "down-right"},
        {-.87f, .5f, WeaponId::Molotov, "down-left"},
        // Just inside each boundary, so a sign slip in the angle maths shows
        // up here rather than as a wheel that highlights the wrong wedge for
        // the last few degrees before the player lets go.
        {std::sin(.98f), -std::cos(.98f), WeaponId::Unarmed, "just before the first seam"},
        {std::sin(1.10f), -std::cos(1.10f), WeaponId::Pistol, "just after the first seam"},
        {std::sin(3.04f), -std::cos(3.04f), WeaponId::Pistol, "just before the second seam"},
        {std::sin(3.24f), -std::cos(3.24f), WeaponId::Molotov, "just after the second seam"},
    };
    for (const auto& pick : kPicks) {
        wheel.begin();
        wheel.point(pick.x, pick.y);
        wheel.close(true);
        REQUIRE_MSG(wheel.equipped == pick.want, "wrong wheel sector", pick.name);
    }

    // Every direction lands in a real slot. An off-by-one in the sector index
    // is an out-of-bounds read of kWeaponWheelOrder, not a cosmetic bug.
    for (int i = 0; i < 1440; ++i) {
        const float a = static_cast<float>(i) * 0.25f * 3.14159265f / 180.f;
        wheel.begin();
        wheel.point(std::cos(a), std::sin(a));
        REQUIRE(wheel.hovered == WeaponId::Unarmed ||
                wheel.hovered == WeaponId::Pistol ||
                wheel.hovered == WeaponId::Molotov);
        wheel.close(false);
    }

    // --- selection rules ----------------------------------------------------
    wheel.begin();wheel.point(.87f,.5f);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Pistol && !wheel.open);
    // The dead zone: a nudge inside it leaves the hovered item alone, so a
    // quick tap of TAB keeps what you already had.
    wheel.begin();wheel.point(.01f,.01f);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Pistol);
    // Rubbish from a disconnected stick is ignored rather than propagated.
    wheel.begin();wheel.point(std::nanf(""),.5f);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Pistol);
    // Cancelling keeps the previous selection.
    wheel.begin();wheel.point(0,-1);wheel.close(false);
    REQUIRE(wheel.equipped==WeaponId::Pistol);
    wheel.begin();wheel.point(0,-1);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Unarmed);
    // A closed wheel ignores pointing entirely, so mouse motion after the
    // release cannot swap the weapon out from under the player.
    wheel.point(.87f,.5f);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Unarmed);

    // Opening starts on what is equipped, not on whatever was hovered last.
    wheel.begin();wheel.point(-.87f,.5f);wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Molotov);
    wheel.begin();
    REQUIRE(wheel.hovered==WeaponId::Molotov);
    wheel.close(true);
    REQUIRE(wheel.equipped==WeaponId::Molotov);

    return apricot_test::done("weapon_wheel_tests");
}
