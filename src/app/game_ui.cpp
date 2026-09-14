#include "app/game_ui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string_view>
#include <tuple>

#include "city/airport.h"
#include "city/florangia_airport.h"
#include "city/neighborhood_shops.h"
#include "city/pawn_shop.h"
#include "city/gun_store.h"
#include "city/districts.h"
#include "city/landmarks.h"
#include "city/marina.h"
#include "city/miandi_bayfront.h"
#include "city/miandi_calle_noche.h"
#include "city/miandi_calle_ocho.h"
#include "city/miandi_context.h"
#include "city/miandi_layout.h"
#include "city/miandi_mariposa_motel.h"
#include "city/miandi_ocean_drive.h"
#include "city/miandi_port_sol.h"
#include "city/miandi_prism_works.h"
#include "city/miandi_sunwave_hotel.h"
#include "city/neighborhood_towers.h"
#include "city/pinatty_infill.h"
#include "city/construction_site.h"
#include "city/construction_neighbor_materials.h"
#include "city/construction_neighbor_equipment.h"
#include "city/construction_street_detail.h"
#include "city/construction_expansion.h"
#include "city/hospital_campus.h"
#include "city/hospital_north_parking.h"
#include "city/hospital_overhaul_massing.h"
#include "city/emergency_stations.h"
#include "city/east_arm_plaza.h"
#include "city/neighborhood_bar.h"
#include "city/loom_cultural.h"
#include "city/burgerpiz.h"
#include "city/church_of_waffles.h"
#include "city/north_pinatty_gas_station.h"
#include "city/luxury_neighborhood.h"
#include "city/residential_neighborhood.h"
#include "city/tidewater_farm.h"
#include "city/roads.h"
#include "city/start_area.h"
#include "city/states.h"
#include "city/tacomaco.h"
#include "core/units.h"
#include "gfx/hud.h"
#include "game/intro_layout.h"
#include "terrain/heightmap.h"

namespace apricot {
namespace {

constexpr glm::vec4 kInk{0.94f, 0.91f, 0.81f, 1.0f};
constexpr glm::vec4 kMuted{0.63f, 0.66f, 0.64f, 1.0f};
constexpr glm::vec4 kAmber{0.96f, 0.55f, 0.12f, 1.0f};
constexpr glm::vec4 kPanel{0.025f, 0.035f, 0.045f, 0.92f};
constexpr glm::vec4 kMapLabelPanel{0.012f, 0.020f, 0.025f, 0.86f};
constexpr glm::vec4 kMapLabelEdge{0.86f, 0.82f, 0.70f, 0.34f};

void draw_star(Hud& hud, glm::vec2 centre, float radius, glm::vec4 color) {
    std::array<glm::vec2, 10> points{};
    constexpr float kPi = 3.14159265358979323846f;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const float angle = -kPi * 0.5f + static_cast<float>(i) * kPi / 5.0f;
        const float r = (i & 1u) == 0u ? radius : radius * 0.43f;
        points[i] = centre + glm::vec2{std::cos(angle), std::sin(angle)} * r;
    }
    for (std::size_t i = 0; i < points.size(); ++i)
        hud.triangle(centre, points[i], points[(i + 1u) % points.size()], color);
}

void draw_wanted_stars(Hud& hud, int wanted_level, bool flash, int64_t step,
                       float right, float top) {
    if (wanted_level <= 0) return;
    constexpr float kSpacing = 26.0f;
    // 4 Hz on the SIM clock: 15 steps lit, 15 unlit at 120 Hz.
    const bool lit = !flash || ((step / 15) & 1) == 0;
    for (int i = 0; i < 5; ++i) {
        const glm::vec2 centre{right - (4 - i) * kSpacing - 11.0f, top + 11.0f};
        draw_star(hud, centre + glm::vec2{2.0f, 3.0f}, 12.0f,
                  {0.0f, 0.0f, 0.0f, 0.52f});
        draw_star(hud, centre, 11.0f,
                  i < wanted_level && lit ? glm::vec4{1.0f, 0.68f, 0.12f, 1.0f}
                                          : glm::vec4{0.20f, 0.22f, 0.22f, 0.92f});
    }
}

struct MenuBox {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float row_h = 0.0f;
};

MenuBox menu_box(UiScreen screen, glm::vec2 vp) {
    if (screen == UiScreen::Title) {
        const auto layout = IntroLayout::from_canvas(vp);
        return {layout.menu_position.x, layout.menu_position.y,
                layout.menu_width, layout.row_height};
    }
    const float width = std::min(500.0f, vp.x - 80.0f);
    const float top = screen == UiScreen::Title ? vp.y * 0.49f : vp.y * 0.31f;
    return {(vp.x - width) * 0.5f, top, width, 58.0f};
}

const char* menu_label(const UiFlow& flow, int index) {
    const UiScreen screen = flow.screen();
    static constexpr const char* kTitle[] = {
        "CONTINUE", "NEW GAME", "CITY MAP", "SETTINGS", "QUIT",
    };
    static constexpr const char* kPause[] = {
        "RESUME", "SAVE GAME", "LOAD GAME", "CITY MAP", "RESET VEHICLE",
        "SETTINGS", "QUIT TO TITLE",
    };
    if (screen == UiScreen::Title && index >= 0 && index < flow.item_count()) {
        return kTitle[index + (flow.save_available() ? 0 : 1)];
    }
    if (screen == UiScreen::Pause && index >= 0 && index < 6) {
        return kPause[index];
    }
    return "";
}

void draw_menu_rows(Hud& hud, const UiFlow& flow, glm::vec2 vp) {
    const MenuBox box = menu_box(flow.screen(), vp);
    for (int i = 0; i < flow.item_count(); ++i) {
        const float y = box.top + static_cast<float>(i) * box.row_h;
        const bool selected = i == flow.selection();
        if (selected) {
            hud.rect({box.left, y}, {box.left + box.width, y + box.row_h - 6.0f},
                     {0.96f, 0.55f, 0.12f, 0.19f});
            hud.rect({box.left, y}, {box.left + 6.0f, y + box.row_h - 6.0f},
                     kAmber);
        }
        hud.text_centered(menu_label(flow, i), vp.x * 0.5f,
                           y + 13.0f, 25.0f, selected ? kInk : kMuted);
    }
}

void draw_dev_rows(Hud& hud, const DevMenu& menu, glm::vec2 vp) {
    const float width = std::min(500.0f, vp.x - 48.0f);
    const float left = vp.x - width - 24.0f;
    const DevMenuPanelLayout layout =
        dev_menu_panel_layout(menu.item_count(), vp.y);
    const float top = layout.top;
    const float row_h = layout.row_height;
    const float header_h = layout.header_height;
    const float body_h = layout.body_height;
    const float bottom = layout.bottom;

    hud.rect({left, top}, {left + width, bottom},
             {0.015f, 0.020f, 0.025f, 0.94f});
    hud.rect({left, top}, {left + width, top + 7.0f}, kAmber);
    hud.text("PROBABLE CAUSE", {left + 22.0f, top + 18.0f}, 16.0f, kMuted);
    hud.text(menu.title(), {left + 22.0f, top + 39.0f}, 27.0f, kInk);
    hud.line({left + 18.0f, top + header_h - 4.0f},
             {left + width - 18.0f, top + header_h - 4.0f}, 1.0f,
             {1.0f, 1.0f, 1.0f, 0.18f});

    const int first = dev_menu_first_visible_row(
        menu.item_count(), menu.selection(), layout.visible_rows);
    const int last = std::min(menu.item_count(), first + layout.visible_rows);
    if (layout.scrolls) {
        // Where you are in the list, so a long page does not look truncated.
        char range[32];
        std::snprintf(range, sizeof(range), "%d-%d / %d", first + 1, last,
                      menu.item_count());
        hud.text_centered(range, left + width - 58.0f, top + 18.0f, 14.0f,
                          kMuted);
    }

    for (int i = first; i < last; ++i) {
        const float y =
            top + header_h + static_cast<float>(i - first) * row_h;
        const bool selected = i == menu.selection();
        if (selected) {
            hud.rect({left + 10.0f, y + 2.0f},
                     {left + width - 10.0f, y + row_h - 3.0f},
                     {0.96f, 0.55f, 0.12f, 0.22f});
            hud.rect({left + 10.0f, y + 2.0f},
                     {left + 16.0f, y + row_h - 3.0f}, kAmber);
        }
        hud.text(menu.item_label(i), {left + 28.0f, y + 11.0f}, 20.0f,
                 selected ? kInk : kMuted);
        const char* value = menu.item_value(i);
        if (value[0] != '\0') {
            hud.text_centered(value, left + width - 67.0f, y + 14.0f,
                              14.0f, selected ? kAmber : kMuted);
        }
    }

    const float footer_y = top + header_h + body_h;
    if (layout.scrolls) {
        // Track and thumb hard against the inside edge, so the list reads as
        // scrollable without spending a row on an indicator.
        const float track_x = left + width - 7.0f;
        const float body_top = top + header_h;
        hud.rect({track_x, body_top + 3.0f}, {track_x + 3.0f, footer_y - 3.0f},
                 {1.0f, 1.0f, 1.0f, 0.10f});
        const float span = std::max(1.0f, footer_y - 6.0f - (body_top + 3.0f));
        const float fraction =
            static_cast<float>(layout.visible_rows) /
            static_cast<float>(menu.item_count());
        const float thumb = std::max(18.0f, span * fraction);
        const float travel = span - thumb;
        const float progress =
            menu.item_count() > layout.visible_rows
                ? static_cast<float>(first) /
                      static_cast<float>(menu.item_count() - layout.visible_rows)
                : 0.0f;
        const float thumb_top = body_top + 3.0f + travel * progress;
        hud.rect({track_x, thumb_top}, {track_x + 3.0f, thumb_top + thumb},
                 kAmber);
    }
    hud.line({left + 18.0f, footer_y + 2.0f},
             {left + width - 18.0f, footer_y + 2.0f}, 1.0f,
             {1.0f, 1.0f, 1.0f, 0.18f});
    hud.text("UP/DOWN  SELECT    ENTER  CHOOSE", {left + 20.0f, footer_y + 13.0f},
             14.0f, kMuted);
    hud.text("BACKSPACE  BACK    F1  CLOSE", {left + 20.0f, footer_y + 30.0f},
             14.0f, kMuted);
}


glm::vec4 road_color(city::RoadClass cls) {
    switch (cls) {
        case city::RoadClass::Freeway:  return {0.86f, 0.69f, 0.39f, 1.0f};
        case city::RoadClass::Arterial: return {0.62f, 0.70f, 0.73f, 1.0f};
        case city::RoadClass::Street:   return {0.42f, 0.51f, 0.56f, 1.0f};
        case city::RoadClass::Alley:    return {0.28f, 0.37f, 0.40f, 1.0f};
        case city::RoadClass::Dirt:     return {0.48f, 0.44f, 0.33f, 1.0f};
    }
    return kMuted;
}

float road_thickness(city::RoadClass cls) {
    switch (cls) {
        case city::RoadClass::Freeway:  return 4.5f;
        case city::RoadClass::Arterial: return 3.2f;
        case city::RoadClass::Street:   return 1.8f;
        case city::RoadClass::Alley:    return 1.0f;
        case city::RoadClass::Dirt:     return 1.3f;
    }
    return 1.0f;
}

MapViewport map_viewport(glm::vec2 vp, const MapCamera& camera) {
    const float left = 24.0f;
    const float top = 104.0f;
    const float width = std::max(240.0f, vp.x - left * 2.0f);
    const float height = std::max(180.0f, vp.y - top - 116.0f);
    return camera.viewport(left, top, width, height);
}

glm::vec2 map_point(float x, float z, const MapViewport& viewport) {
    const MapPoint p = world_to_map(x, z, viewport);
    return {p.x, p.y};
}

void marker(Hud& hud, glm::vec2 p, glm::vec4 color) {
    hud.circle(p, 7.0f, {0.015f, 0.025f, 0.035f, 1.0f});
    hud.circle(p, 4.0f, color);
}

void map_label(Hud& hud, const char* text, glm::vec2 centre, float glyph_h,
               glm::vec4 color = kInk,
               glm::vec4 panel = kMapLabelPanel) {
    const float width = hud.measure_text(text, glyph_h);
    const float height = hud.text_line_height(glyph_h);
    const glm::vec2 half{width * 0.5f + 10.0f, height * 0.5f + 5.0f};
    hud.rect(centre - half, centre + half, panel);
    hud.outline(centre - half, centre + half, 1.0f, kMapLabelEdge);
    hud.text_centered(text, centre.x, centre.y - height * 0.5f, glyph_h,
                      color);
}

void map_halo_text(Hud& hud, const char* text, glm::vec2 centre, float size,
                   glm::vec4 color) {
    const float top = centre.y - hud.text_line_height(size) * 0.5f;
    for (const glm::vec2 offset : {glm::vec2{-1.5f, 0}, glm::vec2{1.5f, 0},
                                  glm::vec2{0, -1.5f}, glm::vec2{0, 1.5f}})
        hud.text_centered(text, centre.x + offset.x, top + offset.y, size,
                          {0.015f, 0.025f, 0.035f, 1.0f});
    hud.text_centered(text, centre.x, top, size, color);
}

void map_site_icon(Hud& hud, glm::vec2 p, int kind, glm::vec4 color,
                   float scale = 1.0f,
                   UiSymbol symbol = UiSymbol::Count) {
    const auto offset = [scale](glm::vec2 value) { return value * scale; };
    hud.circle(p + offset({0, 2}), 19.0f * scale, {0.0f, 0.0f, 0.0f, 0.45f});
    hud.circle(p, 16.0f * scale, color);
    hud.circle(p, 13.0f * scale, {0.025f, 0.045f, 0.060f, 1.0f});
    const glm::vec4 ink{0.94f, 0.97f, 1.0f, 1.0f};
    const auto line = [&](glm::vec2 a, glm::vec2 b) {
        hud.smooth_line(p + offset(a), p + offset(b), std::max(1.0f, 2.0f * scale), ink);
    };
    if (symbol != UiSymbol::Count) {
        hud.symbol(symbol, p - offset({9, 9}), 18.0f * scale, ink);
    } else if (kind == 0) {
        hud.symbol(UiSymbol::LocalGasStation, p - offset({9, 9}), 18.0f * scale, ink);
    } else if (kind == 1) {
        line({-8, 3}, {8, 3}); line({-6, 3}, {-4, -3});
        line({-4, -3}, {4, -3}); line({4, -3}, {6, 3});
        line({-8, -7}, {-5, -5}); line({0, -9}, {0, -6}); line({8, -7}, {5, -5});
        hud.circle(p + offset({-5, 6}), 2.0f * scale, ink);
        hud.circle(p + offset({5, 6}), 2.0f * scale, ink);
    } else if (kind == 2) {
        line({-8, -6}, {-8, 7}); line({8, -2}, {8, 7});
        line({-8, 3}, {8, 3});
        hud.rect(p + offset({-5, -2}), p + offset({7, 2}), ink);
    } else if (kind == 3) {
        hud.outline(p - offset({6, 9}), p + offset({6, 9}),
                    std::max(1.0f, 2.0f * scale), ink);
        for (float y : {-5.0f, 0.0f, 5.0f})
            for (float x : {-3.0f, 2.0f})
                hud.rect(p + offset({x, y}), p + offset({x + 2, y + 2}), ink);
    } else if (kind == 4) {
        line({-5, -8}, {-5, 8}); line({-8, -8}, {-8, -2});
        line({-2, -8}, {-2, -2}); line({-8, -2}, {-2, -2});
        line({5, -8}, {5, 8}); hud.circle(p + offset({5, -5}), 3.0f * scale, ink);
    } else if (kind == 5) {
        hud.triangle(p + offset({-9, -4}), p + offset({0, -10}),
                     p + offset({9, -4}), ink);
        for (float x : {-6.0f, 0.0f, 6.0f}) line({x, -2}, {x, 6});
        line({-9, 8}, {9, 8});
    } else if (kind == 9) {
        hud.circle(p + offset({0,-7}), 3.0f * scale, ink);
        hud.circle(p + offset({0,-7}), 1.3f * scale,{0.025f,0.045f,0.060f,1});
        line({0,-4},{0,9}); line({-5,-1},{5,-1});
        line({0,9},{-8,3}); line({0,9},{8,3});
        line({-8,3},{-8,0}); line({8,3},{8,0});
    } else {
        line({0, -10}, {0, 9}); line({0, -3}, {-9, 3});
        line({0, -3}, {9, 3}); line({0, 5}, {-5, 9}); line({0, 5}, {5, 9});
    }
}

bool inside(glm::vec2 p, glm::vec2 lo, glm::vec2 hi, float pad = 0.0f) {
    return p.x >= lo.x + pad && p.x <= hi.x - pad &&
           p.y >= lo.y + pad && p.y <= hi.y - pad;
}

int clip_code(glm::vec2 p, glm::vec2 lo, glm::vec2 hi) {
    int code = 0;
    if (p.x < lo.x) code |= 1;
    if (p.x > hi.x) code |= 2;
    if (p.y < lo.y) code |= 4;
    if (p.y > hi.y) code |= 8;
    return code;
}

bool clip_segment(glm::vec2& a, glm::vec2& b, glm::vec2 lo, glm::vec2 hi) {
    int ca = clip_code(a, lo, hi);
    int cb = clip_code(b, lo, hi);
    for (int guard = 0; guard < 8; ++guard) {
        if ((ca | cb) == 0) return true;
        if ((ca & cb) != 0) return false;
        const int c = ca != 0 ? ca : cb;
        glm::vec2 p{};
        if ((c & 4) != 0) {
            p = {a.x + (b.x - a.x) * (lo.y - a.y) / (b.y - a.y), lo.y};
        } else if ((c & 8) != 0) {
            p = {a.x + (b.x - a.x) * (hi.y - a.y) / (b.y - a.y), hi.y};
        } else if ((c & 2) != 0) {
            p = {hi.x, a.y + (b.y - a.y) * (hi.x - a.x) / (b.x - a.x)};
        } else {
            p = {lo.x, a.y + (b.y - a.y) * (lo.x - a.x) / (b.x - a.x)};
        }
        if (c == ca) {
            a = p;
            ca = clip_code(a, lo, hi);
        } else {
            b = p;
            cb = clip_code(b, lo, hi);
        }
    }
    return false;
}

glm::vec2 site_point(const city::StartSite& site, glm::vec2 local) {
    return {
        site.origin.x + site.cos_yaw * local.x + site.sin_yaw * local.y,
        site.origin.z - site.sin_yaw * local.x + site.cos_yaw * local.y,
    };
}

}  // namespace

