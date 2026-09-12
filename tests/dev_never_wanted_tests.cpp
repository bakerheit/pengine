// The NEVER WANTED toggle on the developer menu's wanted page, and the
// WantedSystem contract it drives.
//
// Two things are worth pinning here, and neither is the toggle flipping.
//
//   1. The MENU MUST NOT LIE. A wanted page reading "3 STARS  ACTIVE" directly
//      above "NEVER WANTED  ON" describes a state the game cannot be in. The
//      two rows have to agree, in both directions.
//
//   2. Switching it on ENDS a pursuit rather than freezing one. set_enabled
//      (false) resets banked heat as well as refusing new heat; if that ever
//      became a refuse-only gate, turning the toggle on mid-chase would leave
//      the player at four stars forever with no way to cool down, which is the
//      opposite of what the control is for.

#include <cstring>

#include "app/dev_menu.h"
#include "city/police_ai.h"
#include "game/wanted_system.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// Walk the real menu to the wanted page rather than setting state directly:
// the route a player takes is part of what is under test.
DevMenu open_wanted_page() {
    DevMenu menu;
    menu.toggle();
    menu.set_selection(2);  // PLAYER & VEHICLE
    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::None);
    REQUIRE(menu.page() == DevMenuPage::Vehicle);
    menu.set_selection(3);  // WANTED LEVEL
    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::None);
    REQUIRE(menu.page() == DevMenuPage::Wanted);
    return menu;
}

int never_wanted_row() {
    return static_cast<int>(kDevWantedLevelLabels.size());
}

void test_the_toggle_sits_below_the_five_levels() {
    DevMenu menu = open_wanted_page();
    REQUIRE(menu.item_count() ==
            static_cast<int>(kDevWantedLevelLabels.size()) + 1);
    REQUIRE(std::strcmp(menu.item_label(never_wanted_row()), "NEVER WANTED") == 0);
    REQUIRE(std::strcmp(menu.item_value(never_wanted_row()), "OFF") == 0);
    apricot_test::pass("NEVER WANTED is the row under the five stars, off by default");
}

void test_activating_it_raises_the_action_and_flips_the_value() {
    DevMenu menu = open_wanted_page();
    menu.set_selection(never_wanted_row());

    const DevMenuAction on = menu.update(kBtnAccept);
    REQUIRE(on.kind == DevMenuActionKind::SetNeverWanted);
    REQUIRE(on.never_wanted);
    REQUIRE(menu.never_wanted());
    REQUIRE(std::strcmp(menu.item_value(never_wanted_row()), "ON") == 0);

    const DevMenuAction off = menu.update(kBtnAccept);
    REQUIRE(off.kind == DevMenuActionKind::SetNeverWanted);
    REQUIRE(!off.never_wanted);
    REQUIRE(!menu.never_wanted());
    REQUIRE(std::strcmp(menu.item_value(never_wanted_row()), "OFF") == 0);
    apricot_test::pass("the toggle reports both directions, not just switching on");
}

// The page must not stay open on a state that cannot exist.
void test_the_page_never_shows_a_level_active_while_suppressed() {
    DevMenu menu = open_wanted_page();
    menu.set_selection(3);  // 3 STARS
    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::SetWantedLevel);
    REQUIRE(std::strcmp(menu.item_value(3), "ACTIVE") == 0);

    menu.set_selection(never_wanted_row());
    menu.update(kBtnAccept);
    for (int i = 0; i < static_cast<int>(kDevWantedLevelLabels.size()); ++i) {
        REQUIRE_MSG(std::strcmp(menu.item_value(i), "ACTIVE") != 0,
                    "no level may read ACTIVE while heat is suppressed",
                    kDevWantedLevelLabels[static_cast<std::size_t>(i)]);
    }
    apricot_test::pass("no star row claims ACTIVE while NEVER WANTED is on");
}

// ...and the other direction: asking for stars must not be a dead press.
void test_choosing_a_level_clears_the_suppression() {
    DevMenu menu = open_wanted_page();
    menu.set_selection(never_wanted_row());
    menu.update(kBtnAccept);
    REQUIRE(menu.never_wanted());

    menu.set_selection(4);  // 4 STARS
    const DevMenuAction stars = menu.update(kBtnAccept);
    REQUIRE(stars.kind == DevMenuActionKind::SetWantedLevel);
    REQUIRE(stars.wanted_level == 4);
    REQUIRE_MSG(!menu.never_wanted(),
                "asking for stars re-arms the system", "clears");
    REQUIRE(std::strcmp(menu.item_value(never_wanted_row()), "OFF") == 0);
    apricot_test::pass("picking a level turns the suppression off instead of doing nothing");
}

// The behaviour the toggle actually buys, at the system it drives.
void test_suppression_refuses_new_heat() {
    WantedSystem wanted;
    wanted.set_enabled(false);
    wanted.add_heat(9.0f, WantedSystem::Crime::Violent);
    REQUIRE(wanted.level() == 0);
    WantedSystem::Crime reported;
    REQUIRE_MSG(!wanted.take_crime_report(reported),
                "a suppressed crime is not dispatched either", "report");
    apricot_test::pass("heat gained while suppressed never reaches a level");
}

// The one that matters mid-chase.
void test_switching_it_on_ends_a_pursuit_rather_than_freezing_it() {
    WantedSystem wanted;
    wanted.add_heat(9.0f, WantedSystem::Crime::Violent);
    REQUIRE_MSG(wanted.level() > 0, "the pursuit is real first", "setup");

    wanted.set_enabled(false);
    REQUIRE_MSG(wanted.level() == 0,
                "banked heat is dropped, not frozen at its current level",
                "mid-chase");
    REQUIRE(wanted.heat() == 0.0f);
    apricot_test::pass("turning it on mid-pursuit clears the stars you already had");
}

void test_re_enabling_starts_from_clear() {
    WantedSystem wanted;
    wanted.add_heat(9.0f, WantedSystem::Crime::Violent);
    wanted.set_enabled(false);
    wanted.set_enabled(true);
    REQUIRE_MSG(wanted.level() == 0,
                "the old heat does not come back when suppression lifts",
                "re-enable");

    // ...and the system is live again rather than latched off.
    wanted.add_heat(9.0f, WantedSystem::Crime::Violent);
    REQUIRE(wanted.level() > 0);
    apricot_test::pass("re-enabling starts clear and accepts heat again");
}

}  // namespace

int main() {
    test_the_toggle_sits_below_the_five_levels();
    test_activating_it_raises_the_action_and_flips_the_value();
    test_the_page_never_shows_a_level_active_while_suppressed();
    test_choosing_a_level_clears_the_suppression();
    test_suppression_refuses_new_heat();
    test_switching_it_on_ends_a_pursuit_rather_than_freezing_it();
    test_re_enabling_starts_from_clear();
    return apricot_test::done("dev_never_wanted_tests");
}
