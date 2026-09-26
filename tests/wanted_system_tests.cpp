#include <cmath>

#include "game/wanted_system.h"
#include "test_assert.h"

using namespace apricot;

namespace {

void heat_buckets_match_the_alpha() {
    WantedSystem wanted;
    REQUIRE(wanted.level() == 0);
    wanted.add_heat(0.1f);
    REQUIRE(wanted.level() == 1);
    wanted.add_heat(2.9f);
    REQUIRE(wanted.level() == 2);
    wanted.add_heat(3.0f);
    REQUIRE(wanted.level() == 3);
    wanted.add_heat(3.0f);
    REQUIRE(wanted.level() == 4);
    wanted.add_heat(3.0f);
    REQUIRE(wanted.level() == 5);
    wanted.add_heat(100.0f);
    REQUIRE(wanted.heat() == 15.0f);
    apricot_test::pass("wanted heat uses the alpha's five level buckets and cap");
}

void contact_holds_then_escape_cools() {
    WantedSystem wanted;
    PoliceTuning tuning;
    wanted.add_heat(3.0f);

    wanted.update(30.0f, true, tuning);
    REQUIRE(wanted.heat() == 3.0f);
    REQUIRE(wanted.level() == 2);

    wanted.update(police_level_profile(2).escape_s - 0.1f, false, tuning);
    REQUIRE(wanted.heat() == 3.0f);
    wanted.update(1.0f, false, tuning);
    REQUIRE(wanted.heat() < 3.0f);

    for (int i = 0; i < 30; ++i) wanted.update(1.0f, false, tuning);
    REQUIRE(wanted.heat() == 0.0f);
    REQUIRE(wanted.level() == 0);
    apricot_test::pass("police contact holds heat; losing contact starts escape cooling");
}

// More stars take longer to shake: the grace window is the level's, and it is
// the level you held when you broke line of sight.
void escape_window_is_per_level() {
    PoliceTuning tuning;
    WantedSystem five;
    five.add_heat(12.0f);
    REQUIRE(five.level() == 5);
    five.update(60.0f, false, tuning);            // well past one star's window
    REQUIRE(five.heat() == 12.0f);                // ... but inside five stars'
    five.update(police_level_profile(5).escape_s - 60.0f + 1.0f, false, tuning);
    REQUIRE(five.heat() < 12.0f);

    WantedSystem one;
    one.add_heat(1.0f);
    REQUIRE(one.level() == 1);
    one.update(police_level_profile(1).escape_s + 1.0f, false, tuning);
    REQUIRE(one.heat() < 1.0f);
    apricot_test::pass("the escape window scales with the wanted level");
}

void crime_report_is_one_shot_and_keeps_latest_kind() {
    WantedSystem wanted;
    wanted.add_heat(1.0f, WantedSystem::Crime::VehicleTheft);
    wanted.add_heat(1.0f, WantedSystem::Crime::OfficerAssault);
    WantedSystem::Crime crime = WantedSystem::Crime::Other;
    REQUIRE(wanted.take_crime_report(crime));
    REQUIRE(crime == WantedSystem::Crime::OfficerAssault);
    REQUIRE(!wanted.take_crime_report(crime));

    wanted.set_enabled(false);
    wanted.add_heat(5.0f, WantedSystem::Crime::Violent);
    REQUIRE(wanted.level() == 0);
    REQUIRE(!wanted.take_crime_report(crime));
    apricot_test::pass("crime reports drain once and disabled wanted ignores crimes");
}

void developer_level_setup_is_exact_and_does_not_report_a_crime() {
    static constexpr float kExpectedHeat[] = {
        0.0f, 1.0f, 3.0f, 6.0f, 9.0f, 12.0f,
    };
    WantedSystem wanted;
    for (int level = 0; level <= 5; ++level) {
        wanted.add_heat(0.5f, WantedSystem::Crime::OfficerAssault);
        wanted.set_level(level);
        REQUIRE(wanted.level() == level);
        REQUIRE(wanted.heat() == kExpectedHeat[level]);
        WantedSystem::Crime crime = WantedSystem::Crime::Other;
        REQUIRE(!wanted.take_crime_report(crime));
    }
    wanted.set_level(-20);
    REQUIRE(wanted.level() == 0);
    wanted.set_level(20);
    REQUIRE(wanted.level() == 5);
    apricot_test::pass(
        "developer wanted setup selects exact levels without inventing crimes");
}

void traffic_and_cruiser_reports_reach_the_dispatcher() {
    for (const auto crime : {WantedSystem::Crime::TrafficViolation,
                             WantedSystem::Crime::PoliceVehicleCollision}) {
        WantedSystem wanted;
        wanted.add_heat(1.0f, crime);
        REQUIRE(wanted.level() == 1);
        WantedSystem::Crime reported = WantedSystem::Crime::Other;
        REQUIRE(wanted.take_crime_report(reported));
        REQUIRE(reported == crime);
        REQUIRE(!wanted.take_crime_report(reported));
    }
    apricot_test::pass("traffic violations and cruiser impacts keep their crime kind through dispatch");
}

// The HUD meter's countdown is a promise: stay unseen and the stars are gone
// in exactly this long. Step the real system at the sim rate and hold it to it,
// from every level, through every star it drops on the way down.
void cooldown_countdown_matches_the_real_escape() {
    PoliceTuning tuning;
    constexpr float kDt = 1.0f / 120.0f;
    for (int level = 1; level <= 5; ++level) {
        WantedSystem wanted;
        wanted.set_level(level);
        wanted.update(kDt, true, tuning);
        REQUIRE(wanted.cooldown(tuning).phase == WantedCooldown::Phase::Seen);
        REQUIRE(wanted.cooldown(tuning).remaining_fraction == 1.0f);

        wanted.update(kDt, false, tuning);
        const WantedCooldown start = wanted.cooldown(tuning);
        REQUIRE(start.phase == WantedCooldown::Phase::LosingThem);
        const float promised = start.seconds_to_clear;
        REQUIRE_NEAR(promised, police_level_profile(level).escape_s - kDt +
                                   wanted.heat() / tuning.heat_decay_rate, 1e-3f);

        float elapsed = 0.0f;
        float last_fraction = start.remaining_fraction;
        bool saw_cooling = false;
        while (wanted.level() > 0) {
            wanted.update(kDt, false, tuning);
            elapsed += kDt;
            const WantedCooldown now = wanted.cooldown(tuning);
            saw_cooling |= now.phase == WantedCooldown::Phase::Cooling;
            REQUIRE(now.remaining_fraction <= last_fraction + 1e-6f);
            last_fraction = now.remaining_fraction;
            REQUIRE(elapsed < 200.0f);
        }
        REQUIRE(saw_cooling);
        REQUIRE_NEAR(elapsed, promised, 0.05f);
        REQUIRE(wanted.cooldown(tuning).phase == WantedCooldown::Phase::Clear);
    }
    apricot_test::pass("the cooldown countdown is exactly how long an unseen escape takes");
}

// A sighting mid-drain freezes the heat where it is; breaking contact again
// restarts the meter full, measured from the heat that is left.
void a_sighting_restarts_the_cooldown_meter() {
    PoliceTuning tuning;
    WantedSystem wanted;
    wanted.set_level(3);
    wanted.update(police_level_profile(3).escape_s - 0.01f, false, tuning);
    wanted.update(0.5f, false, tuning);
    REQUIRE(wanted.cooldown(tuning).phase == WantedCooldown::Phase::Cooling);
    REQUIRE(wanted.cooldown(tuning).remaining_fraction < 1.0f);

    wanted.update(0.1f, true, tuning);
    REQUIRE(wanted.cooldown(tuning).phase == WantedCooldown::Phase::Seen);

    wanted.update(0.1f, false, tuning);
    const WantedCooldown again = wanted.cooldown(tuning);
    REQUIRE(again.phase == WantedCooldown::Phase::LosingThem);
    REQUIRE(again.remaining_fraction > 0.99f);

    wanted.reset();
    REQUIRE(wanted.cooldown(tuning).phase == WantedCooldown::Phase::Clear);
    apricot_test::pass("a sighting holds the heat and restarts the cooldown meter");
}

}  // namespace

int main() {
    heat_buckets_match_the_alpha();
    contact_holds_then_escape_cools();
    escape_window_is_per_level();
    crime_report_is_one_shot_and_keeps_latest_kind();
    developer_level_setup_is_exact_and_does_not_report_a_crime();
    traffic_and_cruiser_reports_reach_the_dispatcher();
    cooldown_countdown_matches_the_real_escape();
    a_sighting_restarts_the_cooldown_meter();
    return apricot_test::done("wanted_system_tests");
}