void GameUi::build_map() {
    build_map_terrain();

    footprints_.clear();
    const auto add_rect = [this](const char* name, const city::StartSite& site,
                                 glm::vec2 centre, float width, float depth,
                                 bool lot, bool dock = false) {
        MapFootprint fp;
        fp.name = name;
        fp.lot = lot;
        fp.dock = dock;
        const glm::vec2 half{width * 0.5f, depth * 0.5f};
        fp.corners[0] = site_point(site, centre + glm::vec2{-half.x, -half.y});
        fp.corners[1] = site_point(site, centre + glm::vec2{ half.x, -half.y});
        fp.corners[2] = site_point(site, centre + glm::vec2{ half.x,  half.y});
        fp.corners[3] = site_point(site, centre + glm::vec2{-half.x,  half.y});
        footprints_.push_back(fp);
    };
    const auto add_site = [&add_rect](const city::StartSite& site,
                                      const city::BuildingPlan& plan) {
        add_rect(site.name, site, {site.lot_centre.x, site.lot_centre.z},
                 site.lot_width_m, site.lot_depth_m, true);
        for (std::size_t i = 0; i < plan.roof_count; ++i) {
            const city::BuildingRoof& roof = plan.roofs[i];
            add_rect(roof.name, site, {roof.centre.x, roof.centre.z},
                     roof.width_m, roof.depth_m, false);
        }
    };
    for(const auto& station:city::kImportedGasStations) {
    add_rect("6twelve forecourt",*station.site,{0,0},40,56,true);
    add_rect("6twelve store",*station.site,{-3.95f,-14.23f},14.31f,16.21f,false);
    add_rect("6twelve canopy",*station.site,{-2.79f,11.37f},21.68f,16.73f,false);
    add_rect("6twelve restrooms",*station.site,{14.45f,-10.32f},4.66f,7.80f,false);
    }
    add_site(city::kGasStationSite, city::kGasStationPlan);
    add_site(city::kCarWashSite, city::kCarWashPlan);
    add_site(city::kMotelSite, city::kMotelPlan);
    add_site(city::kApartmentSite, city::kApartmentPlan);
    add_site(city::kFastFoodSite, city::kFastFoodPlan);
    add_rect("TacoMaco lot",city::kTacomacoSite,{0,0},60,38,true);
    add_rect(city::kTacomacoSite.name,city::kTacomacoSite,{-1,-5},33,22,false);
    add_site(city::kBankSite, city::kBankPlan);
    add_site(city::kAutoRepairSite, city::kAutoRepairPlan);
    add_site(city::kLaundromatSite, city::kLaundromatPlan);
    add_site(city::kPawnShopSite, city::kPawnShopPlan);
    add_rect("Freaky Franks lot",city::kFreakyFranksSite,{0,0},60,38,true);
    add_rect(city::kFreakyFranksSite.name,city::kFreakyFranksSite,{-1,-5},33,22,false);
    add_rect("BurgerPiz lot",city::kBurgerPizSite,{0,0},60,38,true);
    add_rect(city::kBurgerPizSite.name,city::kBurgerPizSite,{-1,-5},33,22,false);
    add_rect("Church of Waffles lot",city::kChurchOfWafflesSite,{0,0},60,38,true);
    add_rect(city::kChurchOfWafflesSite.name,city::kChurchOfWafflesSite,{0,-2},32,12.1f,false);
    add_site(city::kLoomMuseumSite,city::kLoomMuseumPlan);
    add_rect(city::kLoomParkSite.name,city::kLoomParkSite,{0,0},city::kLoomParkSite.lot_width_m,city::kLoomParkSite.lot_depth_m,true);
    add_rect("Sable Garden gazebo",city::kLoomParkSite,{0,1},9.2f,8.2f,false);

    add_site(city::kGunStoreSite, city::kGunStorePlan);
    add_rect(city::kEastArmPlazaSite.name, city::kEastArmPlazaSite, {0.0f, 0.0f},
             city::kEastArmPlazaSite.lot_width_m,
             city::kEastArmPlazaSite.lot_depth_m, true);
    add_rect("East Arm indoor mall", city::kEastArmPlazaSite, {0.0f, -17.0f},
             136.0f, 106.0f, false);
    add_rect("East Arm cinema anchor", city::kEastArmPlazaSite, {28.0f, -50.0f},
             74.0f, 40.0f, false);
    add_rect("East Arm bookstore anchor", city::kEastArmPlazaSite, {-41.5f, -47.0f},
             47.0f, 37.0f, false);
    add_rect("East Arm restaurant", city::kEastArmPlazaSite, {42.0f, 18.0f},
             44.0f, 28.0f, false);
    add_rect(city::kNeighborhoodBarSite.name, city::kNeighborhoodBarSite,
             {city::kNeighborhoodBarSite.lot_centre.x,
              city::kNeighborhoodBarSite.lot_centre.z},
             city::kNeighborhoodBarSite.lot_width_m,
             city::kNeighborhoodBarSite.lot_depth_m, true);
    add_rect("The Bent Elbow building", city::kNeighborhoodBarSite,
             {0.0f, 2.0f}, 24.5f, 20.5f, false);
    for (const auto* station :
         {&city::kFireStationSite, &city::kPoliceStationSite}) {
        add_rect(station->name, *station,
                 {station->lot_centre.x, station->lot_centre.z},
                 station->lot_width_m, station->lot_depth_m, true);
        add_rect("Halloway emergency-station building", *station,
                 {0.0f, -5.0f}, 54.6f, 26.6f, false);
    }
    for (const auto& house : city::kResidentialHouses) {
        add_rect(house.site.name, house.site,
                 {house.site.lot_centre.x, house.site.lot_centre.z},
                 house.site.lot_width_m, house.site.lot_depth_m, true);
        add_rect("Sycamore Loop house", house.site, {0.0f, 0.0f},
                 house.width_m, 12.0f, false);
    }
    for (const auto& estate : city::kLuxuryEstates) {
        const auto& site=estate.site;
        add_rect(site.name,site,{site.lot_centre.x,site.lot_centre.z},
                 site.lot_width_m,site.lot_depth_m,true);
        add_rect("Westmere estate house",site,{-6.0f,-3.0f},31.0f,18.0f,false);
        add_rect("Westmere estate garage",site,{17.0f,-3.0f},13.0f,18.0f,false);
        if(estate.pool)
            add_rect("Westmere private pool",site,{-5.0f,-23.0f},14.0f,8.5f,false);
    }
    add_rect(city::kWestmereCommonSite.name,city::kWestmereCommonSite,{0,0},
             city::kWestmereCommonSite.lot_width_m,
             city::kWestmereCommonSite.lot_depth_m,true);
    add_rect("Westmere community pool",city::kWestmereCommonSite,{-22,-3},31,12,false);
    add_rect("Westmere tennis court",city::kWestmereCommonSite,{31,17},44,22,false);
    add_rect("Westmere residents pavilion",city::kWestmereCommonSite,{25,-27},19.5f,15.5f,false);
    add_rect(city::kWestmereGateSite.name,city::kWestmereGateSite,{0,0},
             city::kWestmereGateSite.lot_width_m,
             city::kWestmereGateSite.lot_depth_m,true);
    add_rect("Westmere gatehouse",city::kWestmereGateSite,{13.5f,-5.5f},7,8,false);
    add_rect(city::kTidewaterFarmSite.name, city::kTidewaterFarmSite,
             {0.0f, 0.0f}, city::kTidewaterFarmSite.lot_width_m,
             city::kTidewaterFarmSite.lot_depth_m, true);
    add_rect("Tidewater Farm barn", city::kTidewaterFarmSite,
             {-30.0f, -35.0f}, 34.0f, 24.0f, false);
    add_rect("Tidewater Farm house", city::kTidewaterFarmSite,
             {32.0f, -38.0f}, 18.0f, 14.0f, false);
    add_rect("Tidewater Farm west crop field", city::kTidewaterFarmSite,
             {-30.0f, 33.0f}, 48.0f, 64.0f, false);
    add_rect("Tidewater Farm east crop field", city::kTidewaterFarmSite,
             {30.0f, 33.0f}, 48.0f, 64.0f, false);
    for (const auto& tower : city::kNeighborhoodTowers) {
        if (city::hospital_campus_replaces(tower.site)) continue;
        add_rect(tower.site.name, tower.site,
                 {tower.site.lot_centre.x, tower.site.lot_centre.z},
                 tower.site.lot_width_m, tower.site.lot_depth_m, true);
    }
    for (const auto& parcel : city::kPinattyInfillParcels) {
        add_rect(parcel.site.name, parcel.site,
                 {parcel.site.lot_centre.x, parcel.site.lot_centre.z},
                 parcel.site.lot_width_m, parcel.site.lot_depth_m, true);
        add_rect(parcel.site.name, parcel.site,
                 {0.0f, parcel.building_centre_z_m},
                 parcel.building_width_m, parcel.building_depth_m, false);
    }
    add_rect(city::kConstructionSite.site.name, city::kConstructionSite.site,
             {city::kConstructionSite.site.lot_centre.x,
              city::kConstructionSite.site.lot_centre.z},
             city::kConstructionSite.site.lot_width_m,
             city::kConstructionSite.site.lot_depth_m, true);
    add_rect(city::kConstructionNeighborMaterialsSite.name,
             city::kConstructionNeighborMaterialsSite,
             {city::kConstructionNeighborMaterialsSite.lot_centre.x,
              city::kConstructionNeighborMaterialsSite.lot_centre.z},
             city::kConstructionNeighborMaterialsSite.lot_width_m,
             city::kConstructionNeighborMaterialsSite.lot_depth_m, true);
    add_rect(city::kConstructionNeighborEquipment.site.name,
             city::kConstructionNeighborEquipment.site,
             {city::kConstructionNeighborEquipment.site.lot_centre.x,
              city::kConstructionNeighborEquipment.site.lot_centre.z},
             city::kConstructionNeighborEquipment.site.lot_width_m,
             city::kConstructionNeighborEquipment.site.lot_depth_m, true);
    add_rect(city::kConstructionStreetDetailSite.name,
             city::kConstructionStreetDetailSite,
             {city::kConstructionStreetDetailSite.lot_centre.x,
              city::kConstructionStreetDetailSite.lot_centre.z},
             city::kConstructionStreetDetailSite.lot_width_m,
             city::kConstructionStreetDetailSite.lot_depth_m, false);
    for (const auto& expansion : city::kAdditionalConstructionSites) {
        add_rect(expansion.site.name, expansion.site,
                 {expansion.site.lot_centre.x, expansion.site.lot_centre.z},
                 expansion.site.lot_width_m, expansion.site.lot_depth_m, true);
    }
    for (const auto& twin : city::kTwinSkyscraperBlockSites) {
        add_rect(twin.name, twin, {twin.lot_centre.x, twin.lot_centre.z},
                 twin.lot_width_m, twin.lot_depth_m, true);
    }
    const auto add_hospital_bar = [&](const char* name,
                                      const city::HospitalOverhaulRect& rect) {
        add_rect(name, city::kHospitalSite,
                 {rect.centre_x(), rect.centre_z()}, rect.width(),
                 rect.depth(), true);
    };
    add_hospital_bar("Pinatty Regional Hospital public and diagnostic wing",
                     city::kHospitalOverhaulNorthBar);
    add_hospital_bar("Pinatty Regional Hospital inpatient wing",
                     city::kHospitalOverhaulWestBar);
    add_hospital_bar("Pinatty Regional Hospital surgery and emergency wing",
                     city::kHospitalOverhaulEastBar);
    add_hospital_bar("Pinatty Regional Hospital support wing",
                     city::kHospitalOverhaulSouthBar);
    add_hospital_bar("Pinatty Regional Hospital clinical spine",
                     city::kHospitalOverhaulClinicalSpine);
    add_rect("Pinatty Regional Hospital parking garage", city::kHospitalSite,
             {city::kHospitalGarageCentre.x,
              city::kHospitalGarageCentre.z}, city::kHospitalGarageWidthM,
             city::kHospitalBlockDepthM, true);
    add_rect("Pinatty Regional Hospital north visitor parking",
             city::kHospitalNorthParkingSite,
             {city::kHospitalNorthParkingCentre.x,
              city::kHospitalNorthParkingCentre.z},
             city::kHospitalNorthParkingWidthM,
             city::kHospitalNorthParkingDepthM, true);
    for(const auto& fp:city::marina_map_footprints())
        add_rect(fp.name,city::kMarlinDockSite,{fp.centre.x,fp.centre.z},
            fp.width_m,fp.depth_m,fp.lot,fp.dock);
    add_site(city::kAirportSite, city::kAirportPlan);
    add_rect("Runway 09-27", city::kAirportSite, {0.0f, -94.0f},
             900.0f, 48.0f, false);
    add_rect("Passenger apron", city::kAirportSite, {-90.0f, 17.0f},
             500.0f, 92.0f, false);
    add_rect("Taxiway Alpha", city::kAirportSite, {315.0f, -40.0f},
             24.0f, 72.0f, false);
    add_rect("Taxiway Bravo", city::kAirportSite, {-300.0f, -40.0f},
             24.0f, 72.0f, false);
    add_rect("Pinatty International west terminal", city::kAirportSite,
             {-111.5f, 128.0f}, 73.0f, 36.0f, false);
    add_rect("Pinatty International main terminal", city::kAirportSite,
             {-35.0f, 128.0f}, 80.0f, 40.0f, false);
    add_rect("Pinatty International east terminal", city::kAirportSite,
             {44.0f, 128.0f}, 78.0f, 36.0f, false);
    add_rect("Airport hangar", city::kAirportSite, {315.0f, 112.0f},
             108.0f, 68.0f, false);
    add_rect("Camber control tower", city::kAirportSite, {190.0f, 42.0f},
             19.0f, 19.0f, false);
    add_rect("Airport fire station", city::kAirportSite, {210.0f, 130.0f},
             58.0f, 34.0f, false);
    add_rect("Camber Gateway Hotel main wing", city::kAirportSite,
             {-350.0f, 123.0f}, 110.0f, 24.0f, false);
    add_rect("Camber Gateway Hotel west wing", city::kAirportSite,
             {-393.0f, 164.0f}, 24.0f, 82.0f, false);
    add_rect("Camber Gateway Hotel east wing", city::kAirportSite,
             {-307.0f, 164.0f}, 24.0f, 82.0f, false);
    add_rect("Pinatty Rental Centre", city::kAirportSite,
             {225.0f, 190.0f}, 62.0f, 24.0f, false);
    add_rect("Pinatty Rental Parking", city::kAirportSite,
             {298.0f, 216.0f}, 48.0f, 48.0f, false);
    add_rect("Camber Air Cargo", city::kAirportSite,
             {370.0f, 178.0f}, 58.0f, 28.0f, false);
    add_rect("Airport Security Gate", city::kAirportSite,
             {-455.0f, 24.0f}, 12.0f, 8.0f, false);
    add_rect("Pinatty airport arrival pylon", city::kAirportSite,
             {430.0f, 242.0f}, 10.0f, 3.0f, false);
    add_site(city::kFlorangiaAirportSite, city::kFlorangiaAirportPlan);
    add_rect("Florangia runway 08-26", city::kFlorangiaAirportSite,
             {0.0f, -105.0f}, 1000.0f, 48.0f, false);
    add_rect("Florangia passenger apron", city::kFlorangiaAirportSite,
             {-70.0f, 8.0f}, 360.0f, 92.0f, false);
    add_rect("Florangia Regional terminal", city::kFlorangiaAirportSite,
             {-22.0f, 92.0f}, 232.0f, 42.0f, false);
    add_rect("Florangia airport parking", city::kFlorangiaAirportSite,
             {-22.0f, 181.0f}, 250.0f, 44.0f, false);
    for (std::size_t i = 0; i < city::kMiandiContextBlockCount; ++i) {
        if (city::miandi_context_block_is_replaced(i)) continue;
        const city::Vec2 centre = city::kMiandiContextBlockCenters[i];
        add_rect("Miandi context block", city::kMiandiContextSite,
                 {centre.x, centre.z}, 150.0f, 140.0f, true);
    }
    const auto miandi_context_parts = city::bake_miandi_context();
    for (const city::BuildingPiece& part : miandi_context_parts) {
        if (!part.solid || part.bottom_m > 0.2f) continue;
        add_rect(part.name, city::kMiandiContextSite,
                 {part.centre.x, part.centre.z},
                 part.width_m, part.depth_m, false);
    }
    for (const city::StartPart& part : city::kMiandiBuildingParts) {
        if (!city::miandi_keeps_rough_part(part) || !part.solid ||
            part.bottom_m > 0.2f)
            continue;
        add_rect(part.name, city::kMiandiSite,
                 {part.centre.x, part.centre.z},
                 part.width_m, part.depth_m, false);
    }
    add_rect(city::kMiandiCalleOchoSite.name, city::kMiandiCalleOchoSite,
             {city::kMiandiCalleOchoSite.lot_centre.x,
              city::kMiandiCalleOchoSite.lot_centre.z},
             city::kMiandiCalleOchoSite.lot_width_m,
             city::kMiandiCalleOchoSite.lot_depth_m, true);
    for (const city::BuildingPlan* plan : {
             &city::kMiandiSolCafePlan, &city::kMiandiMercadoPlan,
             &city::kMiandiCigarWorkshopPlan,
             &city::kMiandiCornerMusicBarPlan}) {
        for (std::size_t i = 0; i < plan->roof_count; ++i) {
            const auto& roof = plan->roofs[i];
            add_rect(roof.name, city::kMiandiCalleOchoSite,
                     {roof.centre.x, roof.centre.z},
                     roof.width_m, roof.depth_m, false);
        }
    }
    add_site(city::kMiandiBayfrontSite, city::kMiandiBayfrontPlan);
    add_rect("Crown Residences tower", city::kMiandiBayfrontSite,
             {0.0f, 0.0f}, 62.0f, 38.0f, false);
    add_rect(city::kMiandiCalleNocheSite.name, city::kMiandiCalleNocheSite,
             {0.0f, 0.0f}, city::kMiandiCalleNocheSite.lot_width_m,
             city::kMiandiCalleNocheSite.lot_depth_m, true);
    for (const city::BuildingPlan* plan : {
             &city::kMiandiSolSocialClubPlan,
             &city::kMiandiPalmaDanceHallPlan,
             &city::kMiandiLateNightCafecitoPlan}) {
        for (std::size_t i = 0; i < plan->roof_count; ++i) {
            const auto& roof = plan->roofs[i];
            add_rect(roof.name, city::kMiandiCalleNocheSite,
                     {roof.centre.x, roof.centre.z}, roof.width_m,
                     roof.depth_m, false);
        }
    }
    add_site(city::kMiandiPrismWorksSite, city::kMiandiPrismWorksPlan);
    add_site(city::kMiandiMariposaMotelSite,
             city::kMiandiMariposaMotelPlan);
    add_site(city::kMiandiSunwaveHotelSite,
             city::kMiandiSunwaveHotelPlan);
    add_rect(city::kMiandiOceanDriveSite.name, city::kMiandiOceanDriveSite,
             {city::kMiandiOceanDriveSite.lot_centre.x,
              city::kMiandiOceanDriveSite.lot_centre.z},
             city::kMiandiOceanDriveSite.lot_width_m,
             city::kMiandiOceanDriveSite.lot_depth_m, true);
    for (const city::BuildingPlan* plan : {
             &city::kCoralCrownHotelPlan, &city::kBlueHeronHotelPlan}) {
        for (std::size_t i = 0; i < plan->roof_count; ++i) {
            const auto& roof = plan->roofs[i];
            add_rect(roof.name, city::kMiandiOceanDriveSite,
                     {roof.centre.x, roof.centre.z},
                     roof.width_m, roof.depth_m, false);
        }
    }
    add_rect(city::kMiandiNorthPromenadeSite.name,
             city::kMiandiNorthPromenadeSite,
             {city::kMiandiNorthPromenadeSite.lot_centre.x,
              city::kMiandiNorthPromenadeSite.lot_centre.z},
             city::kMiandiNorthPromenadeSite.lot_width_m,
             city::kMiandiNorthPromenadeSite.lot_depth_m, true);
    add_site(city::kMiandiPortSolSite, city::kMiandiPortSolPlan);
    map_camera_.reset();
}

