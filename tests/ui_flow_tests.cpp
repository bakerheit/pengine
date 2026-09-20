#include <cstdio>
#include <cstring>

#include "app/dev_menu.h"
#include "app/bug_report.h"
#include "app/overlay.h"
#include "core/units.h"
#include "city/roads.h"
#include "game/ui_flow.h"
#include "game/map_geometry.h"
#include "game/ui_canvas.h"
#include "game/intro_layout.h"
#include "terrain/heightmap.h"
#include "terrain/scatter.h"
#include "test_assert.h"

using namespace apricot;

namespace {

void intro_is_aspect_safe_and_waits_for_both_time_and_assets() {
    for (const glm::vec2 drawable : {glm::vec2{1280,720}, glm::vec2{1600,1000},
                                    glm::vec2{1280,1000}, glm::vec2{3440,1440}}) {
        const auto vp = UiCanvas::from_drawable(drawable).size;
        const auto layout = IntroLayout::from_canvas(vp);
        REQUIRE(layout.logo_position.x >= vp.x * 0.05f);
        REQUIRE(layout.logo_position.x + layout.logo_size.x < vp.x);
        REQUIRE(layout.logo_position.y + layout.logo_size.y < layout.menu_position.y);
        REQUIRE(layout.menu_position.y + 3 * layout.row_height < vp.y * 0.9f);
        REQUIRE(layout.menu_position.x + layout.menu_width < vp.x);
        REQUIRE_NEAR(layout.logo_size.x / layout.logo_size.y, 1600.0 / 740.0, 1e-5);
        const glm::vec2 image{1672,941};
        const auto cover = intro_cover_scale(drawable, image);
        REQUIRE(cover.x > 0 && cover.x <= 1);
        REQUIRE(cover.y > 0 && cover.y <= 1);
        REQUIRE_NEAR(cover.x * image.x / drawable.x,
                     cover.y * image.y / drawable.y, 1e-5);
        // Pointer and drawing share the same title geometry at both DPIs.
        const auto target = layout.menu_position + glm::vec2{80, 30};
        for (float dpi : {1.0f, 2.0f}) {
            const auto window = drawable / dpi;
            const auto mapped = UiCanvas::from_drawable(drawable).from_window(target / vp * window, window);
            REQUIRE_NEAR(mapped.x, target.x, 1e-3);
            REQUIRE_NEAR(mapped.y, target.y, 1e-3);
        }
    }
    REQUIRE(!intro_ready(6.999, true));
    REQUIRE(!intro_ready(20.0, false));
    REQUIRE(intro_ready(7.0, true));
    REQUIRE(intro_cover_scale({0,0}, {1672,941}) == glm::vec2(1));
    apricot_test::pass("intro crop, logo, menu, pointer and seven-second readiness stay consistent");
}

void map_road_names_are_authored_and_numbered_in_order() {
    const char* north_south[] = {"Briar Street", "Mercer Avenue", "Bellweather Road",
        "Rook Lane", "Pinatty Row", "Juniper Avenue", "Ashford Street",
        "Wren Road", "Cinder Street"};
    const char* east_west[] = {"First Street", "Second Street", "Third Street",
        "Fourth Street", "Fifth Street", "Halloway Street", "Sixth Street",
        "Seventh Street", "Eighth Street", "Ninth Street", "Tenth Street"};
    int named = 0;
    for (const auto& road : city::kRoads) {
        REQUIRE(std::strstr(road.name, "Pinatty NS") == nullptr);
        REQUIRE(std::strstr(road.name, "Pinatty EW") == nullptr);
        if (road.id >= 20 && road.id <= 28) {
            REQUIRE(std::strcmp(road.name, north_south[road.id - 20]) == 0);
            ++named;
        }
        if (road.id >= 30 && road.id <= 40) {
            REQUIRE(std::strcmp(road.name, east_west[road.id - 30]) == 0);
            ++named;
        }
    }
    REQUIRE(named == 20);
    apricot_test::pass("map streets use ordered numbers and real road names");
}

void ui_canvas_scales_with_height_and_keeps_input_aligned() {
    const glm::vec2 drawables[] = {
        {2560.0f, 1440.0f}, {3200.0f, 2000.0f},
        {2560.0f, 2000.0f}, {1280.0f, 720.0f}, {3840.0f, 2160.0f},
    };
    for (const glm::vec2 drawable : drawables) {
        const UiCanvas canvas = UiCanvas::from_drawable(drawable);
        REQUIRE_NEAR(canvas.size.y, 1440.0f, 1e-5);
        REQUIRE_NEAR(canvas.size.x / canvas.size.y,
                     drawable.x / drawable.y, 1e-5);
        const glm::vec2 scale = drawable / canvas.size;
        REQUIRE_NEAR(scale.x, scale.y, 1e-5);
        // Panel height and bottom margin keep the same screen-height ratio.
        REQUIRE_NEAR(154.0f * scale.y / drawable.y, 154.0f / 1440.0f, 1e-5);
        REQUIRE_NEAR(24.0f * scale.y / drawable.y, 24.0f / 1440.0f, 1e-5);
        for (const float dpi : {1.0f, 2.0f}) {
            const glm::vec2 logical = drawable / dpi;
            const glm::vec2 target{canvas.size.x - 100.0f, 700.0f};
            const glm::vec2 pointer = target / canvas.size * logical;
            const glm::vec2 mapped = canvas.from_window(pointer, logical);
            REQUIRE_NEAR(mapped.x, target.x, 1e-3);
            REQUIRE_NEAR(mapped.y, target.y, 1e-3);
            const glm::vec2 drag = canvas.from_window(logical * 0.1f, logical);
            REQUIRE_NEAR(drag.y, 144.0f, 1e-4);
        }
    }
    REQUIRE(UiCanvas::from_drawable({0.0f, 0.0f}).size == glm::vec2{0.0f});
    REQUIRE(UiCanvas::from_drawable({1280.0f, 0.0f}).size == glm::vec2{0.0f});
    REQUIRE(UiCanvas::from_drawable({-1.0f, 720.0f}).size == glm::vec2{0.0f});
    REQUIRE(UiCanvas::from_drawable({1280.0f, 720.0f})
                .from_window({100.0f, 100.0f}, {0.0f, 0.0f}) == glm::vec2{0.0f});
    apricot_test::pass("HUD proportions and pointer mapping survive fullscreen, tall windows and DPI changes");
}

void american_speed_conversion_is_exact() {
    REQUIRE_NEAR(metres_per_second_to_miles_per_hour(0.0f), 0.0, 1e-6);
    REQUIRE_NEAR(metres_per_second_to_miles_per_hour(1.0f), 2.2369363, 1e-5);
    REQUIRE_NEAR(miles_per_hour_to_metres_per_second(60.0f), 26.8224, 1e-5);
    REQUIRE_NEAR(miles_per_hour_to_metres_per_second(
                     metres_per_second_to_miles_per_hour(17.5f)),
                 17.5, 1e-5);
    apricot_test::pass("player-facing speed conversion uses miles per hour");
}

void debug_stats_overlay_starts_hidden_and_toggles() {
    overlay::Controls controls;
    REQUIRE(!controls.stats_visible);
    controls.toggle_stats();
    REQUIRE(controls.stats_visible);
    controls.toggle_stats();
    REQUIRE(!controls.stats_visible);
    apricot_test::pass("F3 debug stats state starts hidden and toggles");
}

void title_menu_wraps_and_starts() {
    UiFlow ui;
    REQUIRE(ui.screen() == UiScreen::Title);
    REQUIRE(ui.item_count() == 4);

    ui.update(kBtnMenuUp);
    REQUIRE(ui.selection() == 3);
    ui.update(kBtnMenuDown);
    REQUIRE(ui.selection() == 0);

    const UiAction action = ui.update(kBtnAccept);
    REQUIRE(action == UiAction::BeginDrive);
    REQUIRE(ui.screen() == UiScreen::Driving);
    ui.show_title(); ui.set_save_available(true);
    REQUIRE(ui.item_count() == 5);
    REQUIRE(ui.update(kBtnAccept) == UiAction::LoadGame);
    REQUIRE(ui.screen() == UiScreen::Title); // Failed load must remain here.
    ui.enter_game(); ui.update(kBtnPause);
    ui.set_selection(1);
    REQUIRE(ui.update(kBtnAccept) == UiAction::SaveGame);
    REQUIRE(ui.screen() == UiScreen::Pause);
    ui.set_selection(2);
    REQUIRE(ui.update(kBtnAccept) == UiAction::LoadGame);
    REQUIRE(ui.screen() == UiScreen::Pause);
    ui.show_title(); ui.set_selection(1);
    REQUIRE(ui.update(kBtnAccept) == UiAction::NewGame);
    REQUIRE(ui.screen() == UiScreen::Driving);
    apricot_test::pass("title navigation wraps and Start enters the drive");
}

void settings_menu_changes_player_preferences() {
    UiFlow ui;
    ui.set_selection(2);
    REQUIRE(ui.update(kBtnAccept) == UiAction::None);
    REQUIRE(ui.screen() == UiScreen::Settings);
    REQUIRE(ui.settings_page() == SettingsPage::Root);
    REQUIRE(ui.item_count() == 5);
    ui.set_settings_hovered_category(2);
    REQUIRE(ui.settings_hovered_category() == 2);
    ui.clear_settings_hovered_category();
    REQUIRE(ui.settings_hovered_category() == -1);

    REQUIRE(ui.update(kBtnAccept) == UiAction::None);
    REQUIRE(ui.settings_page() == SettingsPage::Map);
    REQUIRE(ui.settings().minimap_points_of_interest);
    ui.update(kBtnMenuRight);
    REQUIRE(!ui.settings().minimap_points_of_interest);
    REQUIRE(std::strcmp(ui.settings_value(0), "OFF") == 0);

    ui.update(kBtnBack);
    REQUIRE(ui.settings_page() == SettingsPage::Root);
    ui.set_selection(1);
    ui.update(kBtnAccept);
    ui.set_selection(1);
    ui.update(kBtnMenuRight);
    REQUIRE(ui.settings().camera_fov == 70);
    ui.update(kBtnBack);
    ui.update(kBtnBack);
    REQUIRE(ui.screen() == UiScreen::Title);

    ui.enter_game();
    ui.update(kBtnPause);
    REQUIRE(ui.item_count() == 7);
    ui.set_selection(5);
    ui.update(kBtnAccept);
    REQUIRE(ui.screen() == UiScreen::Settings);
    REQUIRE(ui.settings_return_screen() == UiScreen::Pause);
    apricot_test::pass("settings opens from title and pause and changes map and graphics preferences");
}

void map_returns_to_the_screen_that_opened_it() {
    UiFlow from_title;
    from_title.update(kBtnMap);
    REQUIRE(from_title.screen() == UiScreen::Map);
    REQUIRE(from_title.map_return_screen() == UiScreen::Title);
    from_title.update(kBtnBack);
    REQUIRE(from_title.screen() == UiScreen::Title);

    UiFlow from_drive;
    from_drive.update(kBtnAccept);
    from_drive.update(kBtnMap);
    REQUIRE(from_drive.screen() == UiScreen::Map);
    REQUIRE(from_drive.map_return_screen() == UiScreen::Driving);
    from_drive.update(kBtnMap);
    REQUIRE(from_drive.screen() == UiScreen::Driving);

    UiFlow from_pause;
    from_pause.update(kBtnAccept);
    from_pause.update(kBtnPause);
    from_pause.update(kBtnMap);
    REQUIRE(from_pause.screen() == UiScreen::Map);
    REQUIRE(from_pause.map_return_screen() == UiScreen::Pause);
    from_pause.update(kBtnBack);
    REQUIRE(from_pause.screen() == UiScreen::Pause);
    apricot_test::pass("map closes back to title, pause, or driving");
}

void pause_actions_are_real_actions() {
    UiFlow ui;
    ui.update(kBtnAccept);
    ui.update(kBtnPause);
    REQUIRE(ui.screen() == UiScreen::Pause);
    REQUIRE(ui.item_count() == 7);

    ui.set_selection(4);
    REQUIRE(ui.update(kBtnAccept) == UiAction::ResetVehicle);
    REQUIRE(ui.screen() == UiScreen::Driving);

    ui.update(kBtnPause);
    ui.set_selection(6);
    REQUIRE(ui.update(kBtnAccept) == UiAction::None);
    REQUIRE(ui.screen() == UiScreen::Title);

    ui.set_selection(3);
    REQUIRE(ui.update(kBtnAccept) == UiAction::QuitGame);
    apricot_test::pass("pause can resume, restart, or return to a quitting title");
}

void map_projection_keeps_pinattys_compass() {
    const MapViewport vp{100.0f, 40.0f, 600.0f, 420.0f};
    const MapPoint nw = world_to_map(-city::kWorldHalfMetres,
                                     -city::kWorldHalfMetres, vp);
    const MapPoint se = world_to_map(city::kWorldHalfMetres,
                                     city::kWorldHalfMetres, vp);
    const MapPoint centre = world_to_map(0.0f, 0.0f, vp);

    REQUIRE_NEAR(nw.x, 190.0, 1e-5);
    REQUIRE_NEAR(nw.y, 40.0, 1e-5);
    REQUIRE_NEAR(se.x, 610.0, 1e-5);
    REQUIRE_NEAR(se.y, 460.0, 1e-5);
    REQUIRE_NEAR(centre.x, 400.0, 1e-5);
    REQUIRE_NEAR(centre.y, 250.0, 1e-5);
    const MapPoint east = world_to_map(100, 0, vp);
    const MapPoint south = world_to_map(0, 100, vp);
    REQUIRE_NEAR(east.x - centre.x, south.y - centre.y, 1e-4);
    apricot_test::pass("rectangular maps keep compass directions and equal distance scales");
}

void map_coastline_interpolates_and_clips_without_losing_area() {
    const std::array<glm::vec2, 3> triangle{{{0, 0}, {2, 0}, {0, 2}}};
    const auto area = [](const MapPolygon& polygon) {
        float sum = 0;
        for (int i = 0; i < polygon.count; ++i) {
            const auto a = polygon.points[static_cast<std::size_t>(i)];
            const auto b = polygon.points[static_cast<std::size_t>((i + 1) % polygon.count)];
            sum += a.x * b.y - a.y * b.x;
        }
        return std::fabs(sum) * 0.5f;
    };
    for (int mask = 0; mask < 8; ++mask) {
        std::array<float, 3> heights{};
        for (int i = 0; i < 3; ++i)
            heights[static_cast<std::size_t>(i)] = (mask & (1 << i)) ? 3.0f : -1.0f;
        const auto land = slice_map_triangle(triangle, heights, 0);
        for (float& h : heights) h = -h;
        const auto water = slice_map_triangle(triangle, heights, 0);
        REQUIRE_NEAR(area(land.land) + area(water.land), 2.0, 1e-5);
        REQUIRE(land.land.count <= 4);
        REQUIRE(land.crossing_count == 0 || land.crossing_count == 2);
    }
    const auto coast = slice_map_triangle(triangle, {-1, 3, 3}, 0);
    REQUIRE_NEAR(coast.crossings[0].x, 0.5, 1e-5);
    REQUIRE_NEAR(coast.crossings[1].y, 0.5, 1e-5);
    REQUIRE_NEAR(area(coast.land), 1.875, 1e-5);
    const auto clipped = clip_map_polygon(coast.land, {0, 0}, {1, 1});
    REQUIRE_NEAR(area(clipped), 0.875, 1e-5);
    for (int i = 0; i < clipped.count; ++i) {
        const auto p = clipped.points[static_cast<std::size_t>(i)];
        REQUIRE(p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1);
    }
    apricot_test::pass("vector coastline interpolates shore crossings and preserves clipped fill area");
}

void map_layers_cycle_without_changing_the_return_screen() {
    UiFlow flow;
    flow.update(kBtnMap);
    REQUIRE(flow.map_layer() == MapLayer::Explore);
    flow.update(kBtnCamCycle);
    REQUIRE(flow.map_layer() == MapLayer::Roads);
    flow.update(kBtnCamCycle);
    REQUIRE(flow.map_layer() == MapLayer::Places);
    flow.update(kBtnBack);
    REQUIRE(flow.screen() == UiScreen::Title);
    flow.update(kBtnMap);
    REQUIRE(flow.map_layer() == MapLayer::Places);
    flow.update(kBtnCamCycle);
    REQUIRE(flow.map_layer() == MapLayer::Explore);
    apricot_test::pass("map layers cycle and persist across reopening");
}

void map_camera_zooms_pans_and_stays_on_the_island() {
    MapCamera camera;
    REQUIRE_NEAR(camera.zoom_level(), 1.0, 1e-5);

    camera.zoom(2.0f, 900.0f, -700.0f);
    for (int i = 0; i < 60; ++i) camera.update(1.0f / 60.0f);
    REQUIRE(camera.zoom_level() > 1.8f);
    REQUIRE(camera.center_x() > 500.0f);
    REQUIRE(camera.center_z() < -350.0f);

    const float before_x = camera.center_x();
    camera.pan(1.0f, 0.0f, 0.1f);
    for (int i = 0; i < 30; ++i) camera.update(1.0f / 60.0f);
    REQUIRE(camera.center_x() > before_x);

    camera.zoom(100.0f, 0.0f, 0.0f);
    for (int i = 0; i < 60; ++i) camera.update(1.0f / 60.0f);
    REQUIRE_NEAR(camera.target_span_m(), MapCamera::kClosestSpanM, 1e-4);
    camera.pan_world(100000.0f, 100000.0f);
    const float limit = city::kWorldHalfMetres - camera.span_m() * 0.5f;
    REQUIRE(camera.center_x() <= limit + 1e-3f);
    REQUIRE(camera.center_z() <= limit + 1e-3f);

    camera.set_viewport_size(1200, 600);
    camera.pan_world(100000, 100000);
    REQUIRE(camera.center_x() <= city::kWorldHalfMetres - camera.span_m() + 1e-3f);
    REQUIRE(camera.center_z() <= limit + 1e-3f);

    camera.zoom(-100.0f, 0.0f, 0.0f);
    REQUIRE_NEAR(camera.target_span_m(), MapCamera::kFullSpanM, 1e-4);
    apricot_test::pass("map camera zooms, pans, and clamps to the state atlas");
}

void map_camera_opens_at_a_200m_player_centered_view() {
    MapCamera camera;
    camera.set_viewport_size(1200, 600);
    camera.open_at(900.0f, -700.0f);

    REQUIRE_NEAR(camera.span_m(), MapCamera::kDefaultOpenSpanM, 1e-5);
    REQUIRE_NEAR(camera.target_span_m(), MapCamera::kDefaultOpenSpanM, 1e-5);
    REQUIRE_NEAR(camera.center_x(), 900.0f, 1e-5);
    REQUIRE_NEAR(camera.center_z(), -700.0f, 1e-5);

    const auto view = camera.viewport(24, 104, 1200, 600);
    const auto player = world_to_map(900.0f, -700.0f, view);
    REQUIRE_NEAR(player.x, 624.0f, 1e-5);
    REQUIRE_NEAR(player.y, 404.0f, 1e-5);
    apricot_test::pass("full map opens at 200 m centered on the player");
}

void map_wheel_zoom_keeps_the_visible_point_under_the_cursor() {
    for (const MapPoint size : {MapPoint{900, 900}, MapPoint{1200, 600},
                                MapPoint{600, 1200}}) {
        MapCamera camera;
        camera.set_viewport_size(size.x, size.y);
        camera.zoom(4.0f, 0.0f, 0.0f);
        camera.update(10.0f);
        // Move the pointer between wheel events while zoom is still animating.
        for (const float steps : {1.0f, 0.5f, -1.0f, 2.0f, -0.5f}) {
            const auto before = camera.viewport(24, 104, size.x, size.y);
            const MapPoint pointer{24 + size.x * (steps > 0 ? 0.65f : 0.35f),
                                   104 + size.y * 0.4f};
            const float scale = map_pixels_per_metre(before);
            const float world_x = camera.center_x() +
                (pointer.x - before.left - size.x * 0.5f) / scale;
            const float world_z = camera.center_z() +
                (pointer.y - before.top - size.y * 0.5f) / scale;
            camera.zoom_at(steps, pointer, before);
            for (int frame = 0; frame < 4; ++frame) {
                camera.update(1.0f / 60.0f);
                const auto point = world_to_map(world_x, world_z,
                    camera.viewport(24, 104, size.x, size.y));
                REQUIRE_NEAR(point.x, pointer.x, 0.002f);
                REQUIRE_NEAR(point.y, pointer.y, 0.002f);
            }
        }
    }
    apricot_test::pass("wheel zoom anchors the visible point through animation and cursor changes");
}

void map_wheel_zoom_handles_overview_panel_edges_and_limits() {
    MapCamera camera;
    camera.set_viewport_size(900, 900);
    const auto overview = camera.viewport(24, 104, 900, 900);
    camera.zoom_at(1.0f, {23, 400}, overview);
    camera.zoom_at(1.0f, {500, 103}, overview);
    camera.zoom_at(1.0f, {925, 400}, overview);
    camera.zoom_at(1.0f, {500, 1005}, overview);
    REQUIRE_NEAR(camera.target_span_m(), MapCamera::kFullSpanM, 1e-4);
    const MapPoint pointer{699, 329};
    const float world_x = MapCamera::kFullSpanM * 0.25f;
    const float world_z = -world_x;
    camera.zoom_at(1.0f, pointer, overview);
    for (int i = 0; i < 120; ++i) {
        camera.update(1.0f / 60.0f);
        const auto point = world_to_map(world_x, world_z,
            camera.viewport(24, 104, 900, 900));
        REQUIRE_NEAR(point.x, pointer.x, 0.002f);
        REQUIRE_NEAR(point.y, pointer.y, 0.002f);
    }
    camera.zoom_at(100, pointer, camera.viewport(24, 104, 900, 900));
    camera.update(10);
    REQUIRE_NEAR(camera.span_m(), MapCamera::kClosestSpanM, 0.001f);
    const float x = camera.center_x(), z = camera.center_z();
    camera.zoom_at(1, {24, 104}, camera.viewport(24, 104, 900, 900));
    camera.update(10);
    REQUIRE_NEAR(camera.center_x(), x, 0.001f);
    REQUIRE_NEAR(camera.center_z(), z, 0.001f);
    camera.set_viewport_size(1200, 600);
    camera.zoom_at(-100, {24, 104}, camera.viewport(24, 104, 1200, 600));
    for (int i = 0; i < 120; ++i) {
        camera.update(1.0f / 60.0f);
        REQUIRE(std::fabs(camera.center_x()) <=
                std::max(0.0f, city::kWorldHalfMetres - camera.span_m()) + 0.001f);
        REQUIRE(std::fabs(camera.center_z()) <=
                std::max(0.0f, city::kWorldHalfMetres - camera.span_m() * 0.5f) + 0.001f);
    }
    REQUIRE_NEAR(camera.target_span_m(), MapCamera::kFullSpanM, 0.001f);
    apricot_test::pass("wheel zoom respects the map panel, overview, and island bounds");
}

void developer_menu_teleports_to_ostend_shore() {
    DevMenu menu;
    menu.toggle();
    menu.update(kBtnAccept);
    int ostend=-1;
    for (int i=0;i<menu.item_count();++i) {
        if (std::strcmp(kDevTeleportLocations[static_cast<std::size_t>(i)].name,
                        "OSTEND BAIT & TACKLE")==0) ostend=i;
    }
    REQUIRE(ostend>=0);
    for (int i=0;i<ostend;++i) menu.update(kBtnMenuDown);
    const auto action=menu.update(kBtnAccept);
    REQUIRE(action.kind==DevMenuActionKind::Teleport);
    REQUIRE(action.location_index==ostend);
    const auto& destination=kDevTeleportLocations[static_cast<std::size_t>(ostend)];
    REQUIRE_NEAR(destination.world_xz.x,-2033.f,.001);
    REQUIRE_NEAR(destination.world_xz.y,-600.f,.001);
    REQUIRE_NEAR(destination.heading_radians,1.745329252f,.001);
    apricot_test::pass("Ostend teleport selects the checked shore arrival facing the dock");
}

void developer_menu_teleports_to_safe_florangia_palms() {
    int florangia = -1;
    for (int i = 0; i < static_cast<int>(kDevTeleportLocations.size()); ++i) {
        if (std::strcmp(kDevTeleportLocations[static_cast<std::size_t>(i)].name,
                        "FLORANGIA PALM COAST") == 0) {
            florangia = i;
        }
    }
    REQUIRE(florangia >= 0);
    const DevTeleportLocation& destination =
        kDevTeleportLocations[static_cast<std::size_t>(florangia)];
    REQUIRE_NEAR(destination.world_xz.x, 4670.0f, 0.001f);
    REQUIRE_NEAR(destination.world_xz.y, 5100.0f, 0.001f);
    REQUIRE(height_at(city::kMapSeed, destination.world_xz.x,
                      destination.world_xz.y) > 1.0f);
    REQUIRE(normal_at(city::kMapSeed, destination.world_xz.x,
                      destination.world_xz.y).y > 0.96f);

    bool palm_in_view = false;
    bool arrival_clear = true;
    const ChunkCoord centre = chunk_at(destination.world_xz.x,
                                       destination.world_xz.y);
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            for (const ScatterProp& prop : scatter_chunk(
                     city::kMapSeed, {centre.x + dx, centre.z + dz})) {
                const glm::vec2 delta = glm::vec2{prop.position.x, prop.position.z} -
                    destination.world_xz;
                const float distance = glm::length(delta);
                if (prop.kind == PropKind::Tree &&
                    is_palm_tree_variant(prop.variant) && distance < 120.0f) {
                    palm_in_view = true;
                }
                if (distance < 3.5f) arrival_clear = false;
            }
        }
    }
    REQUIRE(palm_in_view);
    REQUIRE(arrival_clear);
    apricot_test::pass("Florangia teleport lands flat and clear with palms in view");
}

