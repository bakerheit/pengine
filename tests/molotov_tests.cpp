// game/molotov.h — the throw, the arc, and the three-slot weapon wheel.
//
// The state machine is checked against the ways a weapon leaks input, because
// those are the bugs that reach a player: a press held through a pause that
// throws on the way out, a bottle that leaves on the same frame it is drawn,
// and a stock that can be spent twice.
//
// THE WHEEL'S LAYOUT IS NOT HERE. tests/weapon_wheel_tests.cpp owns which
// direction picks which item, and app/weapon_wheel.h paints those same
// sectors; a second copy of the mapping in this file would be a second place
// for it to be wrong.

#include <cmath>
#include <cstdio>
#include <string>

#include "game/molotov.h"
#include "game/weapon.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kStep = 1.0f / 120.0f;

MolotovUseInput held(bool throwing = false, bool aim = false) {
    MolotovUseInput in;
    in.available = true;
    in.aim = aim;
    in.throw_pressed = throwing;
    return in;
}

// Run the draw out so the bottle is up and the state is ready to throw.
void draw(MolotovUseState& use) {
    for (int i = 0; i < 120; ++i) use.step(WeaponId::Molotov, held(), kStep);
    REQUIRE(use.equip_blend >= 1.0f);
    REQUIRE(use.armed());
}

void weapon_names_cover_every_slot() {
    REQUIRE(std::string("UNARMED") == weapon_name(WeaponId::Unarmed));
    REQUIRE(std::string("PISTOL") == weapon_name(WeaponId::Pistol));
    REQUIRE(std::string("MOLOTOV") == weapon_name(WeaponId::Molotov));
    apricot_test::pass("weapon names cover every slot");
}

void a_bottle_cannot_leave_on_the_frame_it_is_drawn() {
    MolotovUseState use;
    // Button already down as the molotov comes out. Nothing may be thrown
    // until the draw has finished AND a fresh press arrives.
    for (int i = 0; i < 400; ++i) {
        REQUIRE_MSG(!use.step(WeaponId::Molotov, held(true), kStep),
                    "a held button threw a bottle", "held-through-draw");
    }
    REQUIRE(use.stock == MolotovUseState::kInitialStock);
    apricot_test::pass("a bottle cannot leave on the frame it is drawn");
}

void one_press_throws_exactly_one_bottle() {
    MolotovUseState use;
    draw(use);
    int thrown = 0;
    // Press, and keep holding for two whole throw intervals.
    for (int i = 0; i < 300; ++i)
        if (use.step(WeaponId::Molotov, held(true), kStep)) ++thrown;
    REQUIRE_MSG(thrown == 1, "a held button threw more than one bottle", "auto");
    REQUIRE(use.stock == MolotovUseState::kInitialStock - 1);
    apricot_test::pass("one press throws exactly one bottle");
}

void the_hand_is_empty_between_throws() {
    MolotovUseState use;
    draw(use);
    REQUIRE(use.step(WeaponId::Molotov, held(true), kStep));
    REQUIRE_MSG(!use.armed(), "the next bottle appeared on the throw frame",
                "rearm");
    REQUIRE(use.rearm_progress() == 0.0f);
    for (int i = 0; i < 40; ++i) use.step(WeaponId::Molotov, held(), kStep);
    REQUIRE(use.rearm_progress() > 0.0f && use.rearm_progress() < 1.0f);
    for (int i = 0; i < 200; ++i) use.step(WeaponId::Molotov, held(), kStep);
    REQUIRE(use.armed());
    REQUIRE(use.rearm_progress() == 1.0f);
    apricot_test::pass("the hand is empty between throws");
}

void the_stock_runs_out_and_stays_out() {
    MolotovUseState use;
    draw(use);
    int thrown = 0;
    for (int i = 0; i < 4000; ++i) {
        // A press every 200 steps, released in between.
        const bool press = (i % 200) < 3;
        if (use.step(WeaponId::Molotov, held(press), kStep)) ++thrown;
    }
    REQUIRE_MSG(thrown == MolotovUseState::kInitialStock,
                "the stock did not spend exactly once per bottle", "spend");
    REQUIRE(use.stock == 0);
    REQUIRE(!use.armed());
    // And nothing comes out of an empty pocket, however long you hold it.
    for (int i = 0; i < 600; ++i)
        REQUIRE(!use.step(WeaponId::Molotov, held((i % 100) < 3), kStep));
    apricot_test::pass("the stock runs out and stays out");
}