void GameUi::build_map_terrain() {
    land_patches_.clear();
    contours_.clear();
    constexpr int grid = kMapTerrainGrid;
    constexpr int stride = grid + 1;
    const float cell = MapCamera::kFullSpanM / static_cast<float>(grid);
    const float origin = -city::kWorldHalfMetres;
    std::vector<float> heights(static_cast<std::size_t>(stride * stride));
    for (int z = 0; z <= grid; ++z) {
        for (int x = 0; x <= grid; ++x) {
            heights[static_cast<std::size_t>(z * stride + x)] =
                height_at(city::kMapSeed, origin + static_cast<float>(x) * cell,
                          origin + static_cast<float>(z) * cell);
        }
    }
    // Fully dry cells become long strips. Only the shore needs small polygons;
    // neither the CPU draw cost nor visible detail follows a magnified raster.
    for (int z = 0; z < grid; ++z) {
        int run = -1;
        const float z0 = origin + static_cast<float>(z) * cell;
        const auto finish_run = [&](int end) {
            if (run < 0) return;
            const float x0 = origin + static_cast<float>(run) * cell;
            const float x1 = origin + static_cast<float>(end) * cell;
            MapPolygon patch;
            patch.count = 4;
            patch.points[0] = {x0, z0};
            patch.points[1] = {x1, z0};
            patch.points[2] = {x1, z0 + cell};
            patch.points[3] = {x0, z0 + cell};
            land_patches_.push_back(patch);
            run = -1;
        };
        for (int x = 0; x < grid; ++x) {
            const auto at = [&](int dx, int dz) {
                return heights[static_cast<std::size_t>((z + dz) * stride + x + dx)];
            };
            const std::array<float, 4> h{at(0, 0), at(1, 0), at(1, 1), at(0, 1)};
            const auto range = std::minmax_element(h.begin(), h.end());
            const float low = *range.first, high = *range.second;
            const bool dry = low > kSeaLevelMetres;
            if (dry) { if (run < 0) run = x; }
            else finish_run(x);
            if (high <= kSeaLevelMetres) continue;
            const float x0 = origin + static_cast<float>(x) * cell;
            const std::array<glm::vec2, 4> p{{
                {x0, z0}, {x0 + cell, z0},
                {x0 + cell, z0 + cell}, {x0, z0 + cell}}};
            for (int t = 0; t < 2; ++t) {
                const std::size_t b = static_cast<std::size_t>(t + 1);
                const std::array<glm::vec2, 3> tri{p[0], p[b], p[b + 1]};
                const std::array<float, 3> th{h[0], h[b], h[b + 1]};
                if (!dry) {
                    const MapSlice slice = slice_map_triangle(tri, th, kSeaLevelMetres);
                    if (slice.land.count >= 3) land_patches_.push_back(slice.land);
                    if (slice.crossing_count == 2)
                        contours_.push_back({slice.crossings[0], slice.crossings[1],
                                             kSeaLevelMetres});
                }
                // Topographic lines describe real elevation, not decorative noise.
                for (float level = 20.0f; level <= high; level += 20.0f) {
                    if (level <= low) continue;
                    const MapSlice slice = slice_map_triangle(tri, th, level);
                    if (slice.crossing_count == 2)
                        contours_.push_back({slice.crossings[0], slice.crossings[1], level});
                }
            }
        }
        finish_run(grid);
    }

    // Join the sampled segments before simplifying them. A six-metre sample
    // edge is smaller than a pixel in the overview; submitting each as its own
    // feathered stroke wastes most of the map's vertex budget.
    using Key = std::tuple<long, long, int>;
    const auto key = [](glm::vec2 p, float elevation) {
        return Key{std::lround(p.x * 64.0f), std::lround(p.y * 64.0f),
                   static_cast<int>(elevation)};
    };
    std::map<Key, std::vector<std::size_t>> adjacency;
    for (std::size_t i = 0; i < contours_.size(); ++i) {
        adjacency[key(contours_[i].a, contours_[i].elevation)].push_back(i);
        adjacency[key(contours_[i].b, contours_[i].elevation)].push_back(i);
    }
    std::vector<bool> visited(contours_.size(), false);
    std::vector<MapContour> detail;
    overview_contours_.clear();
    const auto simplify = [](const std::vector<glm::vec2>& path, float tolerance,
                              float elevation, std::vector<MapContour>& output) {
        std::vector<bool> keep(path.size(), false);
        keep.front() = keep.back() = true;
        std::vector<std::pair<std::size_t, std::size_t>> pending{{0, path.size() - 1}};
        while (!pending.empty()) {
            const auto [first, last] = pending.back();
            pending.pop_back();
            const glm::vec2 delta = path[last] - path[first];
            const float length2 = glm::dot(delta, delta);
            float worst = tolerance * tolerance;
            std::size_t split = first;
            for (std::size_t i = first + 1; i < last; ++i) {
                const float t = length2 > 1e-8f
                    ? glm::clamp(glm::dot(path[i] - path[first], delta) / length2, 0.0f, 1.0f) : 0;
                const glm::vec2 error = path[i] - (path[first] + delta * t);
                const float distance2 = glm::dot(error, error);
                if (distance2 > worst) { worst = distance2; split = i; }
            }
            if (split != first) {
                keep[split] = true;
                pending.emplace_back(first, split);
                pending.emplace_back(split, last);
            }
        }
        std::size_t previous = 0;
        for (std::size_t i = 1; i < path.size(); ++i) {
            if (!keep[i]) continue;
            output.push_back({path[previous], path[i], elevation});
            previous = i;
        }
    };
    for (std::size_t seed = 0; seed < contours_.size(); ++seed) {
        if (visited[seed]) continue;
        visited[seed] = true;
        const MapContour& initial = contours_[seed];
        const auto walk = [&](std::vector<glm::vec2>& path) {
            while (true) {
                const Key current = key(path.back(), initial.elevation);
                const auto& edges = adjacency.at(current);
                if (edges.size() != 2) break;
                const std::size_t next = !visited[edges[0]] ? edges[0] : edges[1];
                if (visited[next]) break;
                visited[next] = true;
                const MapContour& edge = contours_[next];
                path.push_back(key(edge.a, edge.elevation) == current ? edge.b : edge.a);
            }
        };
        std::vector<glm::vec2> path{initial.a, initial.b};
        walk(path);
        std::vector<glm::vec2> backward{initial.a};
        walk(backward);
        std::reverse(backward.begin(), backward.end());
        backward.pop_back();
        path.insert(path.begin(), backward.begin(), backward.end());
        simplify(path, 0.15f, initial.elevation, detail);
        simplify(path, 4.0f, initial.elevation, overview_contours_);
    }
    contours_ = std::move(detail);
}