void developer_menu_teleports_to_florangia_airport_access() {
    int airport = -1;
    for (int i = 0; i < static_cast<int>(kDevTeleportLocations.size()); ++i) {
        if (std::strcmp(kDevTeleportLocations[static_cast<std::size_t>(i)].name,
                        "FLORANGIA REGIONAL AIRPORT") == 0) {
            airport = i;
        }
    }
    REQUIRE(airport >= 0);
    const DevTeleportLocation& destination =
        kDevTeleportLocations[static_cast<std::size_t>(airport)];
    REQUIRE_NEAR(destination.world_xz.x, 4800.0f, 0.001f);
    REQUIRE_NEAR(destination.world_xz.y, 4685.0f, 0.001f);
    REQUIRE_NEAR(height_at(city::kMapSeed, destination.world_xz.x,
                           destination.world_xz.y),
                 city::kFlorangiaAirportSite.ground_m, 0.06f);
    REQUIRE(normal_at(city::kMapSeed, destination.world_xz.x,
                      destination.world_xz.y).y > 0.999f);
    REQUIRE(city::florangia_airport_lot_contains(destination.world_xz.x,
                                                  destination.world_xz.y));
    apricot_test::pass(
        "Florangia airport teleport lands flat on the public access road");
}

void developer_panel_scrolls_instead_of_squashing_rows() {
    // A row's label is 20 px of glyph at kUiTextScale 1.21 drawn at +11, so it
    // needs 35.2 px and rows must never go below kDevMenuMinRowHeight. The old
    // layout shrank to a 24 px floor to keep every entry on screen, which
    // overlapped every label from seventeen entries on and would have run the
    // panel off a 720p canvas entirely at twenty-two. Now rows keep their
    // height and the list scrolls.
    const int locations = static_cast<int>(kDevTeleportLocations.size());

    // SHORT LIST: everything visible, nothing to scroll, full row height.
    for (const int count : {1, 4, 11}) {
        const DevMenuPanelLayout few = dev_menu_panel_layout(count, 720.0f);
        REQUIRE(few.visible_rows == count);
        REQUIRE(!few.scrolls);
        REQUIRE_NEAR(few.row_height, 43.0f, 0.001f);
        REQUIRE(few.bottom <= 700.001f);
        REQUIRE(dev_menu_first_visible_row(count, count - 1,
                                           few.visible_rows) == 0);
    }

    // OVERFLOWING LIST at 720p: rows stay readable, panel stays on screen,
    // and it reports that it is showing a window.
    const DevMenuPanelLayout compact = dev_menu_panel_layout(locations, 720.0f);
    REQUIRE(compact.row_height >= kDevMenuMinRowHeight);
    REQUIRE(compact.bottom <= 700.001f);
    REQUIRE(compact.visible_rows < locations);
    REQUIRE(compact.scrolls);

    // 1080p has room for the whole list at full height.
    const DevMenuPanelLayout roomy = dev_menu_panel_layout(locations, 1080.0f);
    REQUIRE_NEAR(roomy.row_height, 43.0f, 0.001f);
    REQUIRE(roomy.visible_rows == locations);
    REQUIRE(!roomy.scrolls);

    // Every entry stays REACHABLE: for any selection, on any list length that
    // overflows, the window must contain it and must stay inside the list.
    for (const int count : {locations, locations + 1, 40, 200}) {
        const DevMenuPanelLayout layout = dev_menu_panel_layout(count, 720.0f);
        REQUIRE(layout.row_height >= kDevMenuMinRowHeight);
        REQUIRE(layout.bottom <= 700.001f);
        for (int selection = 0; selection < count; ++selection) {
            const int first = dev_menu_first_visible_row(
                count, selection, layout.visible_rows);
            REQUIRE(first >= 0);
            REQUIRE(first + layout.visible_rows <= count);
            REQUIRE_MSG(selection >= first &&
                        selection < first + layout.visible_rows,
                        "keyboard/controller selection scrolled out of view",
                        "dev panel window");
        }
        // Scroll boundaries: the top of the list shows the first row, and the
        // bottom shows the last, with no window hanging past either end.
        REQUIRE(dev_menu_first_visible_row(count, 0, layout.visible_rows) == 0);
        REQUIRE(dev_menu_first_visible_row(count, count - 1,
                                           layout.visible_rows) ==
                count - layout.visible_rows);
    }
    apricot_test::pass(
        "dev panel keeps rows readable, scrolls long lists, and never scrolls "
        "the selection out of view");
}