// A pause owes no simulation time but still revokes the weapon. The press made
// while the menu was open must be spent there, not stored up.
void a_press_through_a_pause_does_not_throw_on_the_way_out() {
    MolotovUseState use;
    draw(use);
    MolotovUseInput blocked;
    blocked.available = false;
    blocked.throw_pressed = true;
    for (int i = 0; i < 60; ++i)
        REQUIRE(!use.step(WeaponId::Molotov, blocked, 0.0f));
    // Back to the game, button still down.
    for (int i = 0; i < 400; ++i)
        REQUIRE_MSG(!use.step(WeaponId::Molotov, held(true), kStep),
                    "a paused press threw a bottle on resume", "pause");
    REQUIRE(use.stock == MolotovUseState::kInitialStock);
    apricot_test::pass("a press through a pause does not throw on the way out");
}

void switching_weapons_cancels_the_draw() {
    MolotovUseState use;
    draw(use);
    // Equip the pistol: the molotov state deactivates without spending stock.
    for (int i = 0; i < 10; ++i) use.step(WeaponId::Pistol, held(true), kStep);
    REQUIRE(use.equip_blend == 0.0f);
    REQUIRE(use.aim_blend == 0.0f);
    REQUIRE(use.stock == MolotovUseState::kInitialStock);
    // Coming back needs the whole draw again before anything can be thrown.
    int thrown = 0;
    for (int i = 0; i < 20; ++i)
        if (use.step(WeaponId::Molotov, held(true), kStep)) ++thrown;
    REQUIRE(thrown == 0);
    apricot_test::pass("switching weapons cancels the draw");
}

void a_bad_dt_changes_nothing() {
    MolotovUseState use;
    draw(use);
    const float blend = use.equip_blend;
    REQUIRE(!use.step(WeaponId::Molotov, held(true), std::nanf("")));
    REQUIRE(!use.step(WeaponId::Molotov, held(true), -1.0f));
    REQUIRE(use.equip_blend == blend);
    REQUIRE(use.stock == MolotovUseState::kInitialStock);
    apricot_test::pass("a bad dt changes nothing");
}

// THE LOFT IS THE POINT. Thrown down the exact camera ray, a level aim lands
// at the player's feet and there is no way to say "over there".
void the_throw_is_lofted_and_aiming_extends_it() {
    const glm::vec3 hip = molotov_throw_velocity(0.0f, 0.0f, 0.0f);
    REQUIRE_MSG(hip.y > 2.0f, "a level hip throw went flat", "loft");

    // Flight time to return to launch height, and the ground covered in it.
    const auto reach = [](glm::vec3 v) {
        const float flight = 2.0f * v.y / kMolotovGravityMps2;
        return std::sqrt(v.x * v.x + v.z * v.z) * flight;
    };
    const glm::vec3 aimed = molotov_throw_velocity(0.0f, 0.0f, 1.0f);
    REQUIRE_MSG(reach(aimed) > reach(hip),
                "taking aim did not buy any reach", "aim");
    // Aiming trades loft for speed, so the aimed throw is the flatter one.
    REQUIRE(aimed.y < hip.y);

    // Yaw points it. -Z is forward at yaw 0, matching the rest of the engine.
    REQUIRE(hip.z < 0.0f);
    REQUIRE_NEAR(static_cast<double>(hip.x), 0.0, 1e-5);
    const glm::vec3 right = molotov_throw_velocity(-1.5707963f, 0.0f, 0.0f);
    REQUIRE_MSG(right.x > 0.0f, "yaw did not steer the throw", "yaw");

    // Straight down is clamped rather than launched into the floor, and
    // straight up is clamped rather than dropped on the thrower's head.
    for (float pitch : {-1.55f, 1.55f}) {
        const glm::vec3 v = molotov_throw_velocity(0.0f, pitch, 1.0f);
        REQUIRE(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z));
        REQUIRE_MSG(std::sqrt(v.x * v.x + v.z * v.z) > 1.0f,
                    "an extreme pitch dropped the bottle straight down",
                    "clamp");
    }
    // Rubbish in, nothing out.
    REQUIRE(molotov_throw_velocity(std::nanf(""), 0.0f, 0.0f) == glm::vec3{0.0f});
    apricot_test::pass("the throw is lofted and aiming extends it");
}

