#include <algorithm>
#include <limits>

#include "game/police_arrest.h"
#include "test_assert.h"

using namespace apricot;
namespace {

VehicleAgent officer(uint64_t key = 10, uint32_t slot = 2) {
    VehicleAgent car;
    car.lane_key = key;
    car.slot = slot;
    car.police_unit = car.police_pursuit = true;
    car.officer.phase = PoliceOfficerPhase::Pursuing;
    car.officer.pos = {0, 0, 0};
    return car;
}

struct Fixture {
    PoliceArrestTracker tracker;
    uint64_t step = 0;
    bool on_foot = true;
    int wanted = 1;
    glm::vec3 player{2, 0, 0};
    std::vector<VehicleAgent> cars{officer()};
    std::vector<VisiblePoliceIdentity> visible{{10, 2}};

    std::optional<PoliceArrestEvent> tick() {
        return tracker.observe(step++, on_foot, wanted, player, cars, visible);
    }
    void no_arrest_for(unsigned ticks) {
        for (unsigned i = 0; i < ticks; ++i) REQUIRE(!tick());
    }
};

void exact_range_and_time_fire_once_per_pursuit() {
    Fixture f;
    // 359 / 120 seconds is still too early. Exactly two metres qualifies.
    f.no_arrest_for(359);
    const auto event = f.tick();
    REQUIRE(event.has_value());
    REQUIRE((event->officer == VisiblePoliceIdentity{10, 2}));
    REQUIRE(event->step == 359u);
    f.no_arrest_for(720);
    // A later pursuit starts with an entirely fresh three-second hold.
    f.wanted = 0;
    REQUIRE(!f.tick());
    f.wanted = 2;
    f.no_arrest_for(359);
    REQUIRE(f.tick().has_value());
    apricot_test::pass("arrest fires once at two metres after exactly three seconds");
}

void every_loss_of_eligibility_restarts_the_timer() {
    for (int scenario = 0; scenario < 9; ++scenario) {
        Fixture f;
        f.no_arrest_for(240);
        switch (scenario) {
        case 0: f.player.x = 2.001f; break;
        case 1: f.visible.clear(); break;
        case 2: f.on_foot = false; break;
        case 3: f.wanted = 0; break;
        case 4: f.cars[0].police_pursuit = false; break;
        case 5: f.cars[0].police_unit = false; break;
        case 6: f.cars[0].officer.phase = PoliceOfficerPhase::Seated; break;
        case 7: f.cars[0].officer.phase = PoliceOfficerPhase::Returning; break;
        case 8: f.cars.clear(); break;
        }
        REQUIRE(!f.tick());
        f.player = {2, 0, 0};
        f.visible = {{10, 2}};
        f.on_foot = true;
        f.wanted = 1;
        f.cars = {officer()};
        f.no_arrest_for(359);
        REQUIRE(f.tick().has_value());
    }
    apricot_test::pass("range, sight, vehicle entry, standdown and missing officers reset arrest progress");
}

void vertical_distance_and_door_transitions_do_not_arrest() {
    Fixture f;
    f.player = {0, 2.001f, 0};
    f.no_arrest_for(600);
    f.player = {2, 0, 0};
    for (const auto phase : {PoliceOfficerPhase::Braking,
                            PoliceOfficerPhase::Exiting,
                            PoliceOfficerPhase::Entering}) {
        f.cars[0].officer.phase = phase;
        f.no_arrest_for(600);
    }
    f.cars[0].officer.phase = PoliceOfficerPhase::Pursuing;
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity()}) {
        f.player = {invalid, 0, 0};
        f.no_arrest_for(600);
    }
    apricot_test::pass("arrest uses full spatial distance and excludes door transitions and invalid positions");
}

void officers_cannot_relay_partial_holds() {
    Fixture f;
    f.cars.push_back(officer(11, 3));
    f.visible.push_back({11, 3});
    f.cars[1].officer.pos.x = 10;
    f.no_arrest_for(180);
    f.cars[0].officer.pos.x = 10;
    f.cars[1].officer.pos.x = 0;
    f.no_arrest_for(359);
    const auto event = f.tick();
    REQUIRE(event.has_value());
    REQUIRE((event->officer == VisiblePoliceIdentity{11, 3}));
    apricot_test::pass("a replacement officer must establish a complete hold of their own");
}

void ordering_and_duplicate_snapshots_do_not_change_the_hold() {
    Fixture f;
    f.cars.push_back(officer(11, 3));
    f.cars.push_back(officer());
    f.visible.push_back({11, 3});
    for (unsigned i = 0; i < 359; ++i) {
        std::reverse(f.cars.begin(), f.cars.end());
        REQUIRE(!f.tick());
        // A render reusing this simulation snapshot cannot advance the timer.
        REQUIRE(!f.tracker.observe(f.step-1u, true, 1, f.player, f.cars, f.visible));
    }
    const auto event = f.tick();
    REQUIRE(event.has_value());
    REQUIRE((event->officer == VisiblePoliceIdentity{10, 2}));
    apricot_test::pass("officer ordering, duplicate identities and repeated ticks are deterministic");
}

void simulation_seeks_and_explicit_resets_start_fresh() {
    for (int scenario = 0; scenario < 3; ++scenario) {
        Fixture f;
        f.no_arrest_for(240);
        if (scenario == 0) f.step += 5000;
        else if (scenario == 1) f.step = 12;
        else f.tracker.reset();
        f.no_arrest_for(359);
        REQUIRE(f.tick().has_value());
    }
    apricot_test::pass("time skips, rewinds and load or teleport resets cannot inherit arrest time");
}

}  // namespace

int main() {
    exact_range_and_time_fire_once_per_pursuit();
    every_loss_of_eligibility_restarts_the_timer();
    vertical_distance_and_door_transitions_do_not_arrest();
    officers_cannot_relay_partial_holds();
    ordering_and_duplicate_snapshots_do_not_change_the_hold();
    simulation_seeks_and_explicit_resets_start_fresh();
    return 0;
}
