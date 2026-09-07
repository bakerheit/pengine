#include "game/minimap.h"
#include "game/ui_canvas.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    const glm::vec2 origin{100,200};
    for (const auto heading : {glm::vec3{0,0,-1}, glm::vec3{1,0,0},
                               glm::vec3{0,0,1}, glm::vec3{-1,0,0}, glm::vec3{1,0,-1}}) {
        const auto view = MinimapView::make({100,30,200}, heading, 0, {2560,1440});
        REQUIRE(glm::length(view.project(origin)-view.centre) < 1e-5f);
        const auto ahead = view.project(origin + view.forward * 120.0f) - view.centre;
        REQUIRE_NEAR(ahead.x, 0, 1e-4);
        REQUIRE_NEAR(ahead.y, -view.radius * 120.0f / view.range_m, 1e-4);
        const auto right = view.project(origin + glm::vec2{-view.forward.y,view.forward.x}*120.0f);
        REQUIRE_NEAR(right.x-view.centre.x, view.radius * 120.0f / view.range_m, 1e-4);
        for (const auto delta : {glm::vec2{10000,-5000}, glm::vec2{-6000,9000},
                                 glm::vec2{0,-8000}, glm::vec2{0,10000}}) {
            const auto offset = view.blip(origin + delta) - view.centre;
            REQUIRE_NEAR(glm::length(offset), view.radius-16, 1e-3);
            REQUIRE(glm::dot(glm::normalize(offset),glm::normalize(view.direction(delta))) > .99999f);
        }
        REQUIRE(view.blip(origin) == view.centre);
        REQUIRE(glm::length(view.blip(origin+view.forward*40.0f)-
                            view.project(origin+view.forward*40.0f)) < 1e-5f);
    }
    const auto parked = MinimapView::make({}, {0,0,-1}, 0, {1280,720});
    REQUIRE_NEAR(parked.range_m, 160, 1e-5);
    const auto fallback = MinimapView::make({}, {0,1,0}, 200, {1280,1440});
    REQUIRE(fallback.forward == glm::vec2(0,-1));
    REQUIRE_NEAR(fallback.range_m, 440, 1e-5);
    for (const auto size : {glm::vec2{1280,720}, glm::vec2{960,720},
                            glm::vec2{3440,1440}, glm::vec2{3200,2000}}) {
        const auto canvas = UiCanvas::from_drawable(size);
        const auto view = MinimapView::make({}, {0,0,-1}, 0, canvas.size);
        REQUIRE(view.centre.x-view.radius > 0);
        REQUIRE(view.centre.y-view.radius > 0);
        REQUIRE(view.centre.x+view.radius < canvas.size.x);
        REQUIRE(view.centre.y+view.radius+40 < canvas.size.y);
    }
    apricot_test::pass("radar rotation, bearing-preserving rim markers, range and aspect-safe placement");

    MapWaypoint waypoint;
    MapViewport viewport{30,120,1000,600,400,-200,1200};
    REQUIRE(waypoint.toggle({530,420}, viewport));
    REQUIRE(*waypoint.position == glm::vec2(400,-200));
    REQUIRE(waypoint.toggle({630,470}, viewport));
    REQUIRE(*waypoint.position == glm::vec2(600,-100));
    REQUIRE(!waypoint.toggle({0,0},viewport));
    REQUIRE(*waypoint.position == glm::vec2(600,-100));
    REQUIRE(waypoint.toggle({632,472},viewport));
    REQUIRE(!waypoint.position);
    viewport.world_center_x = city::kWorldHalfMetres;
    REQUIRE(!waypoint.toggle({900,420},viewport));
    REQUIRE(!waypoint.position);
    apricot_test::pass("waypoints use the visible atlas projection, replace, clear and reject chrome/outside-world clicks");

    RadarPolygon box;
    box.count=4;
    box.points[0]={-1000,-1000};box.points[1]={1000,-1000};
    box.points[2]={1000,1000};box.points[3]={-1000,1000};
    const auto disc = clip_radar_polygon(box, {}, 142);
    REQUIRE(disc.count == 48);
    for (int i=0;i<disc.count;++i)
        REQUIRE(glm::length(disc.points[static_cast<std::size_t>(i)]) <= 142.001f);
    float area=0;
    for (int i=0;i<disc.count;++i) {
        const auto a=disc.points[static_cast<std::size_t>(i)];
        const auto b=disc.points[static_cast<std::size_t>((i+1)%disc.count)];
        area += a.x*b.y-a.y*b.x;
    }
    REQUIRE_NEAR(area*.5f, 24.0f*142*142*std::sin(6.28318530718f/48), .1f);
    REQUIRE(clip_radar_polygon(box,{2000,2000},142).count == 0);
    box.points[0]={-5,-5};box.points[1]={5,-5};box.points[2]={5,5};box.points[3]={-5,5};
    REQUIRE(clip_radar_polygon(box,{},142).count == 4);
    apricot_test::pass("radar disc clips crossing geometry without leaking into the game view");
    return apricot_test::done("minimap_tests");
}
