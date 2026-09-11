#include "game/player_vitals.h"
#include "game/police_combat.h"

#include <limits>

#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kDt = 1.0f / 120.0f;

void damage_accumulates_and_kills_once() {
    PlayerVitals v;
    REQUIRE(v.alive() && v.health == kBodyHealth);
    REQUIRE_NEAR(v.fraction(), 1.0f, 1e-6);

    // Thirteen police rounds at 8 points each. Twelve wound, the thirteenth
    // kills, and only the thirteenth says so.
    int kills = 0;
    for (int round = 0; round < 20; ++round)
        if (v.take_damage(kPoliceBulletDamage)) ++kills;
    REQUIRE_MSG(kills == 1, "a dead player was killed more than once",
                "one death");
    REQUIRE(!v.alive() && v.health == 0.0f);
    REQUIRE_NEAR(v.fraction(), 0.0f, 1e-6);
    apricot_test::pass("rounds accumulate, and exactly one of them kills");
}

void garbage_damage_cannot_kill() {
    PlayerVitals v;
    REQUIRE(!v.take_damage(std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(!v.take_damage(0.0f));
    REQUIRE(!v.take_damage(-40.0f));
    REQUIRE(v.alive() && v.health == kBodyHealth);
    apricot_test::pass("NaN, zero and negative damage leave the player standing");
}

// The whole point of the state existing: there is a span of time during which
// the player is dead. Before this, health hit zero and the respawn happened in
// the same statement, so "dead" was never a state anything could observe.
void death_lasts_and_fires_its_respawn_once() {
    PlayerVitals v;
    REQUIRE(v.kill());
    REQUIRE(!v.kill());  // already dead
    REQUIRE(!v.alive() && v.dead_seconds == 0.0f);

    int respawns = 0;
    float elapsed = 0.0f;
    for (int step = 0; step < 1200; ++step) {  // 10 s at 120 Hz
        if (v.step(kDt)) {
            ++respawns;
            elapsed = v.dead_seconds;
        }
    }
    REQUIRE_MSG(respawns == 1, "the respawn edge fired more than once",
                "one respawn");
    REQUIRE_MSG(elapsed >= kPlayerDeathSeconds, "the respawn came early",
                "timing");
    REQUIRE(elapsed < kPlayerDeathSeconds + kDt * 2.0f);
    apricot_test::pass("being dead takes time, and the respawn edge fires once");
}

void a_living_player_is_not_stepped() {
    PlayerVitals v;
    for (int step = 0; step < 1200; ++step) REQUIRE(!v.step(kDt));
    REQUIRE(v.alive() && v.dead_seconds == 0.0f);
    // A paused frame owes no time, and must not advance a death either.
    REQUIRE(v.kill());
    REQUIRE(!v.step(0.0f));
    REQUIRE(!v.step(-1.0f));
    REQUIRE(!v.step(std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(v.dead_seconds == 0.0f);
    apricot_test::pass("a living player and a paused frame both advance nothing");
}

void reviving_restores_everything() {
    PlayerVitals v;
    v.take_damage(60.0f);
    REQUIRE(v.kill());
    while (!v.step(kDt)) {}
    v.revive();
    REQUIRE(v.alive() && v.health == kBodyHealth && v.dead_seconds == 0.0f);
    // And the revived player can die again, cleanly.
    REQUIRE(v.take_damage(kBodyHealth));
    REQUIRE(!v.alive());
    apricot_test::pass("a revived player is whole, and can die again");
}

}  // namespace

int main() {
    damage_accumulates_and_kills_once();
    garbage_damage_cannot_kill();
    death_lasts_and_fires_its_respawn_once();
    a_living_player_is_not_stepped();
    reviving_restores_everything();
    return apricot_test::done("player_vitals_tests");
}