void GameUi::update_map(glm::vec2 pan_axis, float zoom_steps,
                        glm::vec2 drag_pixels, bool recenter,
                        glm::vec3 player_position, float dt,
                        glm::vec2 viewport_px, glm::vec2 pointer_px,
                        float wheel_zoom_steps) {
    const MapViewport layout = map_viewport(viewport_px, map_camera_);
    map_camera_.set_viewport_size(layout.width, layout.height);
    const glm::vec2 focus{player_position.x, player_position.z};
    map_camera_.zoom(zoom_steps, focus.x, focus.y);
    map_camera_.zoom_at(wheel_zoom_steps, {pointer_px.x, pointer_px.y}, layout);
    map_camera_.pan(pan_axis.x, pan_axis.y, dt);
    if (recenter) map_camera_.recenter(focus.x, focus.y);

    const MapViewport mv = map_viewport(viewport_px, map_camera_);
    if (mv.width > 1.0f && (drag_pixels.x != 0.0f || drag_pixels.y != 0.0f)) {
        const float metres_per_pixel = 1.0f / map_pixels_per_metre(mv);
        map_camera_.pan_world(-drag_pixels.x * metres_per_pixel,
                              -drag_pixels.y * metres_per_pixel);
    }
    map_camera_.update(dt);
}

void GameUi::open_map_view(glm::vec3 player_position, glm::vec2 viewport_px) {
    const MapViewport layout = map_viewport(viewport_px, map_camera_);
    map_camera_.set_viewport_size(layout.width, layout.height);
    map_camera_.open_at(player_position.x, player_position.z);
}

int GameUi::hit_test(const UiFlow& flow, glm::vec2 pointer_px,
                     glm::vec2 viewport_px) const {
    if (flow.item_count() <= 0) return -1;
    if (flow.screen() == UiScreen::Settings) {
        const float nav_left = 72.0f;
        const float nav_top = 260.0f;
        const float nav_width = std::min(330.0f, viewport_px.x * 0.27f);
        const float row_h = 58.0f;
        if (flow.settings_page() == SettingsPage::Root) {
            if (inside(pointer_px, {nav_left - 18.0f, nav_top},
                       {nav_left + nav_width, nav_top + row_h * 5.0f}, 0.0f)) {
                const int row = static_cast<int>((pointer_px.y - nav_top) / row_h);
                return row >= 0 && row < 5 ? 100 + row : -1;
            }
            return -1;
        }
        if (inside(pointer_px, {nav_left - 18.0f, nav_top},
                   {nav_left + nav_width, nav_top + row_h * 5.0f}, 0.0f)) {
            const int row = static_cast<int>((pointer_px.y - nav_top) / row_h);
            return row >= 0 && row < 5 ? 100 + row : -1;
        }
        const float content_left = nav_left + nav_width + 84.0f;
        const float content_width = viewport_px.x - content_left - 72.0f;
        const float content_top = 260.0f + 58.0f;
        if (inside(pointer_px, {content_left, content_top},
                   {content_left + content_width,
                    content_top + row_h * flow.item_count()}, 0.0f)) {
            const int row = static_cast<int>((pointer_px.y - content_top) / row_h);
            return row >= 0 && row < flow.item_count() ? row : -1;
        }
        return -1;
    }
    const MenuBox box = menu_box(flow.screen(), viewport_px);
    if (pointer_px.x < box.left || pointer_px.x > box.left + box.width ||
        pointer_px.y < box.top) {
        return -1;
    }
    const int row = static_cast<int>((pointer_px.y - box.top) / box.row_h);
    if (row < 0 || row >= flow.item_count()) return -1;
    const float row_y = box.top + static_cast<float>(row) * box.row_h;
    if (pointer_px.y > row_y + box.row_h - 6.0f) return -1;
    return row;
}

void GameUi::draw_title(Hud& hud, const UiFlow& flow, glm::vec2 vp, float opacity) const {
    const auto box = menu_box(UiScreen::Title, vp);
    for (int i = 0; i < flow.item_count(); ++i) {
        const float y = box.top + static_cast<float>(i) * box.row_h;
        const bool selected = i == flow.selection();
        if (selected) {
            hud.rect({box.left, y + 27}, {box.left + 30, y + 32},
                     {0.86f, 0.20f, 0.14f, opacity});
        }
        const glm::vec3 ink = selected ? glm::vec3(kInk) : glm::vec3{0.68f, 0.68f, 0.64f};
        hud.text(menu_label(flow, i), {box.left + 52, y + 7},
                 34.0f, {ink, opacity});
    }
    hud.text("W/S / D-PAD   MOVE     ENTER / A   SELECT",
             {box.left, vp.y * 0.91f}, 19.0f, {0.68f, 0.68f, 0.64f, opacity});
}

void GameUi::draw_pause(Hud& hud, const UiFlow& flow,
                        const GameUiSnapshot& snapshot, glm::vec2 vp) const {
    hud.rect({0.0f, 0.0f}, vp, {0.01f, 0.015f, 0.02f, 0.64f});
    const float panel_w = std::min(620.0f, vp.x - 60.0f);
    const glm::vec2 lo{(vp.x - panel_w) * 0.5f, 70.0f};
    const glm::vec2 hi{lo.x + panel_w, vp.y - 55.0f};
    hud.rect(lo, hi, kPanel);
    hud.outline(lo, hi, 2.0f, {0.96f, 0.55f, 0.12f, 0.55f});
    hud.text_centered("PAUSED", vp.x * 0.5f, 98.0f, 48.0f, kInk);

    draw_menu_rows(hud, flow, vp);

    char line[128];
    const city::DistrictId id =
        city::district_at(snapshot.player_position.x, snapshot.player_position.z);
    std::snprintf(line, sizeof(line), "%s   CAR %.0f%%   %.0f MPH",
                  city::district_name(id),
                  static_cast<double>(snapshot.vehicle_health),
                  static_cast<double>(snapshot.speed_mph));
    hud.text_centered(line, vp.x * 0.5f, vp.y - 108.0f, 16.0f, kMuted);
    hud.text_centered("ESC/B  RESUME     M/BACK  MAP",
                       vp.x * 0.5f, vp.y - 80.0f, 15.0f, kMuted);
}

