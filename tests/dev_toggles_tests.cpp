// The three developer switches on the F1 menu: FRAME LOGGING, GOD MODE and
// VEHICLE GOD MODE.
//
// The menu half is what is testable headlessly, and it is also where these go
// wrong. Each toggle is a row index and a row index is a number somebody will
// renumber: the wanted-level suite next door already relies on PLAYER &
// VEHICLE sitting at root row 2 and WANTED LEVEL at vehicle row 3. Adding rows
// beneath them has to leave those alone, so that is asserted here rather than
// discovered later as a menu that opens the wrong page.

#include <cstring>

#include "app/dev_menu.h"
#include "test_assert.h"

using namespace apricot;

namespace {

DevMenu opened() {
    DevMenu menu;
    menu.toggle();
    REQUIRE(menu.open());
    REQUIRE(menu.page() == DevMenuPage::Root);
    return menu;
}

DevMenu open_vehicle_page() {
    DevMenu menu = opened();
    menu.set_selection(2);
    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::None);
    REQUIRE(menu.page() == DevMenuPage::Vehicle);
    return menu;
}

// The rows the other suites navigate by index must not have moved.
void test_new_rows_did_not_renumber_the_old_ones() {
    DevMenu root = opened();
    REQUIRE(root.item_count() == 8);
    REQUIRE(std::strcmp(root.item_label(2), "PLAYER & VEHICLE  >") == 0);
    REQUIRE(std::strcmp(root.item_label(6), "FRAME LOGGING") == 0);
    REQUIRE_MSG(std::strcmp(root.item_label(7), "REPORT BUG  [F2]") == 0,
                "REPORT BUG stays last", "root");

    DevMenu vehicle = open_vehicle_page();
    REQUIRE(vehicle.item_count() == 6);
    REQUIRE(std::strcmp(vehicle.item_label(3), "WANTED LEVEL  >") == 0);
    REQUIRE(std::strcmp(vehicle.item_label(4), "GOD MODE") == 0);
    REQUIRE(std::strcmp(vehicle.item_label(5), "VEHICLE GOD MODE") == 0);
    apricot_test::pass("the added rows sit below the ones other pages navigate by index");
}

// Recording costs a file and a little time every frame; a session nobody asked
// to profile should not be paying for one.
void test_every_toggle_starts_off() {
    DevMenu root = opened();
    REQUIRE(!root.frame_logging());
    REQUIRE(std::strcmp(root.item_value(6), "OFF") == 0);

    DevMenu vehicle = open_vehicle_page();
    REQUIRE(!vehicle.god_mode());
    REQUIRE(!vehicle.vehicle_god_mode());
    REQUIRE(std::strcmp(vehicle.item_value(4), "OFF") == 0);
    REQUIRE(std::strcmp(vehicle.item_value(5), "OFF") == 0);
    apricot_test::pass("frame logging and both god modes default to off");
}

void test_frame_logging_toggles_both_ways() {
    DevMenu menu = opened();
    menu.set_selection(6);

    const DevMenuAction on = menu.update(kBtnAccept);
    REQUIRE(on.kind == DevMenuActionKind::SetFrameLogging);
    REQUIRE(on.frame_logging);
    REQUIRE(menu.frame_logging());
    REQUIRE(std::strcmp(menu.item_value(6), "ON") == 0);
    REQUIRE_MSG(menu.page() == DevMenuPage::Root,
                "a toggle does not navigate anywhere", "page");

    const DevMenuAction off = menu.update(kBtnAccept);
    REQUIRE(off.kind == DevMenuActionKind::SetFrameLogging);
    REQUIRE(!off.frame_logging);
    REQUIRE(!menu.frame_logging());
    REQUIRE(std::strcmp(menu.item_value(6), "OFF") == 0);
    apricot_test::pass("FRAME LOGGING reports on and off, and stays on the root page");
}

void test_god_mode_toggles_both_ways() {
    DevMenu menu = open_vehicle_page();
    menu.set_selection(4);

    const DevMenuAction on = menu.update(kBtnAccept);
    REQUIRE(on.kind == DevMenuActionKind::SetGodMode);
    REQUIRE(on.god_mode);
    REQUIRE(menu.god_mode());
    REQUIRE(std::strcmp(menu.item_value(4), "ON") == 0);

    const DevMenuAction off = menu.update(kBtnAccept);
    REQUIRE(off.kind == DevMenuActionKind::SetGodMode);
    REQUIRE(!off.god_mode);
    REQUIRE(std::strcmp(menu.item_value(4), "OFF") == 0);
    apricot_test::pass("GOD MODE reports on and off");
}

void test_vehicle_god_mode_toggles_both_ways() {
    DevMenu menu = open_vehicle_page();
    menu.set_selection(5);

    const DevMenuAction on = menu.update(kBtnAccept);
    REQUIRE(on.kind == DevMenuActionKind::SetVehicleGodMode);
    REQUIRE(on.vehicle_god_mode);
    REQUIRE(menu.vehicle_god_mode());
    REQUIRE(std::strcmp(menu.item_value(5), "ON") == 0);

    const DevMenuAction off = menu.update(kBtnAccept);
    REQUIRE(off.kind == DevMenuActionKind::SetVehicleGodMode);
    REQUIRE(!off.vehicle_god_mode);
    REQUIRE(std::strcmp(menu.item_value(5), "OFF") == 0);
    apricot_test::pass("VEHICLE GOD MODE reports on and off");
}

// The two god modes are separate switches. Driving an indestructible car while
// still being shootable on foot is a legitimate state, and the reverse too.
void test_the_two_god_modes_are_independent() {
    DevMenu menu = open_vehicle_page();
    menu.set_selection(4);
    menu.update(kBtnAccept);
    REQUIRE(menu.god_mode());
    REQUIRE_MSG(!menu.vehicle_god_mode(),
                "the player switch does not move the car switch", "player");

    menu.set_selection(5);
    menu.update(kBtnAccept);
    REQUIRE(menu.god_mode());
    REQUIRE(menu.vehicle_god_mode());

    menu.set_selection(4);
    menu.update(kBtnAccept);
    REQUIRE_MSG(!menu.god_mode(), "turned back off on its own", "player off");
    REQUIRE_MSG(menu.vehicle_god_mode(),
                "the car switch survives the player switch", "car stays");
    apricot_test::pass("the player and car switches move independently");
}

// Selecting a toggle must not leave the menu somewhere else; the neighbouring
// rows on these pages DO navigate, so this is the easy mistake.
void test_toggles_do_not_navigate() {
    DevMenu menu = open_vehicle_page();
    for (int row : {4, 5}) {
        menu.set_selection(row);
        menu.update(kBtnAccept);
        REQUIRE_MSG(menu.page() == DevMenuPage::Vehicle,
                    "a toggle stays on its page", "vehicle");
    }
    // ...while row 3 still does navigate, which is what makes the above a
    // real distinction rather than a tautology.
    menu.set_selection(3);
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::Wanted);
    apricot_test::pass("toggles stay put while the page rows still navigate");
}

}  // namespace

int main() {
    test_new_rows_did_not_renumber_the_old_ones();
    test_every_toggle_starts_off();
    test_frame_logging_toggles_both_ways();
    test_god_mode_toggles_both_ways();
    test_vehicle_god_mode_toggles_both_ways();
    test_the_two_god_modes_are_independent();
    test_toggles_do_not_navigate();
    return apricot_test::done("dev_toggles_tests");
}