void developer_menu_offers_teleport_only_when_a_map_waypoint_exists() {
    DevMenu menu;
    menu.toggle();
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::Teleport);
    REQUIRE(menu.item_count() ==
            static_cast<int>(kDevTeleportLocations.size()));

    menu.update(kBtnBack);
    menu.set_waypoint_available(true);
    menu.update(kBtnAccept);
    REQUIRE(menu.item_count() ==
            static_cast<int>(kDevTeleportLocations.size()) + 1);
    REQUIRE(std::strcmp(menu.item_label(0), "MAP WAYPOINT") == 0);
    // The contract this test names is the OFFSET, not which location happens
    // to be authored first: the runtime waypoint row owns 0 and every authored
    // location keeps its own index one row below. Checking the whole mapping
    // states that directly. The old single hard-coded "HALLOWAY GAS" went
    // stale the moment SIX TWELVE NORTH was prepended, even though the offset
    // it was standing in for never moved.
    for (std::size_t i = 0; i < kDevTeleportLocations.size(); ++i)
        REQUIRE(std::strcmp(menu.item_label(static_cast<int>(i) + 1),
                            kDevTeleportLocations[i].name) == 0);
    REQUIRE(menu.update(kBtnAccept).kind ==
            DevMenuActionKind::TeleportWaypoint);

    menu.set_selection(1);
    const DevMenuAction authored = menu.update(kBtnAccept);
    REQUIRE(authored.kind == DevMenuActionKind::Teleport);
    REQUIRE(authored.location_index == 0);
    apricot_test::pass(
        "F1 exposes the manual map waypoint without shifting authored teleports");
}

