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

    wanted.update(tuning.lose_track_window - 0.1f, false, tuning);
    REQUIRE(wanted.heat() == 3.0f);
    wanted.update(1.0f, false, tuning);
    REQUIRE(wanted.heat() < 3.0f);

    for (int i = 0; i < 30; ++i) wanted.update(1.0f, false, tuning);
    REQUIRE(wanted.heat() == 0.0f);
    REQUIRE(wanted.level() == 0);
    apricot_test::pass("police contact holds heat; losing contact starts escape cooling");
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

}  // namespace

int main() {
    heat_buckets_match_the_alpha();
    contact_holds_then_escape_cools();
    crime_report_is_one_shot_and_keeps_latest_kind();
    developer_level_setup_is_exact_and_does_not_report_a_crime();
    traffic_and_cruiser_reports_reach_the_dispatcher();
    return apricot_test::done("wanted_system_tests");
}