void GameUi::draw_settings(Hud& hud, const UiFlow& flow, glm::vec2 vp) const {
    const glm::vec4 ink{0.94f, 0.91f, 0.81f, 1.0f};
    const glm::vec4 muted{0.52f, 0.61f, 0.63f, 1.0f};
    const glm::vec4 accent{0.96f, 0.55f, 0.12f, 1.0f};
    const glm::vec4 panel{0.012f, 0.020f, 0.028f, 0.96f};
    const float nav_left = 72.0f;
    const float nav_top = 260.0f;
    const float nav_width = std::min(330.0f, vp.x * 0.27f);
    const float row_h = 58.0f;
    const float content_left = nav_left + nav_width + 84.0f;
    const float content_width = vp.x - content_left - 72.0f;
    const float content_top = nav_top;

    hud.rect({0, 0}, vp, {0.008f, 0.012f, 0.018f, 0.94f});
    hud.rect({48.0f, 48.0f}, {vp.x - 48.0f, vp.y - 48.0f}, panel);
    hud.rect({48.0f, 48.0f}, {vp.x - 48.0f, 56.0f}, accent);
    hud.text("PROBABLE CAUSE", {72.0f, 82.0f}, 18.0f, muted);
    hud.text("SETTINGS", {72.0f, 112.0f}, 54.0f, ink);
    hud.text("PERSONALIZE YOUR DRIVE", {72.0f, 178.0f}, 18.0f, muted);

    hud.text("SETTINGS", {nav_left, 218.0f}, 16.0f, accent);
    const int active_page = flow.settings_page() == SettingsPage::Root ? -1 :
        static_cast<int>(flow.settings_page()) - 1;
    const int hovered_page = flow.settings_hovered_category();
    for (int i = 0; i < 5; ++i) {
        const float y = nav_top + static_cast<float>(i) * row_h;
        const bool selected = hovered_page == i ||
            (flow.settings_page() == SettingsPage::Root
                ? flow.selection() == i : active_page == i);
        if (selected) {
            hud.rect({nav_left - 18.0f, y},
                     {nav_left + nav_width, y + row_h - 6.0f},
                     {0.96f, 0.55f, 0.12f, 0.18f});
            hud.rect({nav_left - 18.0f, y},
                     {nav_left - 12.0f, y + row_h - 6.0f}, accent);
        }
        hud.text(flow.settings_page_label(i), {nav_left, y + 14.0f}, 23.0f,
                 selected ? ink : muted);
    }

    if (flow.settings_page() == SettingsPage::Root) {
        hud.text("CHOOSE A CATEGORY", {content_left, content_top + 10.0f},
                 31.0f, ink);
        hud.text("Use the tabs to tune the way O'Haven looks, sounds, and reads.",
                 {content_left, content_top + 66.0f}, 19.0f, muted);
        hud.line({content_left, content_top + 112.0f},
                 {content_left + content_width, content_top + 112.0f}, 1.0f,
                 {1.0f, 1.0f, 1.0f, 0.16f});
        hud.text("Changes apply immediately.", {content_left, content_top + 142.0f},
                 18.0f, accent);
    } else {
        hud.text(flow.settings_page_label(active_page),
                 {content_left, content_top - 6.0f}, 31.0f, ink);
        hud.text("LEFT / RIGHT   CHANGE        ENTER   TOGGLE", {content_left + content_width - 420.0f,
                 content_top - 1.0f}, 15.0f, muted);
        if (flow.settings_page() == SettingsPage::Map) {
            hud.text("MINIMAP", {content_left, content_top + 31.0f},
                     16.0f, accent);
            hud.text("HUD MARKERS ONLY - THE FULL CITY MAP STAYS DETAILED.",
                     {content_left + 110.0f, content_top + 31.0f}, 15.0f, muted);
        }
        for (int i = 0; i < flow.item_count(); ++i) {
            const float y = content_top + 58.0f + static_cast<float>(i) * row_h;
            const bool selected = i == flow.selection();
            if (selected) {
                hud.rect({content_left - 18.0f, y},
                         {content_left + content_width, y + row_h - 6.0f},
                         {0.96f, 0.55f, 0.12f, 0.18f});
                hud.rect({content_left - 18.0f, y},
                         {content_left - 12.0f, y + row_h - 6.0f}, accent);
            }
            hud.text(flow.settings_label(i), {content_left, y + 13.0f}, 20.0f,
                     selected ? ink : muted);
            hud.text_centered(flow.settings_value(i),
                              content_left + content_width - 70.0f,
                              y + 16.0f, 18.0f, selected ? accent : ink);
            if (flow.settings_page() == SettingsPage::Audio) {
                const float value = i == 0 ? flow.settings().master_volume :
                    i == 1 ? flow.settings().music_volume : flow.settings().sfx_volume;
                const glm::vec2 bar_lo{content_left + content_width - 360.0f, y + 22.0f};
                const glm::vec2 bar_hi{content_left + content_width - 142.0f, y + 30.0f};
                hud.rect(bar_lo, bar_hi, {0.18f, 0.22f, 0.23f, 1.0f});
                hud.rect(bar_lo, {bar_lo.x + 218.0f * value, bar_hi.y}, accent);
            }
        }
    }

    hud.line({72.0f, vp.y - 124.0f}, {vp.x - 72.0f, vp.y - 124.0f},
             1.0f, {1.0f, 1.0f, 1.0f, 0.16f});
    hud.text("UP/DOWN   MOVE     LEFT/RIGHT   CHANGE     ENTER   SELECT",
             {72.0f, vp.y - 98.0f}, 17.0f, muted);
    hud.text("BACKSPACE / ESC   BACK", {72.0f, vp.y - 68.0f}, 17.0f, muted);
}

void GameUi::toggle_waypoint(glm::vec2 pointer, glm::vec2 vp, bool at_centre) {
    const auto viewport = map_viewport(vp, map_camera_);
    if (at_centre) pointer = {viewport.left + viewport.width * .5f,
                              viewport.top + viewport.height * .5f};
    waypoint_.toggle(pointer, viewport);
}

namespace {
constexpr glm::vec4 kMissionBlip{1.0f, .78f, .16f, 1};
constexpr glm::vec4 kWaypointBlip{1.0f, .32f, .64f, 1};
void navigation_blip(Hud& hud, glm::vec2 p, glm::vec4 color, bool mission) {
    const auto diamond = [&](float r, glm::vec4 c) {
        hud.quad(p + glm::vec2{0,-r}, p + glm::vec2{r,0},
                 p + glm::vec2{0,r}, p + glm::vec2{-r,0}, c);
    };
    if (mission) {
        diamond(12, {0,0,0,1}); diamond(8, color);
    } else {
        hud.circle(p, 11, {0,0,0,1}); hud.circle(p, 8, color);
        hud.circle(p, 3, {0.08f,.02f,.04f,1});
    }
}

struct MapSiteMarker {
    const city::StartSite* site;
    glm::vec4 color;
    int icon;
    UiSymbol symbol = UiSymbol::Count;
    const char* letter = "";
    glm::vec2 local_position{};
    const char* label = nullptr;
};

glm::vec2 map_site_world_position(const MapSiteMarker& marker) {
    return marker.site == &city::kAirportSite
        ? site_point(city::kAirportSite, {-35, 150})
        : site_point(*marker.site, marker.local_position);
}

// Keep the atlas POI set in one place. The full map uses these for its
// labelled markers; the radar uses the same positions as small blips.
const MapSiteMarker kMapSiteMarkers[] = {
    {&city::kNorthPinattyGasStationSite,{.1f,.7f,.68f,1},0},
    {&city::kMiandiGasStationSite,{.94f,.72f,.31f,1},0},
    {&city::kFreakyFranksSite,{.96f,.30f,.58f,1},4},
    {&city::kBurgerPizSite,{1.f,.45f,.16f,1},4},
    {&city::kChurchOfWafflesSite,{.62f,.35f,.92f,1},4},
    {&city::kLoomMuseumSite,{.86f,.72f,.49f,1},-1,UiSymbol::Count,"M"},
    {&city::kLoomParkSite,{.43f,.78f,.39f,1},-1,UiSymbol::Count,"P"},
    {&city::kGasStationSite, {0.94f, 0.72f, 0.31f, 1}, 0},
    {&city::kCarWashSite, {0.29f, 0.77f, 0.93f, 1}, 1},
    {&city::kMotelSite, {0.30f, 0.85f, 0.75f, 1}, 2},
    {&city::kApartmentSite, {0.70f, 0.60f, 0.96f, 1}, 3},
    {&city::kFastFoodSite, {0.99f, 0.55f, 0.40f, 1}, 4},
    {&city::kTacomacoSite, {0.88f, 0.70f, 0.22f, 1}, 4},
    {&city::kBankSite, {0.39f, 0.65f, 0.98f, 1}, 5},
    {&city::kAirportSite, {0.64f, 0.80f, 0.96f, 1}, 6},
    {&city::kFlorangiaAirportSite, {0.96f, 0.78f, 0.35f, 1}, 6},
    {&city::kAutoRepairSite, {0.94f, 0.59f, 0.34f, 1}, -1,
     UiSymbol::Build, "R"},
    {&city::kLaundromatSite, {0.42f, 0.85f, 0.83f, 1}, -1,
     UiSymbol::LocalLaundryService, "S"},
    {&city::kMarlinDockSite, {0.35f, 0.83f, 0.94f, 1}, 9},
    {&city::kPawnShopSite, {0.80f, 0.68f, 0.36f, 1}, -1,
     UiSymbol::MoneyRange, "P"},
    {&city::kGunStoreSite, {0.82f, 0.39f, 0.33f, 1}, -1,
     UiSymbol::Target, "G"},
    {&city::kEastArmPlazaSite, {0.96f, 0.66f, 0.28f, 1}, -1,
     UiSymbol::Build, "M"},
    {&city::kMiandiOceanDriveSite, {1.f, .48f, .68f, 1}, 2,
     UiSymbol::Count, "", {city::kCoralCrownRoofs[0].centre.x,
                           city::kCoralCrownRoofs[0].centre.z}, city::kBellmarIdentity.name},
    {&city::kMiandiOceanDriveSite, {.35f, .86f, .96f, 1}, 2,
     UiSymbol::Count, "", {city::kBlueHeronRoofs[0].centre.x,
                           city::kBlueHeronRoofs[0].centre.z}, city::kMaravelleIdentity.name},
    {&city::kMiandiSunwaveHotelSite, {1.f, .86f, .55f, 1}, 2,
     UiSymbol::Count, "", {city::kSunwaveHotelRoofs[0].centre.x,
                           city::kSunwaveHotelRoofs[0].centre.z}, city::kPalmeraIdentity.name},
    {&city::kMiandiPrismWorksSite, {.58f, .58f, 1.f, 1}, -1,
     UiSymbol::Count, "C", {city::kMiandiPrismWorksRoofs[0].centre.x,
                            city::kMiandiPrismWorksRoofs[0].centre.z}, city::kMirageIdentity.name},
    {&city::kMiandiCalleNocheSite, {1.f, .62f, .36f, 1}, -1,
     UiSymbol::Count, "C", {city::kMiandiSolSocialRoofs[0].centre.x,
                            city::kMiandiSolSocialRoofs[0].centre.z}, city::kCandelaIdentity.name},
    {&city::kMiandiCalleNocheSite, {1.f, .80f, .52f, 1}, -1,
     UiSymbol::Count, "T", {city::kMiandiPalmaRoofs[0].centre.x,
                            city::kMiandiPalmaRoofs[0].centre.z}, city::kTropicoIdentity.name},
};
}