void developer_menu_navigates_and_returns_a_teleport() {
    DevMenu menu;
    REQUIRE(!menu.open());
    REQUIRE(menu.driving_mechanics() == DrivingMechanicsStyle::ClassicGta);
    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::None);

    menu.toggle();
    REQUIRE(menu.open());
    REQUIRE(menu.page() == DevMenuPage::Root);
    // Six submenus, the FRAME LOGGING toggle, then REPORT BUG. This count is
    // pinned because the rows below it are navigated by index elsewhere; a new
    // row inserted rather than appended silently reroutes those tests.
    REQUIRE(menu.item_count() == 8);

    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::None);
    REQUIRE(menu.page() == DevMenuPage::Teleport);
    REQUIRE(menu.item_count() ==
            static_cast<int>(kDevTeleportLocations.size()));
    REQUIRE(menu.item_count() >= 5);

    menu.update(kBtnMenuUp);
    REQUIRE(menu.selection() == menu.item_count() - 1);
    const DevMenuAction action = menu.update(kBtnAccept);
    REQUIRE(action.kind == DevMenuActionKind::Teleport);
    REQUIRE(action.location_index == menu.item_count() - 1);
    const DevTeleportLocation& runway =
        kDevTeleportLocations[static_cast<std::size_t>(action.location_index)];
    REQUIRE(runway.name != nullptr);
    REQUIRE(runway.world_xz.x < 0.0f);
    REQUIRE(runway.world_xz.y > 2000.0f);

    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::Root);

    menu.update(kBtnMenuDown);
    REQUIRE(menu.selection() == 1);
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::DrivingMechanics);
    REQUIRE(menu.item_count() ==
            static_cast<int>(kDrivingMechanicsStyleCount));
    REQUIRE(menu.item_value(
                static_cast<int>(DrivingMechanicsStyle::ClassicGta))[0] !=
            '\0');

    menu.update(kBtnMenuDown);
    const DevMenuAction mechanics = menu.update(kBtnAccept);
    REQUIRE(mechanics.kind == DevMenuActionKind::SetDrivingMechanics);
    REQUIRE(mechanics.driving_mechanics == DrivingMechanicsStyle::Arcade);
    REQUIRE(menu.driving_mechanics() == DrivingMechanicsStyle::Arcade);
    REQUIRE(menu.item_value(1)[0] != '\0');

    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::Root);
    REQUIRE(menu.selection() == 1);

    menu.update(kBtnMenuDown);
    REQUIRE(menu.selection() == 2);
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::Vehicle);
    // The two god-mode toggles are APPENDED below WANTED LEVEL rather than
    // inserted, so rows 0-3 keep the indices this test and others navigate by.
    REQUIRE(menu.item_count() == 6);
    REQUIRE(std::strcmp(menu.item_label(0), "CHOOSE CAR  >") == 0);
    REQUIRE(std::strcmp(menu.item_label(1), "REPAIR") == 0);
    REQUIRE(std::strcmp(menu.item_label(2), "COPY POSITION") == 0);
    REQUIRE(std::strcmp(menu.item_label(3), "WANTED LEVEL  >") == 0);
    REQUIRE(std::strcmp(menu.item_label(4), "GOD MODE") == 0);
    REQUIRE(std::strcmp(menu.item_label(5), "VEHICLE GOD MODE") == 0);
    REQUIRE(std::strcmp(menu.item_value(0), "CAR 5") == 0);

    for (std::size_t brand = 1; brand < kPlayerCarBrands.size(); ++brand) {
        REQUIRE(std::strcmp(kPlayerCarBrands[brand - 1].name,
                            kPlayerCarBrands[brand].name) < 0);
    }
    for (const PlayerCarBrand& brand : kPlayerCarBrands) {
        for (std::size_t model = 1; model < brand.car_count; ++model) {
            REQUIRE(std::strcmp(
                        kPlayerCars[brand.first_car + model - 1].model,
                        kPlayerCars[brand.first_car + model].model) < 0);
        }
    }

    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::VehicleBrands);
    REQUIRE(menu.item_count() ==
            static_cast<int>(kPlayerCarBrands.size()));
    REQUIRE(menu.selection() == player_car_brand_index(PlayerCarId::LegacyCar5));
    menu.set_selection(player_car_brand_index(PlayerCarId::VesperVx91));
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::VehicleModels);
    REQUIRE(menu.item_count() == 3);
    REQUIRE(std::strcmp(menu.title(), "VESPER") == 0);
    REQUIRE(std::strcmp(menu.item_label(0), "MISTRAL") == 0);
    REQUIRE(menu.update(kBtnAccept).player_car == PlayerCarId::VesperMistral);
    menu.set_selection(1);
    REQUIRE(std::strcmp(menu.item_label(1), "SCYTHE") == 0);
    REQUIRE(menu.update(kBtnAccept).player_car == PlayerCarId::VesperScythe);
    menu.set_selection(2);
    REQUIRE(std::strcmp(menu.item_label(2), "VX-91") == 0);
    const DevMenuAction choose_car = menu.update(kBtnAccept);
    REQUIRE(choose_car.kind == DevMenuActionKind::SetPlayerCar);
    REQUIRE(choose_car.player_car == PlayerCarId::VesperVx91);
    REQUIRE(menu.player_car() == PlayerCarId::VesperVx91);
    REQUIRE(std::strcmp(menu.item_value(2), "ACTIVE") == 0);

    menu.update(kBtnBack);
    menu.set_selection(player_car_brand_index(PlayerCarId::OrisonCinderGt));
    menu.update(kBtnAccept);
    REQUIRE(std::strcmp(menu.title(), "ORISON") == 0);
    REQUIRE(menu.item_count() == 1);
    REQUIRE(std::strcmp(menu.item_label(0), "CINDER GT") == 0);
    const auto choose_cinder=menu.update(kBtnAccept);
    REQUIRE(choose_cinder.kind == DevMenuActionKind::SetPlayerCar);
    REQUIRE(choose_cinder.player_car == PlayerCarId::OrisonCinderGt);
    REQUIRE(std::strcmp(menu.item_value(0), "ACTIVE") == 0);

    menu.update(kBtnBack);
    menu.set_selection(player_car_brand_index(PlayerCarId::Bwc360));
    menu.update(kBtnAccept);
    REQUIRE(std::strcmp(menu.title(), "BWC") == 0);
    REQUIRE(menu.item_count() == 1);
    REQUIRE(std::strcmp(menu.item_label(0), "360") == 0);
    const auto choose_bwc=menu.update(kBtnAccept);
    REQUIRE(choose_bwc.kind == DevMenuActionKind::SetPlayerCar);
    REQUIRE(choose_bwc.player_car == PlayerCarId::Bwc360);
    REQUIRE(std::strcmp(menu.item_value(0), "ACTIVE") == 0);
    menu.update(kBtnBack);
    menu.set_selection(player_car_brand_index(PlayerCarId::HalcyonSovereign));
    menu.update(kBtnAccept);
    REQUIRE(menu.item_count()==2);
    REQUIRE(std::strcmp(menu.item_label(1),"SOVEREIGN LIMO")==0);
    menu.set_selection(1);
    REQUIRE(menu.update(kBtnAccept).player_car==PlayerCarId::HalcyonSovereign);
    menu.update(kBtnBack);
    menu.set_selection(player_car_brand_index(PlayerCarId::GlmLunge));
    menu.update(kBtnAccept);
    REQUIRE(std::strcmp(menu.title(), "GLM") == 0);
    REQUIRE(menu.item_count() == 2);
    REQUIRE(std::strcmp(menu.item_label(0), "LUNGE") == 0);
    REQUIRE(std::strcmp(menu.item_label(1), "ZIP") == 0);
    const DevMenuAction choose_glm = menu.update(kBtnAccept);
    REQUIRE(choose_glm.kind == DevMenuActionKind::SetPlayerCar);
    REQUIRE(choose_glm.player_car == PlayerCarId::GlmLunge);

    menu.set_selection(1);
    const DevMenuAction choose_zip = menu.update(kBtnAccept);
    REQUIRE(choose_zip.kind == DevMenuActionKind::SetPlayerCar);
    REQUIRE(choose_zip.player_car == PlayerCarId::GlmZip);
    REQUIRE(menu.player_car() == PlayerCarId::GlmZip);
    REQUIRE(std::strcmp(menu.item_value(1), "ACTIVE") == 0);

    menu.update(kBtnBack);
    menu.set_selection(player_car_brand_index(PlayerCarId::HarrowWorkman));
    menu.update(kBtnAccept);
    REQUIRE(std::strcmp(menu.title(), "HARROW") == 0);
    REQUIRE(menu.item_count() == 4);
    REQUIRE(std::strcmp(menu.item_label(0), "CITYLINER BUS") == 0);
    REQUIRE(menu.update(kBtnAccept).player_car == PlayerCarId::HarrowCityliner);
    menu.set_selection(1);
    REQUIRE(std::strcmp(menu.item_label(1), "HAULER SEMI") == 0);
    REQUIRE(menu.update(kBtnAccept).player_car == PlayerCarId::HarrowHauler);
    menu.set_selection(2);
    REQUIRE(std::strcmp(menu.item_label(2), "PARCEL") == 0);
    REQUIRE(menu.update(kBtnAccept).player_car == PlayerCarId::HarrowParcel);
    menu.set_selection(3);
    REQUIRE(std::strcmp(menu.item_label(3), "WORKMAN") == 0);
    const DevMenuAction choose_pickup = menu.update(kBtnAccept);
    REQUIRE(choose_pickup.kind == DevMenuActionKind::SetPlayerCar);
    REQUIRE(choose_pickup.player_car == PlayerCarId::HarrowWorkman);
    REQUIRE(menu.player_car() == PlayerCarId::HarrowWorkman);
    REQUIRE(std::strcmp(menu.item_value(3), "ACTIVE") == 0);

    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::VehicleBrands);
    REQUIRE(menu.selection() == player_car_brand_index(PlayerCarId::HarrowWorkman));
    menu.set_selection(player_car_brand_index(PlayerCarId::AlderWayfarer));
    menu.update(kBtnAccept);
    REQUIRE(std::strcmp(menu.title(), "ALDER") == 0);
    REQUIRE(menu.item_count() == 3);
    REQUIRE(std::strcmp(menu.item_label(0), "PIP") == 0);
    REQUIRE(menu.update(kBtnAccept).player_car == PlayerCarId::AlderPip);
    menu.set_selection(1);
    REQUIRE(std::strcmp(menu.item_label(1), "RIDGE") == 0);
    REQUIRE(menu.update(kBtnAccept).player_car == PlayerCarId::AlderRidge);
    menu.set_selection(2);
    REQUIRE(std::strcmp(menu.item_label(2), "WAYFARER") == 0);
    const DevMenuAction choose_wagon = menu.update(kBtnAccept);
    REQUIRE(choose_wagon.kind == DevMenuActionKind::SetPlayerCar);
    REQUIRE(choose_wagon.player_car == PlayerCarId::AlderWayfarer);
    REQUIRE(menu.player_car() == PlayerCarId::AlderWayfarer);
    REQUIRE(std::strcmp(menu.item_value(2), "ACTIVE") == 0);
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::VehicleBrands);
    REQUIRE(menu.selection() == player_car_brand_index(PlayerCarId::AlderWayfarer));
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::Vehicle);
    REQUIRE(menu.selection() == 0);

    menu.set_selection(2);
    const DevMenuAction copy_position = menu.update(kBtnAccept);
    REQUIRE(copy_position.kind == DevMenuActionKind::CopyPlayerPosition);

    menu.set_wanted_level(2);
    REQUIRE(std::strcmp(menu.item_value(3), "2 STARS") == 0);
    menu.set_selection(3);
    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::None);
    REQUIRE(menu.page() == DevMenuPage::Wanted);
    REQUIRE(menu.selection() == 2);
    REQUIRE(std::strcmp(menu.item_value(2), "ACTIVE") == 0);
    menu.set_selection(5);
    const DevMenuAction wanted = menu.update(kBtnAccept);
    REQUIRE(wanted.kind == DevMenuActionKind::SetWantedLevel);
    REQUIRE(wanted.wanted_level == 5);
    REQUIRE(menu.wanted_level() == 5);
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::Vehicle);
    REQUIRE(menu.selection() == 3);

    menu.set_selection(1);
    const DevMenuAction repair = menu.update(kBtnAccept);
    REQUIRE(repair.kind == DevMenuActionKind::RepairVehicle);

    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::Root);
    REQUIRE(menu.selection() == 2);

    menu.update(kBtnMenuDown);
    REQUIRE(menu.selection() == 3);
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::WeatherTime);
    // Three rows since snow accumulation joined weather and time on this page.
    REQUIRE(menu.item_count() == 3);
    REQUIRE(std::strcmp(menu.item_value(0), "DYNAMIC") == 0);
    REQUIRE(std::strcmp(menu.item_value(1), "LIVE") == 0);
    REQUIRE(std::strcmp(menu.item_label(2), "SNOW ACCUMULATION  >") == 0);
    REQUIRE(std::strcmp(menu.item_value(2), "AUTO (WEATHER)") == 0);

    // Snow depth had no UI coverage at all, so walk the whole row: open it,
    // pick a real preset, confirm the action carries that exact depth, and
    // confirm Back lands on the row it came from rather than the page top.
    menu.set_selection(2);
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::SnowDepth);
    REQUIRE(std::strcmp(menu.title(), "SNOW ACCUMULATION") == 0);
    REQUIRE(menu.item_count() == static_cast<int>(kDevSnowDepthValues.size()));
    REQUIRE(std::strcmp(menu.item_value(0), "ACTIVE") == 0);
    const int deep = static_cast<int>(kDevSnowDepthValues.size()) - 1;
    menu.set_selection(deep);
    const DevMenuAction snow = menu.update(kBtnAccept);
    REQUIRE(snow.kind == DevMenuActionKind::SetSnowDepth);
    REQUIRE_NEAR(snow.snow_depth_m, kDevSnowDepthValues[
        static_cast<std::size_t>(deep)], 1e-6f);
    REQUIRE(std::strcmp(menu.item_value(deep), "ACTIVE") == 0);
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::WeatherTime);
    REQUIRE(menu.selection() == 2);
    REQUIRE(std::strcmp(menu.item_value(2), kDevSnowDepthLabels[
        static_cast<std::size_t>(deep)]) == 0);

    menu.set_selection(0);

    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::Weather);
    REQUIRE(menu.selection() == static_cast<int>(DevWeatherPreset::Dynamic));
    REQUIRE(menu.item_count() == static_cast<int>(DevWeatherPreset::kCount));
    menu.set_selection(static_cast<int>(DevWeatherPreset::Tornado));
    const DevMenuAction weather = menu.update(kBtnAccept);
    REQUIRE(weather.kind == DevMenuActionKind::SetWeather);
    REQUIRE(weather.weather == DevWeatherPreset::Tornado);
    REQUIRE(std::strcmp(menu.item_value(menu.selection()), "ACTIVE") == 0);
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::WeatherTime);
    REQUIRE(menu.selection() == 0);

    menu.update(kBtnMenuDown);
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::Time);
    menu.set_selection(static_cast<int>(DevTimePreset::Noon));
    const DevMenuAction time = menu.update(kBtnAccept);
    REQUIRE(time.kind == DevMenuActionKind::SetTime);
    REQUIRE(time.time == DevTimePreset::Noon);
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::WeatherTime);
    REQUIRE(menu.selection() == 1);
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::Root);
    REQUIRE(menu.selection() == 3);

    menu.update(kBtnMenuDown);
    REQUIRE(menu.selection() == 4);
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::Camera);
    REQUIRE(std::strcmp(menu.item_value(0), "CHASE") == 0);
    REQUIRE(std::strcmp(menu.item_value(1), "ON") == 0);
    const DevMenuAction camera_mode = menu.update(kBtnAccept);
    REQUIRE(camera_mode.kind == DevMenuActionKind::SetCameraMode);
    REQUIRE(camera_mode.camera_mode == 2);
    menu.update(kBtnMenuDown);
    const DevMenuAction camera_recenter = menu.update(kBtnAccept);
    REQUIRE(camera_recenter.kind ==
            DevMenuActionKind::SetCameraAutoRecenter);
    REQUIRE(!camera_recenter.camera_auto_recenter);
    REQUIRE(std::strcmp(menu.item_value(1), "OFF") == 0);
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::Root);
    REQUIRE(menu.selection() == 4);

    menu.update(kBtnMenuDown);
    REQUIRE(menu.selection() == 5);
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::SoundTesting);
    REQUIRE(menu.item_count() == 1);
    REQUIRE(menu.item_label(0)[0] != '\0');
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::SoundTestingCar);
    REQUIRE(menu.item_count() == static_cast<int>(kCarSoundUseCount));
    REQUIRE(menu.item_count() >= 5);

    menu.update(kBtnMenuDown);
    menu.update(kBtnMenuDown);
    REQUIRE(menu.selection() == static_cast<int>(CarSoundUse::Crash));
    menu.update(kBtnAccept);
    REQUIRE(menu.page() == DevMenuPage::SoundTestingCarOptions);
    REQUIRE(menu.item_count() >= 5);
    REQUIRE(menu.item_label(4)[0] != '\0');
    menu.set_selection(4);
    const DevMenuAction sound = menu.update(kBtnAccept);
    REQUIRE(sound.kind == DevMenuActionKind::AuditionCarSound);
    REQUIRE(sound.car_sound_use == CarSoundUse::Crash);
    REQUIRE(sound.sound_variant == 4);

    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::SoundTestingCar);
    REQUIRE(menu.selection() == static_cast<int>(CarSoundUse::Crash));
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::SoundTesting);
    menu.update(kBtnBack);
    REQUIRE(menu.page() == DevMenuPage::Root);
    REQUIRE(menu.selection() == 5);
    menu.update(kBtnBack);
    REQUIRE(!menu.open());
    apricot_test::pass(
        "F1 menu navigates cars, wanted level, position, weather, time, camera and sound choices");
}

