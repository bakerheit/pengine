#include <cmath>
#include <limits>

#include "game/snow_clearance.h"
#include "physics/terrain_collider.h"
#include "road/ribbon.h"
#include "test_assert.h"

using namespace apricot;

namespace {

void footprints_are_local_and_height_aware() {
    SnowClearanceField field;
    REQUIRE(field.clear_segment({0, 10, 0}, {10, 11, 0}, 2, 0.8f));
    REQUIRE_NEAR(field.depth_at(5, 10.5f, 0, 0.8f), 0.008f, 1e-6);
    REQUIRE_NEAR(field.depth_at(5, 10.5f, 3, 0.8f), 0.8f, 1e-6);
    REQUIRE_NEAR(field.depth_at(5, 3, 0, 0.8f), 0.8f, 1e-6);
    REQUIRE_NEAR(field.depth_at(5, 20, 0, 0.8f), 0.8f, 1e-6);
    REQUIRE(!field.clear_segment({10, 11, 0}, {500, 11, 0}, 2, 0.8f));
    REQUIRE_NEAR(field.depth_at(250, 11, 0, 0.8f), 0.8f, 1e-6);
    REQUIRE(!field.clear_segment({0, 0, 0}, {1, 0, 0}, -1, 0.8f));
    REQUIRE(!field.clear_segment({0, 0, 0}, {1, 0, 0}, 2,
        std::numeric_limits<float>::quiet_NaN()));
    apricot_test::pass("clearance follows blade width, slope and grade without teleport streaks");
}

void snow_refills_and_thaw_forgets_tracks() {
    SnowClearanceField field;
    REQUIRE(field.clear_segment({0, 0, 0}, {10, 0, 0}, 2, 0.8f));
    field.advance(1, 0, 400, 0.8f);
    REQUIRE_NEAR(field.depth_at(5, 0, 0, 0.8f), 0.033f, 1e-6);
    // Melting also bounds every local result by today's smaller global pack.
    REQUIRE_NEAR(field.depth_at(5, 0, 0, 0.02f), 0.02f, 1e-6);
    field.advance(0, 1, 4000, 0.0f);
    REQUIRE(field.strips().empty());
    REQUIRE_NEAR(field.depth_at(5, 0, 0, 1), 1, 1e-6);
    apricot_test::pass("cleared snow reaccumulates and thaw removes stale footprints");
}

void fixed_step_updates_and_capacity_are_bounded() {
    SnowClearanceField ticks, elapsed;
    REQUIRE(ticks.clear_segment({0, 0, 0}, {10, 0, 0}, 2, 1));
    REQUIRE(elapsed.clear_segment({0, 0, 0}, {10, 0, 0}, 2, 1));
    for (int i = 0; i < 12000; ++i) ticks.advance(1, 0, 1.0f / 120, 1);
    elapsed.advance(1, 0, 100, 1);
    REQUIRE_NEAR(ticks.depth_at(5, 0, 0, 1), elapsed.depth_at(5, 0, 0, 1), 1e-7);
    SnowClearanceField merged;
    for (int i = 0; i < 1000; ++i) {
        const float x = static_cast<float>(i) * 0.01f;
        REQUIRE(merged.clear_segment({x, 0, 0}, {x + 0.01f, 0, 0}, 2, 1));
    }
    REQUIRE(merged.strips().size() == 1);
    for (std::size_t i = 0; i < SnowClearanceField::kMaxStrips + 10; ++i) {
        const float z = 100 + static_cast<float>(i) * 5;
        REQUIRE(merged.clear_segment({0, 0, z}, {10, 0, z}, 2, 1));
    }
    REQUIRE(merged.strips().size() == SnowClearanceField::kMaxStrips);
    REQUIRE_NEAR(merged.depth_at(5, 0, 0, 1), 1, 1e-6);
    apricot_test::pass("fixed-step snowfall retains precision and strips merge within a fixed budget");
}

void refill_targets_the_current_main_pack() {
    SnowClearanceField field;
    REQUIRE(field.clear_segment({0, 0, 0}, {10, 0, 0}, 2, 0.18f));
    // A deepening storm must not forget clearance at the old 18 cm level.
    field.advance(1, 0, 4000, 0.8f);
    REQUIRE(!field.strips().empty());
    REQUIRE_NEAR(field.depth_at(5, 0, 0, 0.8f), 0.258f, 1e-6);
    field.advance(1, 0, 10000, 0.8f);
    REQUIRE(field.strips().empty());
    REQUIRE_NEAR(field.depth_at(5, 0, 0, 0.8f), 0.8f, 1e-6);
    // Matching a pinned main depth retires the strip, so a later storm is
    // ordinary snow rather than an old clearance footprint coming back.
    REQUIRE_NEAR(field.depth_at(5, 0, 0, 1.2f), 1.2f, 1e-6);
    REQUIRE(field.clear_segment({0, 0, 0}, {10, 0, 0}, 2, 0.8f));
    field.advance(1, 0, 100, 0.8f);
    const float prior = field.depth_at(5, 0, 0, 0.8f);
    field.advance(0, 0, 100, 0.8f);
    REQUIRE(field.depth_at(5, 0, 0, 0.8f) < prior);
    field.advance(0, 0, 1, 0.001f);
    REQUIRE(field.strips().empty());
    apricot_test::pass("refill tracks rising, pinned and melting main snow without stale footprints");
}

void continuous_plowing_retains_longer_roads() {
    SnowClearanceField field;
    // 6 m/s at 120 Hz for 2 km: old 24 m strips needed at least84 slots.
    for (int i = 0; i < 40000; ++i) {
        const float x = static_cast<float>(i) * 0.05f;
        field.advance(1, 0, 1.0f / 120, 0.8f);
        REQUIRE(field.clear_segment({x, 0, 0}, {x + 0.05f, 0, 0}, 2, 0.8f));
    }
    REQUIRE(field.strips().size() <= 23);
    REQUIRE(field.depth_at(1, 0, 0, 0.8f) < 0.03f);
    // Long retained strips must not loosen the rejection of sudden jumps.
    REQUIRE(!field.clear_segment({2100, 0, 0}, {2150, 0, 0}, 2, 0.8f));
    apricot_test::pass("continuous blade travel retains 2 km in a small strip budget");
}

void collider_uses_local_raw_depth_on_selected_surface() {
    TerrainCollider collider(0xC0FFEEu);
    const float x = 91, z = -36;
    const float terrain_y = collider.height(x, z);
    const float road_y = terrain_y + 3;
    RoadCollision road;
    RoadCollisionTri first;
    first.geom = {{x - 5, road_y, z - 5}, {x + 5, road_y, z + 5},
                  {x + 5, road_y, z - 5}, {0, 1, 0}};
    first.layer = RoadLayer::Carriageway;
    first.material = Surface::Gravel;
    RoadCollisionTri second = first;
    second.geom = {{x - 5, road_y, z - 5}, {x - 5, road_y, z + 5},
                   {x + 5, road_y, z + 5}, {0, 1, 0}};
    road.triangles = {first, second};
    collider.set_road_collision(road);
    collider.set_snow_collision_depth(0.7f);
    SnowClearanceField field;
    collider.set_snow_clearance(&field, 0.8f);
    REQUIRE(field.clear_segment({x - 4, road_y, z}, {x + 4, road_y, z}, 1, 0.8f));
    auto hit = collider.probe_down({x, road_y + 2, z}, 4);
    REQUIRE(hit.hit && hit.road && !hit.prop);
    REQUIRE_NEAR(hit.point.y, road_y, 1e-5);
    REQUIRE_NEAR(hit.snow_depth_m, 0.008f, 1e-6);
    REQUIRE_NEAR(collider.height(x, z), terrain_y + 0.7f, 1e-5);
    const auto untouched = collider.probe_down({x, road_y + 2, z + 3}, 4);
    REQUIRE(untouched.road);
    REQUIRE_NEAR(untouched.point.y, road_y + 0.7f, 1e-5);
    field.advance(1, 0, 3200, 0.8f);
    hit = collider.probe_down({x, road_y + 2, z}, 4);
    REQUIRE_NEAR(hit.point.y, road_y + 0.108f, 1e-5);
    REQUIRE_NEAR(hit.snow_depth_m, 0.208f, 1e-6);
    collider.add_static_box({{x - 0.5f, road_y + 0.2f, z - 0.5f},
                              {x + 0.5f, road_y + 1, z + 0.5f}});
    hit = collider.probe_down({x, road_y + 2, z}, 4);
    REQUIRE(hit.prop);
    REQUIRE_NEAR(hit.point.y, road_y + 1, 1e-5);
    REQUIRE(hit.snow_depth_m == 0);
    collider.set_snow_clearance(nullptr, 0.8f);
    REQUIRE_NEAR(collider.height(x, z), terrain_y + 0.7f, 1e-5);
    apricot_test::pass("actual road contacts follow clearance and refill without clearing another grade or prop top");
}

void terrain_and_authored_ground_share_clearance() {
    TerrainCollider collider(0xC0FFEEu);
    const float x = 91, z = -36;
    const float terrain_y = collider.height(x, z);
    SnowClearanceField field;
    collider.set_snow_collision_depth(0.7f);
    collider.set_snow_clearance(&field, 0.8f);
    REQUIRE(field.clear_segment({x, terrain_y, z}, {x, terrain_y, z}, 2, 0.8f));
    REQUIRE_NEAR(collider.height(x, z), terrain_y, 1e-5);
    REQUIRE_NEAR(collider.probe_down({x, terrain_y + 2, z}, 3).snow_depth_m,
                 0.008f, 1e-6);
    const float slab_y = terrain_y + 3;
    collider.add_static_ground_rect({x, z}, slab_y, {4, 4}, 0);
    auto hit = collider.probe_down({x, slab_y + 2, z}, 3);
    REQUIRE_NEAR(hit.point.y, slab_y + 0.7f, 1e-5);
    REQUIRE(field.clear_segment({x - 2, slab_y, z}, {x + 2, slab_y, z}, 1, 0.8f));
    hit = collider.probe_down({x, slab_y + 2, z}, 3);
    REQUIRE(hit.hit && !hit.road && !hit.prop);
    REQUIRE_NEAR(hit.point.y, slab_y, 1e-5);
    REQUIRE_NEAR(hit.snow_depth_m, 0.008f, 1e-6);
    apricot_test::pass("terrain and authored ground use the same grade-aware local snow depth");
}

}  // namespace

int main() {
    footprints_are_local_and_height_aware();
    snow_refills_and_thaw_forgets_tracks();
    fixed_step_updates_and_capacity_are_bounded();
    refill_targets_the_current_main_pack();
    continuous_plowing_retains_longer_roads();
    collider_uses_local_raw_depth_on_selected_surface();
    terrain_and_authored_ground_share_clearance();
    return apricot_test::done("snow_clearance");
}
