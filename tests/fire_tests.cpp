// game/fire.h — the ground a molotov sets alight.
//
// The four properties worth pinning are the four that would each ship a
// different disaster: the fire must be BOUNDED (it cannot eat the city or the
// frame), it must be REPRODUCIBLE (a replayed tape burns the fire it
// recorded), it must GO OUT, and it must not walk through geometry it has no
// business crossing. Everything else here is in service of those.

#include <cmath>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

#include "game/fire.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// A flat, endless, flammable world. The simplest ground a fire can have, and
// the one that makes the spread numbers below mean only what the spread rule
// says they mean.
auto flat_ground(float height = 0.0f) {
    return [height](float, float, float) {
        FireGround out;
        out.supported = true;
        out.height_m = height;
        return out;
    };
}

// Step a field for `seconds` at the real sim rate, so a test that says a fire
// is out after twenty seconds is describing twenty seconds of play.
constexpr float kStep = 1.0f / 120.0f;

template <typename GroundFn>
void burn(FireField& fire, float seconds, GroundFn&& ground) {
    const int steps = static_cast<int>(seconds / kStep);
    for (int i = 0; i < steps; ++i) fire.step(kStep, ground);
}

void an_unlit_field_is_quiet() {
    FireField fire;
    REQUIRE(!fire.burning());
    REQUIRE(fire.live_count() == 0);
    REQUIRE(fire.intensity() == 0.0f);
    REQUIRE(fire.heat_at({0.0f, 0.0f, 0.0f}) == 0.0f);
    // Stepping an empty field cannot invent one.
    burn(fire, 5.0f, flat_ground());
    REQUIRE(!fire.burning());
    apricot_test::pass("an unlit field is quiet");
}

void ignition_lights_exactly_one_cell() {
    FireField fire;
    REQUIRE(fire.ignite({12.0f, 3.0f, -7.0f}, 3.0f, 1));
    REQUIRE(fire.live_count() == 1);
    // A second bottle into the same patch adds nothing: the flames are already
    // there, and a second set of geometry standing in them is all it would buy.
    REQUIRE(!fire.ignite({12.0f, 3.0f, -7.0f}, 3.0f, 2));
    REQUIRE(fire.live_count() == 1);
    // A bottle a cell away IS a second fire, though. The grid is 1.1 m, so
    // "the same patch" is a cell and not a radius somebody has to remember.
    REQUIRE(fire.ignite({13.5f, 3.0f, -7.0f}, 3.0f, 3));
    REQUIRE(fire.live_count() == 2);
    // Rubbish in changes nothing rather than corrupting the field.
    REQUIRE(!fire.ignite({std::nanf(""), 0.0f, 0.0f}, 0.0f, 4));
    REQUIRE(!fire.ignite({40.0f, 0.0f, 40.0f}, std::nanf(""), 5));
    REQUIRE(fire.live_count() == 2);
    apricot_test::pass("ignition lights exactly one cell");
}

void the_fire_spreads_and_then_stops_spreading() {
    FireField fire;
    REQUIRE(fire.ignite({0.0f, 0.0f, 0.0f}, 0.0f, 0xABCDEF));

    // Nothing catches before the seed cell is properly alight. Without this the
    // whole radius lights on the first step and the fire reads as a decal.
    burn(fire, FireField::kSpreadDelayS * 0.9f, flat_ground());
    REQUIRE_MSG(fire.live_count() == 1, "spread began before the delay",
                "pre-delay");

    burn(fire, 1.0f, flat_ground());
    const std::size_t after_delay = fire.live_count();
    REQUIRE_MSG(after_delay > 1, "the fire never spread", "post-delay");

    // Bounded, in both senses: it never exceeds the pool, and every burning
    // cell stays inside the authored radius of the bottle that lit it.
    burn(fire, 6.0f, flat_ground());
    REQUIRE(fire.live_count() <= FireField::kCapacity);
    const float limit = FireField::kSpreadRadiusM + FireField::kCellSizeM;
    for (const auto& cell : fire.cells()) {
        if (!cell.live) continue;
        const float dx = static_cast<float>(cell.gx - cell.origin_gx) * FireField::kCellSizeM;
        const float dz = static_cast<float>(cell.gz - cell.origin_gz) * FireField::kCellSizeM;
        REQUIRE_MSG(std::sqrt(dx * dx + dz * dz) <= limit,
                    "a cell burned outside the spread radius", "radius");
    }
    apricot_test::pass("the fire spreads and then stops spreading");
}