void projectiles_fly_tumble_and_expire() {
    MolotovProjectiles shots;
    REQUIRE(shots.live_count() == 0);
    REQUIRE(shots.launch({0.0f, 2.0f, 0.0f}, {0.0f, 6.0f, -12.0f}, 1));
    REQUIRE(shots.live_count() == 1);

    const auto before = shots.shots()[0];
    for (int i = 0; i < 12; ++i) shots.step(kStep);
    const auto after = shots.shots()[0];
    REQUIRE_MSG(after.position.z < before.position.z, "the bottle did not fly",
                "travel");
    REQUIRE_MSG(after.velocity.y < before.velocity.y, "no gravity on the arc",
                "gravity");
    REQUIRE_MSG(after.spin_turns > 0.0f, "the bottle did not tumble", "spin");
    REQUIRE_NEAR(static_cast<double>(glm::length(after.spin_axis)), 1.0, 1e-4);

    // Nothing flies forever: a bottle thrown off a roof that never finds
    // ground is a fire waiting to start under a player half a mile away.
    for (int i = 0; i < 1200; ++i) shots.step(kStep);
    REQUIRE_MSG(shots.live_count() == 0, "a projectile outlived its flight cap",
                "expiry");

    // The pool is bounded and reports a full house rather than losing a bottle
    // quietly.
    for (std::size_t i = 0; i < MolotovProjectiles::kCapacity; ++i)
        REQUIRE(shots.launch({0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, -1.0f}, i));
    REQUIRE(!shots.launch({0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, -1.0f}, 99));
    REQUIRE(shots.live_count() == MolotovProjectiles::kCapacity);

    // Rubbish in changes nothing.
    shots.clear();
    REQUIRE(!shots.launch({std::nanf(""), 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1));
    REQUIRE(!shots.launch({0.0f, 0.0f, 0.0f}, {std::nanf(""), 1.0f, 0.0f}, 1));
    REQUIRE(shots.live_count() == 0);
    // A bad dt does not advance the arc.
    REQUIRE(shots.launch({0.0f, 9.0f, 0.0f}, {0.0f, 0.0f, -9.0f}, 5));
    const auto still = shots.shots()[0].position;
    shots.step(0.0f);
    shots.step(std::nanf(""));
    REQUIRE(shots.shots()[0].position == still);
    // Putting one out is idempotent and bounds-checked.
    shots.extinguish(0);
    shots.extinguish(0);
    shots.extinguish(9999);
    REQUIRE(shots.live_count() == 0);
    apricot_test::pass("projectiles fly, tumble and expire");
}

void the_same_throw_tumbles_the_same_way() {
    const auto run = [](uint64_t id) {
        MolotovProjectiles shots;
        shots.launch({1.0f, 2.0f, 3.0f}, {2.0f, 7.0f, -11.0f}, id);
        for (int i = 0; i < 60; ++i) shots.step(kStep);
        return shots.shots()[0];
    };
    const auto a = run(4242);
    const auto b = run(4242);
    REQUIRE(a.position == b.position);
    REQUIRE(a.spin_axis == b.spin_axis);
    REQUIRE(a.spin_turns == b.spin_turns);
    REQUIRE(run(4242).spin_axis != run(4243).spin_axis);
    apricot_test::pass("the same throw tumbles the same way");
}

}  // namespace

int main() {
    weapon_names_cover_every_slot();
    a_bottle_cannot_leave_on_the_frame_it_is_drawn();
    one_press_throws_exactly_one_bottle();
    the_hand_is_empty_between_throws();
    the_stock_runs_out_and_stays_out();
    a_press_through_a_pause_does_not_throw_on_the_way_out();
    switching_weapons_cancels_the_draw();
    a_bad_dt_changes_nothing();
    the_throw_is_lofted_and_aiming_extends_it();
    projectiles_fly_tumble_and_expire();
    the_same_throw_tumbles_the_same_way();
    return apricot_test::done("molotov_tests");
}