void developer_bug_report_keeps_message_and_capture_metadata() {
    DevMenu menu;
    menu.toggle();
    // REPORT BUG moved down one when FRAME LOGGING was added above it.
    menu.set_selection(7);
    REQUIRE(std::strcmp(menu.item_label(7), "REPORT BUG  [F2]") == 0);
    REQUIRE(menu.update(kBtnAccept).kind == DevMenuActionKind::ReportBug);

    BugReportUi report;
    report.begin({12.25f, 3.5f, -91.75f}, {0.0f, 0.0f, -1.0f});
    REQUIRE(report.open);
    REQUIRE(!bug_report_has_message(report));
    std::snprintf(report.message.data(), report.message.size(),
                  "Traffic car is stuck in the junction");
    REQUIRE(bug_report_has_message(report));
    const std::string markdown = bug_report_markdown(
        report, 42, 9001, "vehicle", "test-build");
    REQUIRE(markdown.find("Traffic car is stuck in the junction") !=
            std::string::npos);
    REQUIRE(markdown.find("12.250f, 3.500f, -91.750f") !=
            std::string::npos);
    REQUIRE(markdown.find("Simulation step: `9001`") != std::string::npos);
    REQUIRE(markdown.find("Screenshot: `screenshot.png`") !=
            std::string::npos);
    apricot_test::pass(
        "F2 bug reports preserve typed text and exact capture metadata");
}