void every_fire_goes_out() {
    FireField fire;
    REQUIRE(fire.ignite({0.0f, 0.0f, 0.0f}, 0.0f, 7));
    // Comfortably past the longest chain the delay and the lifetime allow.
    burn(fire, 90.0f, flat_ground());
    REQUIRE_MSG(!fire.burning(), "a fire was still alight after 90 seconds",
                "burnout");
    REQUIRE(fire.live_count() == 0);
    REQUIRE(fire.intensity() == 0.0f);
    apricot_test::pass("every fire goes out");
}

// THE REPLAY PROPERTY. Two fields lit at the same place with the same id burn
// the same shape, cell for cell and second for second, however many other
// fires have been lit in between.
void the_same_bottle_burns_the_same_fire() {
    const auto shape = [](uint64_t id) {
        FireField fire;
        fire.ignite({4.5f, 0.0f, -2.5f}, 0.0f, id);
        std::vector<std::pair<int32_t, int32_t>> live;
        for (int i = 0; i < 4; ++i) {
            burn(fire, 1.5f, flat_ground());
            for (const auto& cell : fire.cells())
                if (cell.live) live.push_back({cell.gx, cell.gz});
        }
        return live;
    };
    REQUIRE(shape(99) == shape(99));
    // And a different bottle burns a different shape, or the id is decoration.
    REQUIRE(shape(99) != shape(100));
    apricot_test::pass("the same bottle burns the same fire");
}

void flames_do_not_climb_walls_or_cross_water() {
    // Ground that steps up hard two metres east of the bottle: a wall.
    FireField wall_fire;
    REQUIRE(wall_fire.ignite({0.0f, 0.0f, 0.0f}, 0.0f, 11));
    burn(wall_fire, 12.0f, [](float x, float, float) {
        FireGround out;
        out.supported = true;
        out.height_m = x > 2.0f ? 6.0f : 0.0f;
        return out;
    });
    for (const auto& cell : wall_fire.cells()) {
        if (!cell.live) continue;
        REQUIRE_MSG(cell.ground_y < 1.0f, "the fire climbed a six-metre wall",
                    "wall");
    }

    // Ground that simply is not there. Nothing catches at all.
    FireField void_fire;
    REQUIRE(void_fire.ignite({0.0f, 0.0f, 0.0f}, 0.0f, 12));
    burn(void_fire, 3.0f, [](float, float, float) { return FireGround{}; });
    REQUIRE_MSG(void_fire.live_count() == 1,
                "the fire spread onto unsupported ground", "unsupported");
    apricot_test::pass("flames do not climb walls or cross water");
}

void heat_is_local_and_low() {
    FireField fire;
    REQUIRE(fire.ignite({20.0f, 5.0f, 20.0f}, 5.0f, 21));
    burn(fire, 2.0f, flat_ground(5.0f));

    // Standing in it.
    const glm::vec3 centre = fire.centre();
    REQUIRE_MSG(fire.heat_at(centre) > 0.0f, "no heat at the centre of a fire",
                "centre");
    // Standing well clear of it.
    REQUIRE(fire.heat_at(centre + glm::vec3{40.0f, 0.0f, 0.0f}) == 0.0f);
    // On the roof above it, and in the basement below it.
    REQUIRE(fire.heat_at(centre + glm::vec3{0.0f, 6.0f, 0.0f}) == 0.0f);
    REQUIRE(fire.heat_at(centre + glm::vec3{0.0f, -4.0f, 0.0f}) == 0.0f);
    // Rubbish in, zero out — never a NaN propagated into the player's health.
    REQUIRE(fire.heat_at({std::nanf(""), 0.0f, 0.0f}) == 0.0f);
    apricot_test::pass("heat is local and low");
}