void GameUi::draw_minimap(Hud& hud, const UiFlow& flow,
                          const GameUiSnapshot& snapshot, glm::vec2 vp) const {
    if (vp.x <= 0 || vp.y <= 0) return;

    int total_minutes = static_cast<int>(snapshot.time_of_day * 1440.0f + 0.5f);
    total_minutes = ((total_minutes % 1440) + 1440) % 1440;
    const int hour24 = total_minutes / 60;
    const int hour12 = hour24 % 12 == 0 ? 12 : hour24 % 12;
    char clock_text[16];
    std::snprintf(clock_text, sizeof(clock_text), "%d:%02d %s", hour12,
                  total_minutes % 60, hour24 < 12 ? "AM" : "PM");
    constexpr float clock_width = 142.0f;
    constexpr float clock_height = 54.0f;
    const float clock_right = vp.x - 28.0f;
    const float clock_top = 28.0f;
    hud.rect({clock_right - clock_width + 4.0f, clock_top + 4.0f},
             {clock_right + 4.0f, clock_top + clock_height + 4.0f},
             {0.0f, 0.0f, 0.0f, 0.32f});
    hud.rect({clock_right - clock_width, clock_top},
             {clock_right, clock_top + clock_height},
             {0.01f, 0.015f, 0.02f, 0.78f});
    hud.rect({clock_right - clock_width, clock_top},
             {clock_right - clock_width + 5.0f, clock_top + clock_height},
             kAmber);
    hud.text_centered(clock_text, clock_right - clock_width * 0.5f,
                      clock_top + 14.0f, 28.0f, kInk);
    draw_wanted_stars(hud, std::clamp(snapshot.wanted_level, 0, 5),
                      snapshot.wanted_searching || snapshot.wanted_report_pending,
                      snapshot.step,
                      clock_right, clock_top + clock_height + 10.0f);

    const auto view = MinimapView::make(snapshot.player_position, snapshot.player_forward,
                                       snapshot.speed_mph, vp);
    const auto centre = view.centre;
    const float radius = view.radius;
    hud.circle(centre + glm::vec2{3,5}, radius + 9, {0,0,0,.4f});
    hud.circle(centre, radius + 7, {.015f,.02f,.025f,.96f});
    hud.circle(centre, radius + 2, {.29f,.84f,.85f,1});
    // This is the full map's atlas background, clipped to the radar disc.
    hud.circle(centre, radius, {.035f,.078f,.105f,1});

    const auto fill = [&](RadarPolygon polygon, glm::vec4 color) {
        polygon = clip_radar_polygon(polygon, centre, radius);
        for (int i = 1; i + 1 < polygon.count; ++i)
            hud.triangle(polygon.points[0], polygon.points[static_cast<std::size_t>(i)],
                         polygon.points[static_cast<std::size_t>(i + 1)], color);
    };
    const auto world_polygon = [&](const glm::vec2* points, int count, glm::vec4 color) {
        // Reject in world space before rotating any vertices. Terrain is the
        // atlas's immutable cache; no height sampling or scene rendering here.
        glm::vec2 lo{1e9f}, hi{-1e9f};
        for (int i = 0; i < count; ++i) {
            lo = glm::min(lo, points[i]); hi = glm::max(hi, points[i]);
        }
        if (lo.x > view.origin.x + view.range_m || hi.x < view.origin.x - view.range_m ||
            lo.y > view.origin.y + view.range_m || hi.y < view.origin.y - view.range_m) return;
        RadarPolygon polygon;
        polygon.count = count;
        for (int i = 0; i < count; ++i)
            polygon.points[static_cast<std::size_t>(i)] = view.project(points[i]);
        fill(polygon, color);
    };
    for (const auto& patch : land_patches_)
        world_polygon(patch.points.data(), patch.count, {.135f,.185f,.208f,1});

    const auto stroke = [&](glm::vec2 a, glm::vec2 b, float width, glm::vec4 color) {
        // Bound long spines before forming their quads, keeping clipping stable.
        const glm::vec2 extent{radius + width};
        if (!clip_segment(a, b, centre - extent, centre + extent)) return;
        const float length = glm::length(b - a);
        if (length < .001f) return;
        const glm::vec2 side = glm::vec2{a.y-b.y,b.x-a.x} * (width * .5f / length);
        RadarPolygon polygon;
        polygon.count = 4;
        polygon.points[0] = a-side; polygon.points[1] = b-side;
        polygon.points[2] = b+side; polygon.points[3] = a+side;
        fill(polygon, color);
    };

    // Use the same coastline, topography, and district boundaries as the
    // atlas. The overview contour cache is the right density at radar scale.
    for (const MapContour& contour : overview_contours_) {
        const glm::vec2 a = view.project(contour.a);
        const glm::vec2 b = view.project(contour.b);
        if (contour.elevation == kSeaLevelMetres) {
            stroke(a, b, 3.0f, {0.16f, 0.33f, 0.36f, 0.44f});
            stroke(a, b, 0.9f, {0.31f, 0.48f, 0.48f, 0.80f});
        } else {
            stroke(a, b, 0.7f, {0.31f, 0.40f, 0.42f, 0.35f});
        }
    }
    for (const city::District& district : city::kDistricts) {
        for (int i = 0; i < district.boundary.count; ++i) {
            const city::Vec2& a = district.boundary.points[i];
            const city::Vec2& b = district.boundary.points[(i + 1) % district.boundary.count];
            stroke(view.project({a.x, a.z}), view.project({b.x, b.z}),
                   0.7f, {0.49f, 0.60f, 0.62f, 0.32f});
        }
    }
    for (const auto& footprint : footprints_)
        world_polygon(footprint.corners, 4,
            footprint.dock ? glm::vec4{0.34f,0.28f,0.20f,1} :
            footprint.lot ? glm::vec4{0.10f,0.15f,0.18f,1} :
                             glm::vec4{0.32f,0.43f,0.48f,1});
    std::array<const city::Road*, city::kRoadCount> road_order{};
    for (int i = 0; i < city::kRoadCount; ++i)
        road_order[static_cast<std::size_t>(i)] = &city::kRoads[i];
    std::stable_sort(road_order.begin(), road_order.end(), [](const auto* a, const auto* b) {
        return static_cast<int>(a->cls) > static_cast<int>(b->cls);
    });
    const float scale = radius / view.range_m;
    for (int pass = 0; pass < 3; ++pass) {
        for (const city::Road* item : road_order) {
            const city::Road& road = *item;
            const float base_inner = road_thickness(road.cls) * 1.35f;
            float width_extra = 0.0f;
            glm::vec4 color = road_color(road.cls);
            if (pass == 0) {
                width_extra = city::road_has_sidewalks(road.cls)
                    ? city::kWalkWidthM * 2.0f * scale + 4.0f : 4.0f;
                color = {0.23f, 0.30f, 0.34f, 1.0f};
            } else if (pass == 1) {
                width_extra = 2.5f;
                color = {0.045f, 0.075f, 0.090f, 1.0f};
            }
            for (int i = 0; i + 1 < road.count; ++i) {
                const float metres = 0.5f * (road.carriageway_width_at_point(i) +
                                             road.carriageway_width_at_point(i + 1));
                const float width = std::max(base_inner, metres * scale) + width_extra;
                stroke(view.project({road.path[i].x,road.path[i].z}),
                       view.project({road.path[i+1].x,road.path[i+1].z}),
                       width, color);
                if (pass == 2 && road.cls == city::RoadClass::Freeway) {
                    stroke(view.project({road.path[i].x, road.path[i].z}),
                           view.project({road.path[i + 1].x, road.path[i + 1].z}),
                           1.0f, {0.35f, 0.27f, 0.15f, 0.8f});
                }
            }
        }
    }

    // The atlas's places stay available on the radar as compact dots. They
    // inherit the same authored positions and colors as the full-map icons;
    // labels would just turn a 284 px radar into noise.
    const auto site_world_position = [](const MapSiteMarker& site) {
        return map_site_world_position(site);
    };
    const auto radar_marker = [&](glm::vec2 world, glm::vec4 color, float size) {
        const glm::vec2 p = view.project(world);
        if (glm::length(p - centre) > radius - size - 2.0f) return;
        hud.circle(p, size + 2.0f, {0.015f, 0.025f, 0.035f, 1.0f});
        hud.circle(p, size, color);
    };
    const auto radar_site_icon = [&](const MapSiteMarker& site) {
        const glm::vec2 p = view.project(site_world_position(site));
        constexpr float kPoiScale = 1.3225f; // 15% larger than the previous 1.15x pass.
        constexpr float kIconScale = 0.55f * kPoiScale;
        constexpr float kIconRadius = 11.0f * kPoiScale;
        if (glm::length(p - centre) > radius - kIconRadius - 2.0f) return;
        if (site.icon >= 0 || site.symbol != UiSymbol::Count) {
            map_site_icon(hud, p, site.icon, site.color, kIconScale,
                          site.symbol);
        } else {
            hud.circle(p, 9.0f * kPoiScale, {0.015f, 0.025f, 0.035f, 1.0f});
            hud.circle(p, 7.0f * kPoiScale, site.color);
            hud.text_centered(site.letter, p.x,
                              p.y - hud.text_line_height(11.0f * kPoiScale) * 0.5f,
                              11.0f * kPoiScale, {0.025f, 0.045f, 0.060f, 1.0f});
        }
    };
    if (flow.settings().minimap_points_of_interest) {
        for (const MapSiteMarker& site : kMapSiteMarkers)
            radar_site_icon(site);
        for (const city::Landmark& landmark : city::kLandmarks)
            radar_marker({landmark.pos.x, landmark.pos.z}, {0.77f, 0.83f, 0.72f, 1}, 2.5f);
    }

    // Targets clamp radially, never independently by axis: their true bearing
    // survives even when the destination is on the far side of the island.
    if (waypoint_.position && flow.settings().minimap_waypoint_marker)
        navigation_blip(hud, view.blip(*waypoint_.position), kWaypointBlip, false);
    if (snapshot.mission_target && flow.settings().minimap_mission_marker)
        navigation_blip(hud, view.blip(*snapshot.mission_target), kMissionBlip, true);
    const auto north = centre + view.direction({0,-1}) * (radius + 4);
    hud.circle(north, 14, {.015f,.02f,.025f,1});
    hud.text_centered("N", north.x, north.y - hud.text_line_height(20)*.5f, 20, kInk);
    hud.triangle(centre+glm::vec2{0,-15}, centre+glm::vec2{-11,11},
                 centre+glm::vec2{11,11}, {0,0,0,1});
    hud.triangle(centre+glm::vec2{0,-11}, centre+glm::vec2{-7,7},
                 centre+glm::vec2{7,7}, {1,1,.94f,1});
    const char* district = city::district_name(city::district_at(view.origin.x, view.origin.y));
    const float label_y = centre.y + radius + 14;
    if (snapshot.road_name && snapshot.road_name[0]) {
        const float measured = hud.measure_text(snapshot.road_name, 20);
        const float road_glyph = measured > radius * 1.82f
            ? std::max(14.0f, 20.0f * radius * 1.82f / measured) : 20.0f;
        hud.text_centered(snapshot.road_name, centre.x+1, label_y+2, road_glyph, {0,0,0,1});
        hud.text_centered(snapshot.road_name, centre.x, label_y, road_glyph, kInk);
        hud.text_centered(district, centre.x, label_y+25, 14, {.70f,.72f,.67f,1});
    } else {
        hud.text_centered(district, centre.x+1, label_y+2, 20, {0,0,0,1});
        hud.text_centered(district, centre.x, label_y, 20, kInk);
    }
}