void driving_mechanics_profiles_are_distinct_and_player_safe() {
    const VehicleTuning balanced =
        player_vehicle_tuning(DrivingMechanicsStyle::Balanced);
    const VehicleTuning arcade =
        player_vehicle_tuning(DrivingMechanicsStyle::Arcade);
    const VehicleTuning rally =
        player_vehicle_tuning(DrivingMechanicsStyle::Rally);
    const VehicleTuning heavy =
        player_vehicle_tuning(DrivingMechanicsStyle::Heavy);
    const VehicleTuning sport =
        player_vehicle_tuning(DrivingMechanicsStyle::Sport);
    const VehicleTuning muscle =
        player_vehicle_tuning(DrivingMechanicsStyle::Muscle);
    const VehicleTuning offroad =
        player_vehicle_tuning(DrivingMechanicsStyle::Offroad);
    const VehicleTuning drift =
        player_vehicle_tuning(DrivingMechanicsStyle::Drift);
    const VehicleTuning classic_gta =
        player_vehicle_tuning(DrivingMechanicsStyle::ClassicGta);

    REQUIRE(balanced.arcade_reverse && arcade.arcade_reverse &&
            rally.arcade_reverse && heavy.arcade_reverse &&
            sport.arcade_reverse && muscle.arcade_reverse &&
            offroad.arcade_reverse && drift.arcade_reverse &&
            classic_gta.arcade_reverse);
    REQUIRE(arcade.max_steer > balanced.max_steer);
    REQUIRE(arcade.steer_rate > balanced.steer_rate);
    REQUIRE(arcade.tyre_tail_grip > balanced.tyre_tail_grip);
    REQUIRE(rally.tyre_tail_grip < balanced.tyre_tail_grip);
    REQUIRE(rally.handbrake_grip_scale < balanced.handbrake_grip_scale);
    REQUIRE(rally.front_drive_bias < balanced.front_drive_bias);
    REQUIRE(heavy.mass_kg > balanced.mass_kg);
    REQUIRE(heavy.steer_rate < balanced.steer_rate);
    REQUIRE(sport.mass_kg < balanced.mass_kg);
    REQUIRE(sport.brake_torque > balanced.brake_torque);
    REQUIRE(muscle.engine_peak_torque > balanced.engine_peak_torque);
    REQUIRE(muscle.front_drive_bias < sport.front_drive_bias);
    REQUIRE(offroad.suspension_travel > rally.suspension_travel);
    REQUIRE(offroad.differential_coupling > balanced.differential_coupling);
    REQUIRE(drift.max_steer > arcade.max_steer);
    REQUIRE(drift.tyre_tail_grip < rally.tyre_tail_grip);
    REQUIRE(classic_gta.steer_rate > arcade.steer_rate);
    REQUIRE(classic_gta.front_drive_bias < sport.front_drive_bias);
    REQUIRE(classic_gta.handbrake_grip_scale < rally.handbrake_grip_scale);
    REQUIRE(classic_gta.tyre_tail_grip > drift.tyre_tail_grip);
    REQUIRE(classic_gta.spring_k < arcade.spring_k);
    REQUIRE(classic_gta.grounded_roll_rate_limit <
            balanced.grounded_roll_rate_limit);
    REQUIRE(classic_gta.service_brake_steer_scale <
            balanced.service_brake_steer_scale);

    // Live switching must not change the model or collision footprint around
    // the existing VehicleState. Those dimensions stay one shared car.
    REQUIRE(arcade.half_wheelbase == balanced.half_wheelbase);
    REQUIRE(rally.half_track == balanced.half_track);
    REQUIRE(heavy.wheel_radius == balanced.wheel_radius);
    REQUIRE(arcade.car_collision_half_width ==
            balanced.car_collision_half_width);
    REQUIRE(rally.car_collision_half_length ==
            balanced.car_collision_half_length);
    REQUIRE(sport.car_collision_half_width ==
            balanced.car_collision_half_width);
    REQUIRE(muscle.car_collision_half_length ==
            balanced.car_collision_half_length);
    REQUIRE(offroad.half_wheelbase == balanced.half_wheelbase);
    REQUIRE(drift.wheel_radius == balanced.wheel_radius);
    REQUIRE(classic_gta.car_collision_half_width ==
            balanced.car_collision_half_width);

    apricot_test::pass(
        "nine driving profiles differ without resizing the live player car");
}

}  // namespace

