#include <cstring>

#include "app/dev_menu.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    DevMenu menu;
    menu.toggle();
    menu.set_selection(3);
    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::None);
    REQUIRE(menu.page() == DevMenuPage::WeatherTime);
    REQUIRE(menu.item_count() == 3);
    REQUIRE(std::strcmp(menu.item_label(2), "SNOW ACCUMULATION  >") == 0);

    menu.set_selection(2);
    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::None);
    REQUIRE(menu.page() == DevMenuPage::SnowDepth);
    REQUIRE(menu.item_count() == static_cast<int>(kDevSnowDepthValues.size()));

    menu.set_selection(8);
    const DevMenuAction one_metre = menu.update(kBtnAccept);
    REQUIRE(one_metre.kind == DevMenuActionKind::SetSnowDepth);
    REQUIRE(one_metre.snow_depth_m == 1.0f);

    menu.set_selection(0);
    const DevMenuAction automatic = menu.update(kBtnAccept);
    REQUIRE(automatic.kind == DevMenuActionKind::SetSnowDepth);
    REQUIRE(automatic.snow_depth_m < 0.0f);

    apricot_test::pass("dev weather menu exposes manual and automatic snow depth");
    return apricot_test::done("dev_weather_menu_tests");
}