void GameUi::draw_map(Hud& hud, const UiFlow& flow, const GameUiSnapshot& snapshot,
                      glm::vec2 vp) const {
    const glm::vec4 ink{0.91f, 0.95f, 0.97f, 1.0f};
    const glm::vec4 muted{0.53f, 0.65f, 0.71f, 1.0f};
    const glm::vec4 accent{0.29f, 0.84f, 0.85f, 1.0f};
    const glm::vec4 chrome{0.022f, 0.035f, 0.048f, 1.0f};
    const MapViewport mv = map_viewport(vp, map_camera_);
    const glm::vec2 map_lo{mv.left, mv.top};
    const glm::vec2 map_hi{mv.left + mv.width, mv.top + mv.height};
    const float scale = map_pixels_per_metre(mv);
    const float zoom = map_camera_.zoom_level();
    const bool places = flow.map_layer() != MapLayer::Roads;
    const bool street_labels = flow.map_layer() != MapLayer::Places;
    hud.rect({0, 0}, vp, chrome);
    hud.rect(map_lo, map_hi, {0.035f, 0.078f, 0.105f, 1.0f});
    hud.set_clip_rect(map_lo, map_hi);

    const auto project = [&](glm::vec2 world) {
        return map_point(world.x, world.y, mv);
    };
    const auto draw_polygon = [&](MapPolygon polygon, glm::vec4 color) {
        glm::vec2 lo{1e9f}, hi{-1e9f};
        for (int i = 0; i < polygon.count; ++i) {
            auto& p = polygon.points[static_cast<std::size_t>(i)];
            p = project(p);
            lo = glm::min(lo, p); hi = glm::max(hi, p);
        }
        if (hi.x < map_lo.x || lo.x > map_hi.x ||
            hi.y < map_lo.y || lo.y > map_hi.y) return;
        polygon = clip_map_polygon(polygon, map_lo, map_hi);
        for (int i = 1; i + 1 < polygon.count; ++i)
            hud.triangle(polygon.points[0], polygon.points[static_cast<std::size_t>(i)],
                         polygon.points[static_cast<std::size_t>(i + 1)], color);
    };
    const auto stroke = [&](glm::vec2 a, glm::vec2 b, float width,
                             glm::vec4 color) {
        const glm::vec2 pad{width * 0.5f + 1.0f};
        if (clip_segment(a, b, map_lo - pad, map_hi + pad))
            hud.smooth_line(a, b, width, color);
    };
    for (const MapPolygon& patch : land_patches_)
        draw_polygon(patch, {0.135f, 0.185f, 0.208f, 1.0f});

    const auto& visible_contours = zoom < 2.0f ? overview_contours_ : contours_;
    for (const MapContour& contour : visible_contours) {
        const glm::vec2 a = project(contour.a), b = project(contour.b);
        if (contour.elevation == kSeaLevelMetres) {
            stroke(a, b, 5.0f, {0.16f, 0.33f, 0.36f, 0.44f});
            stroke(a, b, 1.4f, {0.31f, 0.48f, 0.48f, 0.80f});
        } else {
            stroke(a, b, 1.0f, {0.31f, 0.40f, 0.42f, 0.35f});
        }
    }
    if (zoom < 4.0f) {
        for (const city::District& district : city::kDistricts) {
            for (int i = 0; i < district.boundary.count; ++i) {
                const city::Vec2 a = district.boundary.points[i];
                const city::Vec2 b = district.boundary.points[(i + 1) % district.boundary.count];
                stroke(map_point(a.x, a.z, mv), map_point(b.x, b.z, mv),
                       1.0f, {0.49f, 0.60f, 0.62f, 0.32f});
            }
        }

        // Miandi uses local district boundaries but real world roads and
        // buildings. The normal road pass below draws its authored spines.
        for (const city::MiandiDistrict& district : city::kMiandiDistricts) {
            MapPolygon polygon;
            polygon.count = district.boundary.count;
            for (int i = 0; i < polygon.count; ++i) {
                const city::Vec2 world = city::miandi_world_point(
                    district.boundary.points[i]);
                polygon.points[static_cast<std::size_t>(i)] = {world.x, world.z};
            }
            draw_polygon(polygon, {0.24f, 0.20f, 0.12f, 0.24f});
            for (int i = 0; i < district.boundary.count; ++i) {
                const city::Vec2 a = city::miandi_world_point(
                    district.boundary.points[i]);
                const city::Vec2 b = city::miandi_world_point(
                    district.boundary.points[(i + 1) % district.boundary.count]);
                stroke(project({a.x, a.z}), project({b.x, b.z}),
                       1.4f, {0.96f, 0.68f, 0.32f, 0.82f});
            }
        }
    }

    // Clip the actual lot/roof polygons, including partially visible buildings.
    // Their proportions stay correct as the window shape changes.
    if (zoom >= 1.8f) {
        for (const MapFootprint& fp : footprints_) {
            MapPolygon polygon;
            polygon.count = 4;
            std::copy(std::begin(fp.corners), std::end(fp.corners), polygon.points.begin());
            draw_polygon(polygon, fp.dock ? glm::vec4{0.34f,0.28f,0.20f,1}
                                : fp.lot ? glm::vec4{0.10f, 0.15f, 0.18f, 1.0f}
                                        : glm::vec4{0.32f, 0.43f, 0.48f, 1.0f});
            for (int i = 0; i < 4; ++i)
                stroke(project(fp.corners[i]), project(fp.corners[(i + 1) % 4]),
                       fp.lot ? 1.0f : 1.5f,
                       fp.dock ? glm::vec4{0.67f,0.56f,0.36f,1}
                       : fp.lot ? glm::vec4{0.23f, 0.32f, 0.36f, 1.0f}
                              : glm::vec4{0.49f, 0.60f, 0.64f, 1.0f});
        }
    }

    // Streets use their authored width at close zoom, with sidewalks and road
    // casing underneath. Round joins avoid cracks at sharp spine corners.
    std::array<const city::Road*, city::kRoadCount> road_order{};
    for (int i = 0; i < city::kRoadCount; ++i)
        road_order[static_cast<std::size_t>(i)] = &city::kRoads[i];
    std::stable_sort(road_order.begin(), road_order.end(), [](const auto* a, const auto* b) {
        return static_cast<int>(a->cls) > static_cast<int>(b->cls);
    });
    for (int pass = 0; pass < 3; ++pass) {
        for (const city::Road* item : road_order) {
            const city::Road& road = *item;
            if (road.cls == city::RoadClass::Alley && zoom < 2.0f) continue;
            const float base_inner = road_thickness(road.cls) * 1.35f;
            float width_extra = 0.0f;
            glm::vec4 color = road_color(road.cls);
            if (pass == 0) {
                width_extra += zoom >= 3.0f && city::road_has_sidewalks(road.cls)
                             ? city::kWalkWidthM * 2.0f * scale + 4.0f : 4.0f;
                color = {0.23f, 0.30f, 0.34f, 1.0f};
            } else if (pass == 1) {
                width_extra += 2.5f;
                color = {0.045f, 0.075f, 0.090f, 1.0f};
            }
            for (int i = 0; i + 1 < road.count; ++i) {
                const float metres = 0.5f * (road.carriageway_width_at_point(i) +
                                             road.carriageway_width_at_point(i + 1));
                const float width = std::max(base_inner, metres * scale) + width_extra;
                stroke(map_point(road.path[i].x, road.path[i].z, mv),
                       map_point(road.path[i + 1].x, road.path[i + 1].z, mv), width, color);
            }
            for (int i = 0; i < road.count; ++i) {
                const float width = std::max(base_inner,
                    road.carriageway_width_at_point(i) * scale) + width_extra;
                const glm::vec2 p = map_point(road.path[i].x, road.path[i].z, mv);
                if (inside(p, map_lo, map_hi, -width * 0.5f - 1.0f))
                    hud.circle(p, width * 0.5f, color);
            }
            if (pass == 2 && zoom >= 6.0f && road.cls == city::RoadClass::Freeway) {
                for (int i = 0; i + 1 < road.count; ++i)
                    stroke(map_point(road.path[i].x, road.path[i].z, mv),
                           map_point(road.path[i + 1].x, road.path[i + 1].z, mv),
                           1.4f, {0.35f, 0.27f, 0.15f, 0.8f});
            }
        }
    }

    struct LabelBox { glm::vec2 lo, hi; };
    std::vector<LabelBox> labels;
    labels.reserve(180);
    const glm::vec2 player = project({snapshot.player_position.x, snapshot.player_position.z});
    labels.push_back({player - glm::vec2{25}, player + glm::vec2{25}});
    // Compass and scale occupy the same reserved area on every zoom.
    labels.push_back({map_hi - glm::vec2{90, mv.height - 16}, {map_hi.x - 12, map_lo.y + 100}});
    labels.push_back({map_hi - glm::vec2{280, 86}, map_hi});
    const auto label_fits = [&](glm::vec2 centre, const char* name, float glyph) {
        const glm::vec2 half{hud.measure_text(name, glyph) * 0.5f + 12.0f,
                             hud.text_line_height(glyph) * 0.5f + 7.0f};
        const LabelBox box{centre - half, centre + half};
        if (!inside(box.lo, map_lo, map_hi, 12) ||
            !inside(box.hi, map_lo, map_hi, 12)) return false;
        for (const LabelBox& used : labels)
            if (box.lo.x < used.hi.x && box.hi.x > used.lo.x &&
                box.lo.y < used.hi.y && box.hi.y > used.lo.y) return false;
        labels.push_back(box);
        return true;
    };
    // State names own the OVERVIEW, city names own everything closer, and the
    // bands do not overlap. O'Haven's landmass and Pinatty's urban extent are
    // very nearly the same shape, so both labels want the same pixels; drawing
    // them together just means one of them loses a collision every frame and
    // flickers. Zoom decides instead: which landmass, then which city.
    //
    // Both passes stay ahead of sites, districts and roads in the collision
    // list, so the largest place on screen always gets its name.
    if (zoom < 2.0f) {
        const float glyph = zoom < 1.8f ? 36.0f : 30.0f;
        for (const city::State& item : city::kStates) {
            const glm::vec2 anchor = project(
                {item.map_label_anchor.x, item.map_label_anchor.z});
            const glm::vec4 color = item.id == city::StateId::Florangia
                ? glm::vec4{0.94f, 0.81f, 0.53f, 1.0f}
                : glm::vec4{0.73f, 0.86f, 0.89f, 1.0f};
            for (const float dy : {0.0f, -48.0f, 48.0f, -82.0f, 82.0f}) {
                const glm::vec2 at = anchor + glm::vec2{0.0f, dy};
                if (label_fits(at, item.name, glyph)) {
                    map_halo_text(hud, item.name, at, glyph, color);
                    break;
                }
            }
        }
    }
    // The regional band, and its edges are both load-bearing. It starts where
    // the state label stops, and it ENDS at the street-label threshold below:
    // Pinatty's anchor sits over downtown, so the city name must be gone by the
    // time downtown has streets and sites worth reading. Between the two, the
    // island fills the view and the only useful question is which city this is.
    const auto site_position = [&](const MapSiteMarker& site) {
        return project(map_site_world_position(site));
    };
    // City names are DEFERRED, not drawn here, and the reason is downtown: the
    // site icons are drawn after this point, and a 34pt name placed first still
    // ends up underneath a pile of coloured discs. Reserving the box stops other
    // TEXT from landing on it and does nothing about the icons. So the placement
    // happens here — early, so the city keeps first claim on the map — and the
    // draw happens after the icons, on top of them.
    struct PendingLabel { const char* name; glm::vec2 at; glm::vec4 color; };
    std::vector<PendingLabel> city_labels;
    if (zoom >= 2.0f && zoom < 3.2f) {
        // The icon footprints the name has to get out from under. Same ±20 box
        // the site pass reserves below, computed early so the offset ladder can
        // see the cluster instead of walking into it.
        std::vector<LabelBox> icons;
        if (places) {
            for (const MapSiteMarker& site : kMapSiteMarkers) {
                const glm::vec2 p = site_position(site);
                if (inside(p, map_lo, map_hi, 20))
                    icons.push_back({p - glm::vec2{20}, p + glm::vec2{20}});
            }
        }
        const auto clears_icons = [&](glm::vec2 centre, const char* name) {
            const glm::vec2 half{hud.measure_text(name, 34.0f) * 0.5f + 12.0f,
                                 hud.text_line_height(34.0f) * 0.5f + 7.0f};
            for (const LabelBox& icon : icons)
                if (centre.x - half.x < icon.hi.x && centre.x + half.x > icon.lo.x &&
                    centre.y - half.y < icon.hi.y && centre.y + half.y > icon.lo.y)
                    return false;
            return true;
        };
        for (const city::City& item : city::kCities) {
            const glm::vec2 anchor = project(
                {item.map_label_anchor.x, item.map_label_anchor.z});
            const glm::vec4 color = item.state == city::StateId::Florangia
                ? glm::vec4{1.00f, 0.78f, 0.40f, 1.0f}
                : glm::vec4{0.73f, 0.86f, 0.89f, 1.0f};
            // North first and further than the other passes go. Downtown is the
            // densest part of any city here, so the slot that clears it is
            // rarely the nearest one, and a city name 200px off its anchor still
            // reads as that city — one buried in the icons does not.
            bool placed = false;
            for (const float dy : {-96.0f, -150.0f, -204.0f, 96.0f, 150.0f,
                                   204.0f, -258.0f, 258.0f, 0.0f}) {
                const glm::vec2 at = anchor + glm::vec2{0.0f, dy};
                if (!clears_icons(at, item.name)) continue;
                if (label_fits(at, item.name, 34.0f)) {
                    city_labels.push_back({item.name, at, color});
                    placed = true;
                    break;
                }
            }
            // Every slot blocked. The city still gets its name — it is the most
            // important label on the screen and dropping it is what started
            // this — and drawing last means the icons no longer bury it.
            if (!placed) {
                for (const float dy : {0.0f, -96.0f, 96.0f}) {
                    const glm::vec2 at = anchor + glm::vec2{0.0f, dy};
                    if (label_fits(at, item.name, 34.0f)) {
                        city_labels.push_back({item.name, at, color});
                        break;
                    }
                }
            }
        }
    }
    if (places) {
        for (const MapSiteMarker& site : kMapSiteMarkers) {
            const glm::vec2 p = site_position(site);
            if (inside(p, map_lo, map_hi, 20))
                labels.push_back({p - glm::vec2{20}, p + glm::vec2{20}});
        }
        for (int index = 0; index < static_cast<int>(std::size(kMapSiteMarkers)); ++index) {
            const MapSiteMarker& site = kMapSiteMarkers[index];
            const glm::vec2 p = site_position(site);
            if (!inside(p, map_lo, map_hi, 21)) continue;
            const bool marina=site.site==&city::kMarlinDockSite;
            if (marina) map_site_icon(hud,p,9,site.color);
            else if (zoom < 2.2f) marker(hud, p, site.color);
            else if (site.icon >= 0 || site.symbol != UiSymbol::Count)
                map_site_icon(hud, p, site.icon, site.color, 1.0f,
                              site.symbol);
            else {
                hud.circle(p, 16, {0.015f,0.025f,0.035f,1});
                hud.circle(p, 13, site.color);
                hud.circle(p, 10, {0.015f,0.025f,0.035f,1});
                hud.text_centered(site.letter, p.x,
                    p.y-hud.text_line_height(17)*.5f,17,site.color);
            }
            if (zoom < 2.2f && !marina) continue;
            const float glyph = zoom < 2.2f ? 20.f : 24.f;
            const char* name = site.label ? site.label : site.site->name;
            const float dx = hud.measure_text(name, glyph) * 0.5f + 40;
            for (const glm::vec2 offset : {glm::vec2{dx, 0}, glm::vec2{-dx, 0},
                                          glm::vec2{0, -52}, glm::vec2{0, 52}}) {
                if (label_fits(p + offset, name, glyph)) {
                    map_label(hud, name, p + offset, glyph, ink);
                    break;
                }
            }
        }
        for (const city::Landmark& landmark : city::kLandmarks) {
            if (zoom < 2.2f && landmark.tier == city::LandmarkTier::Corner) continue;
            const glm::vec2 p = map_point(landmark.pos.x, landmark.pos.z, mv);
            if (!inside(p, map_lo, map_hi, 12)) continue;
            const bool covered = std::any_of(labels.begin(), labels.end(), [&](const LabelBox& box) {
                return p.x + 9 > box.lo.x && p.x - 9 < box.hi.x &&
                       p.y + 9 > box.lo.y && p.y - 9 < box.hi.y;
            });
            if (covered) continue;
            marker(hud, p, {0.77f, 0.83f, 0.72f, 1});
            labels.push_back({p - glm::vec2{9}, p + glm::vec2{9}});
            const glm::vec2 label = p + glm::vec2{0, 27};
            if (zoom >= 4.0f && label_fits(label, landmark.name, 21))
                map_halo_text(hud, landmark.name, label, 21, ink);
        }
    }
    // The city names, last of the place labels and therefore over the icons
    // rather than under them. Their boxes went into `labels` back where they
    // were placed, so nothing drawn in between chose to sit here.
    for (const PendingLabel& item : city_labels)
        map_halo_text(hud, item.name, item.at, 34.0f, item.color);
    if (flow.map_layer() == MapLayer::Explore && zoom < 3.0f) {
        for (const city::District& district : city::kDistricts) {
            glm::vec2 centre{};
            for (int i = 0; i < district.boundary.count; ++i)
                centre += glm::vec2{district.boundary.points[i].x, district.boundary.points[i].z};
            centre /= static_cast<float>(district.boundary.count);
            const glm::vec2 p = project(centre);
            for (const float dy : {0.0f, -42.0f, 42.0f, -76.0f, 76.0f}) {
                const auto at = p + glm::vec2{0, dy};
                if (label_fits(at, district.name, 30)) {
                    map_halo_text(hud, district.name, at, 30, {0.73f, 0.83f, 0.85f, 1});
                    break;
                }
            }
        }
    }
    if (street_labels) {
        std::vector<std::string_view> names;
        for (int priority = 0; priority < 5; ++priority) {
            for (const city::Road& road : city::kRoads) {
                if (static_cast<int>(road.cls) != priority || road.count < 2) continue;
                const bool show = road.cls == city::RoadClass::Freeway ||
                    (road.cls == city::RoadClass::Arterial && zoom >= 1.8f) ||
                    (road.cls == city::RoadClass::Street && zoom >= 3.2f) ||
                    (road.cls == city::RoadClass::Dirt && zoom >= 5.0f) ||
                    (road.cls == city::RoadClass::Alley && zoom >= 7.0f);
                if (!show || std::find(names.begin(), names.end(), road.name) != names.end()) continue;
                // Anchor on the longest visible segment, not the road's global
                // midpoint (which often leaves the screen at close zoom).
                glm::vec2 anchor{};
                float best = 0;
                for (int i = 0; i + 1 < road.count; ++i) {
                    glm::vec2 a = map_point(road.path[i].x, road.path[i].z, mv);
                    glm::vec2 b = map_point(road.path[i + 1].x, road.path[i + 1].z, mv);
                    if (!clip_segment(a, b, map_lo + glm::vec2{45}, map_hi - glm::vec2{45})) continue;
                    const float length = glm::length(b - a);
                    if (length > best) { best = length; anchor = (a + b) * 0.5f; }
                }
                const float size = road.cls == city::RoadClass::Freeway ? 25.0f : 23.0f;
                if (best < 65) continue;
                const float half = hud.measure_text(road.name, size) * 0.5f + 15;
                if (half * 2 >= mv.width - 30) continue;
                anchor.x = std::clamp(anchor.x, map_lo.x + half, map_hi.x - half);
                for (const float dy : {0.0f, -32.0f, 32.0f}) {
                    const glm::vec2 at = anchor + glm::vec2{0, dy};
                    if (label_fits(at, road.name, size)) {
                        map_halo_text(hud, road.name, at, size,
                                      road.cls == city::RoadClass::Freeway
                                          ? glm::vec4{0.98f, 0.83f, 0.55f, 1} : ink);
                        names.emplace_back(road.name);
                        break;
                    }
                }
            }
        }
    }
    if (inside(player, map_lo, map_hi, 25)) {
        hud.circle(player, 22, {0.20f, 0.80f, 0.83f, 0.22f});
        hud.circle(player, 14, {0.025f, 0.05f, 0.065f, 1});
        glm::vec2 heading{snapshot.player_forward.x, snapshot.player_forward.z};
        heading = glm::length(heading) > 1e-4f ? glm::normalize(heading) : glm::vec2{0, -1};
        const glm::vec2 side{-heading.y, heading.x};
        hud.triangle(player + heading * 15.0f, player - heading * 8.0f + side * 9.0f,
                     player - heading * 4.0f, ink);
        hud.triangle(player + heading * 15.0f, player - heading * 4.0f,
                     player - heading * 8.0f - side * 9.0f, accent);
    }

    if (waypoint_.position)
        navigation_blip(hud, project(*waypoint_.position), kWaypointBlip, false);
    if (snapshot.mission_target)
        navigation_blip(hud, project(*snapshot.mission_target), kMissionBlip, true);
    const auto cursor = (map_lo + map_hi) * .5f;
    hud.line(cursor - glm::vec2{10,0}, cursor + glm::vec2{10,0}, 3, {0,0,0,.8f});
    hud.line(cursor - glm::vec2{0,10}, cursor + glm::vec2{0,10}, 3, {0,0,0,.8f});
    hud.line(cursor - glm::vec2{9,0}, cursor + glm::vec2{9,0}, 1, ink);
    hud.line(cursor - glm::vec2{0,9}, cursor + glm::vec2{0,9}, 1, ink);

    const glm::vec2 north{map_hi.x - 46, map_lo.y + 57};
    hud.circle(north, 27, chrome);
    hud.triangle(north + glm::vec2{0, -13}, north + glm::vec2{-8, 10},
                 north + glm::vec2{0, 5}, accent);
    hud.triangle(north + glm::vec2{0, -13}, north + glm::vec2{0, 5},
                 north + glm::vec2{8, 10}, muted);
    hud.text_centered("N", north.x, north.y - 48, 20, ink);
    const float desired_metres = 160.0f / scale;
    const float power = std::pow(10.0f, std::floor(std::log10(desired_metres)));
    const float fraction = desired_metres / power;
    const float distance = (fraction >= 5 ? 5.0f : fraction >= 2 ? 2.0f : 1.0f) * power;
    const float scale_px = distance * scale;
    const glm::vec2 scale_b = map_hi - glm::vec2{30, 27};
    const glm::vec2 scale_a = scale_b - glm::vec2{scale_px, 0};
    hud.rect(scale_a - glm::vec2{16, 43}, scale_b + glm::vec2{16, 14}, chrome);
    hud.line(scale_a, scale_b, 2, ink);
    hud.line(scale_a - glm::vec2{0, 6}, scale_a + glm::vec2{0, 2}, 2, ink);
    hud.line(scale_b - glm::vec2{0, 6}, scale_b + glm::vec2{0, 2}, 2, ink);
    char text[192];
    std::snprintf(text, sizeof(text), "%.0f M", static_cast<double>(distance));
    hud.text_centered(text, (scale_a.x + scale_b.x) * 0.5f, scale_a.y - 35, 20, ink);

    hud.clear_clip_rect();
    hud.outline(map_lo, map_hi, 1, {0.30f, 0.40f, 0.45f, 0.7f});
    hud.text("O'HAVEN + FLORANGIA", {32, 20}, 34, ink);
    hud.text("STATE ATLAS", {365, 38}, 20, muted);
    const char* layers[] = {"EXPLORE", "ROADS", "PLACES"};
    const float chips_x = vp.x - 488;
    for (int i = 0; i < 3; ++i) {
        const glm::vec2 lo{chips_x + static_cast<float>(i) * 126.0f, 25};
        const bool active = static_cast<int>(flow.map_layer()) == i;
        hud.rect(lo, lo + glm::vec2{116, 43}, active ? accent : glm::vec4{0.09f, 0.14f, 0.17f, 1});
        hud.text_centered(layers[i], lo.x + 58, lo.y + 7, 23, active ? chrome : muted);
    }
    std::snprintf(text, sizeof(text), "%.1fX", static_cast<double>(zoom));
    hud.text_centered(text, vp.x - 58, 34, 25, ink);
    hud.text("C / Y  CHANGE LAYER", {chips_x, 75}, 16, muted);
    navigation_blip(hud, {42,86}, kMissionBlip, true);
    hud.text("MISSION", {60,73}, 18, ink);
    navigation_blip(hud, {158,86}, kWaypointBlip, false);
    hud.text("WAYPOINT", {176,73}, 18, ink);
    const auto district = city::district_at(snapshot.player_position.x, snapshot.player_position.z);
    hud.circle({42, vp.y - 79}, 5, accent);
    hud.text("YOUR LOCATION", {58, vp.y - 94}, 18, muted);
    hud.text(city::district_name(district), {32, vp.y - 63}, 28, ink);
    hud.text_centered("WASD / STICK / DRAG   PAN      WHEEL / +/- / LB-RB   ZOOM",
                      vp.x * 0.57f, vp.y - 96, 19, ink);
    hud.text_centered("RIGHT CLICK   MARK SPOT      R / X   MARK CENTRE      REPEAT   CLEAR",
                      vp.x * 0.57f, vp.y - 65, 18, kWaypointBlip);
    hud.text_centered("ENTER / A   RECENTER      C / Y   LAYER      M / ESC / B   BACK",
                      vp.x * 0.57f, vp.y - 34, 18, muted);
}