int main() {
    intro_is_aspect_safe_and_waits_for_both_time_and_assets();
    map_road_names_are_authored_and_numbered_in_order();
    ui_canvas_scales_with_height_and_keeps_input_aligned();
    american_speed_conversion_is_exact();
    debug_stats_overlay_starts_hidden_and_toggles();
    title_menu_wraps_and_starts();
    settings_menu_changes_player_preferences();
    map_returns_to_the_screen_that_opened_it();
    pause_actions_are_real_actions();
    map_projection_keeps_pinattys_compass();
    map_coastline_interpolates_and_clips_without_losing_area();
    map_layers_cycle_without_changing_the_return_screen();
    map_camera_zooms_pans_and_stays_on_the_island();
    map_camera_opens_at_a_200m_player_centered_view();
    map_wheel_zoom_keeps_the_visible_point_under_the_cursor();
    map_wheel_zoom_handles_overview_panel_edges_and_limits();
    developer_menu_navigates_and_returns_a_teleport();
    developer_bug_report_keeps_message_and_capture_metadata();
    developer_menu_teleports_to_ostend_shore();
    developer_menu_teleports_to_safe_florangia_palms();
    developer_menu_teleports_to_florangia_airport_access();
    developer_panel_scrolls_instead_of_squashing_rows();
    developer_menu_offers_teleport_only_when_a_map_waypoint_exists();
    driving_mechanics_profiles_are_distinct_and_player_safe();
    return apricot_test::done("ui_flow_tests");
}