void intensity_and_centre_follow_the_burn() {
    FireField fire;
    REQUIRE(fire.ignite({0.0f, 0.0f, 0.0f}, 0.0f, 31));
    burn(fire, 0.5f, flat_ground());
    const float young = fire.intensity();
    burn(fire, 3.0f, flat_ground());
    const float grown = fire.intensity();
    REQUIRE_MSG(grown > young, "a spreading fire did not get louder", "growth");
    REQUIRE_MSG(grown <= 1.0f, "intensity ran past full scale", "saturation");

    // The centroid stays inside the fire it describes, which is the only
    // property the audio emitter actually needs from it.
    const glm::vec3 centre = fire.centre();
    REQUIRE(std::isfinite(centre.x) && std::isfinite(centre.z));
    REQUIRE(std::fabs(centre.x) <= FireField::kSpreadRadiusM);
    REQUIRE(std::fabs(centre.z) <= FireField::kSpreadRadiusM);

    burn(fire, 90.0f, flat_ground());
    REQUIRE(fire.intensity() == 0.0f);
    // An unlit field reports the origin rather than dividing by no weight.
    REQUIRE(fire.centre() == glm::vec3{0.0f});
    apricot_test::pass("intensity and centre follow the burn");
}

void a_bad_step_is_refused_not_absorbed() {
    FireField fire;
    REQUIRE(fire.ignite({0.0f, 0.0f, 0.0f}, 0.0f, 41));
    const float before = fire.draw(0).age;
    fire.step(0.0f, flat_ground());
    fire.step(-1.0f, flat_ground());
    fire.step(std::nanf(""), flat_ground());
    REQUIRE(fire.draw(0).age == before);
    REQUIRE(fire.live_count() == 1);
    apricot_test::pass("a bad step is refused, not absorbed");
}

void draw_reports_only_live_cells() {
    FireField fire;
    REQUIRE(fire.ignite({0.0f, 0.0f, 0.0f}, 0.0f, 51));
    burn(fire, 3.0f, flat_ground());
    std::size_t visible = 0;
    std::set<std::pair<int32_t, int32_t>> seen;
    for (std::size_t i = 0; i < FireField::kCapacity; ++i) {
        const auto draw = fire.draw(i);
        if (!draw.visible) continue;
        ++visible;
        REQUIRE(draw.heat > 0.0f && draw.heat <= 1.0f);
        REQUIRE(std::isfinite(draw.position.x) && std::isfinite(draw.position.y));
        // No two live cells occupy one patch of ground, or the flames double up.
        const auto key = std::make_pair(
            static_cast<int32_t>(std::floor(draw.position.x / FireField::kCellSizeM)),
            static_cast<int32_t>(std::floor(draw.position.z / FireField::kCellSizeM)));
        REQUIRE_MSG(seen.insert(key).second, "two live cells share a position",
                    "overlap");
    }
    REQUIRE(visible == fire.live_count());
    // Out of range is empty, not out of bounds.
    REQUIRE(!fire.draw(FireField::kCapacity).visible);
    REQUIRE(!fire.draw(FireField::kCapacity + 9000).visible);
    apricot_test::pass("draw reports only live cells");
}

void clear_puts_everything_out() {
    FireField fire;
    REQUIRE(fire.ignite({0.0f, 0.0f, 0.0f}, 0.0f, 61));
    burn(fire, 3.0f, flat_ground());
    REQUIRE(fire.burning());
    fire.clear();
    REQUIRE(!fire.burning());
    REQUIRE(fire.live_count() == 0);
    // And the same ground is free to be lit again afterwards.
    REQUIRE(fire.ignite({0.0f, 0.0f, 0.0f}, 0.0f, 62));
    apricot_test::pass("clear puts everything out");
}

}  // namespace

int main() {
    an_unlit_field_is_quiet();
    ignition_lights_exactly_one_cell();
    the_fire_spreads_and_then_stops_spreading();
    every_fire_goes_out();
    the_same_bottle_burns_the_same_fire();
    flames_do_not_climb_walls_or_cross_water();
    heat_is_local_and_low();
    intensity_and_centre_follow_the_burn();
    a_bad_step_is_refused_not_absorbed();
    draw_reports_only_live_cells();
    clear_puts_everything_out();
    return apricot_test::done("fire_tests");
}