void GameUi::draw_perf_recorder(Hud& hud, const GameUiSnapshot& snapshot,
                                glm::vec2 vp) const {
    if (!snapshot.perf_logging || vp.x <= 0.0f || vp.y <= 0.0f) return;

    // Directly under the clock, sharing its right edge and width so the two
    // read as one stack rather than two things that happen to be near a
    // corner.
    constexpr float width = 142.0f;
    constexpr float height = 30.0f;
    const float right = vp.x - 28.0f;
    const float top = 28.0f + 54.0f + 8.0f;
    const float left = right - width;

    hud.rect({left, top}, {right, top + height}, {0.01f, 0.015f, 0.02f, 0.72f});

    // A slow pulse, keyed on the SIM STEP rather than a wall clock. Every
    // other animated element in this HUD is keyed the same way, and a replay
    // that pulses differently from the run it recorded is a distraction in
    // exactly the footage someone is studying frame by frame.
    const float phase =
        static_cast<float>(snapshot.step % 240) / 240.0f;
    // Never fades far enough to stop reading as RED. A recording dot that
    // dims to grey looks like a disabled control at the bottom of its cycle,
    // which is the opposite of what it is there to say.
    const float pulse = 0.78f + 0.22f * std::cos(phase * 6.2831853f);
    const bool marked = snapshot.perf_mark_feedback_s > 0.0f;

    const glm::vec4 dot = marked ? glm::vec4{0.35f, 0.95f, 0.45f, 1.0f}
                                 : glm::vec4{0.92f, 0.22f, 0.20f, pulse};
    hud.circle({left + 17.0f, top + height * 0.5f}, 6.0f, dot);

    char label[32];
    if (marked) {
        std::snprintf(label, sizeof(label), "MARK %d", snapshot.perf_marks);
    } else {
        std::snprintf(label, sizeof(label), "REC %s", snapshot.perf_log_label);
    }
    hud.text(label, {left + 30.0f, top + 6.0f}, 19.0f,
             marked ? glm::vec4{0.35f, 0.95f, 0.45f, 1.0f} : kMuted);
}

void GameUi::draw(Hud& hud, const UiFlow& flow,
                  const GameUiSnapshot& snapshot, glm::vec2 viewport_px) const {
    switch (flow.screen()) {
        case UiScreen::Title: draw_title(hud, flow, viewport_px, snapshot.title_opacity); break;
        case UiScreen::Pause: draw_pause(hud, flow, snapshot, viewport_px); break;
        case UiScreen::Map:   draw_map(hud, flow, snapshot, viewport_px); break;
        case UiScreen::Driving:
            draw_minimap(hud, flow, snapshot, viewport_px);
            draw_perf_recorder(hud, snapshot, viewport_px);
            break;
        case UiScreen::Settings: draw_settings(hud, flow, viewport_px); break;
    }
    if (snapshot.save_notice && snapshot.save_notice[0] &&
        (flow.screen() == UiScreen::Title || flow.screen() == UiScreen::Pause)) {
        hud.text_centered(snapshot.save_notice, viewport_px.x * .5f,
                          viewport_px.y - 158.0f, 20.0f, kAmber);
    }
}

void GameUi::draw_wanted_badge(Hud& hud, int wanted_level, bool flash, int64_t step,
                               float right, float top) const {
    draw_wanted_stars(hud, std::clamp(wanted_level, 0, 5), flash, step, right, top);
}

void GameUi::draw_arrested(Hud& hud, float remaining_s, glm::vec2 vp) const {
    if (remaining_s <= 0.0f || vp.x <= 0.0f || vp.y <= 0.0f) return;
    const float alpha = glm::smoothstep(0.0f, 1.0f, remaining_s);
    const float centre = vp.x * 0.5f;
    const float top = vp.y * 0.29f;
    const float height = std::min(92.0f, vp.x * 0.12f);
    const float half = hud.measure_title_text("ARRESTED", height) * 0.5f + 54.0f;
    hud.quad({centre-half-14.0f, top}, {centre+half, top},
             {centre+half+14.0f, top+height+48.0f},
             {centre-half, top+height+48.0f}, {0.012f,0.025f,0.04f,0.84f*alpha});
    hud.rect({centre-half+26.0f, top+height+21.0f},
             {centre+half-26.0f, top+height+26.0f}, {0.3f,0.66f,0.86f,alpha});
    hud.title_text_centered("ARRESTED", centre+4.0f, top+15.0f, height,
                            {0.0f,0.0f,0.0f,0.9f*alpha});
    hud.title_text_centered("ARRESTED", centre, top+11.0f, height,
                            {0.88f,0.94f,0.98f,alpha});
}

void GameUi::draw_dev_menu(Hud& hud, const DevMenu& menu,
                           glm::vec2 viewport_px) const {
    if (menu.open()) draw_dev_rows(hud, menu, viewport_px);
}

}  // namespace apricot
