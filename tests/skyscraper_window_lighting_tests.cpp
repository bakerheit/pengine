#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

#include "city/construction_expansion.h"
#include "city/neighborhood_towers.h"
#include "city/skyscraper_window_lighting.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool same_light(const city::SkyscraperWindowLight& a,
                const city::SkyscraperWindowLight& b) {
    return a.lit == b.lit && a.emissive_alpha == b.emissive_alpha &&
           a.tint.x == b.tint.x && a.tint.y == b.tint.y &&
           a.tint.z == b.tint.z;
}

bool same_tint(const glm::vec3& a, const glm::vec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool approved_building_tint(const glm::vec3& tint) {
    const float hi = std::max({tint.x, tint.y, tint.z});
    const float lo = std::min({tint.x, tint.y, tint.z});
    const bool whiteish = hi - lo <= 0.08f;
    const bool yellowish = tint.x >= tint.z + 0.08f &&
                           tint.y >= tint.z + 0.06f &&
                           std::fabs(tint.x - tint.y) <= 0.10f;
    const bool not_blue_or_orange = tint.z <= std::max(tint.x, tint.y) &&
                                    tint.y >= tint.x - 0.10f;
    return (whiteish || yellowish) && not_blue_or_orange;
}

city::SkyscraperWindowAddress address(uint64_t index) {
    return {1000u + index / 240u, (index / 28u) % 36u, index % 28u,
            static_cast<city::SkyscraperUse>(index % 3u)};
}

void schedule_is_pure_and_order_independent() {
    constexpr uint64_t seed = 0x5e55f10a37b1d905ull;
    constexpr std::array<uint64_t, 8> steps{{
        0u, 1u, 4799u, 720000u, 19037u, 120u, 991337u, 4800u,
    }};
    std::array<std::array<city::SkyscraperWindowLight, steps.size()>, 40>
        expected{};
    for (std::size_t window = 0; window < expected.size(); ++window)
        for (std::size_t sample = 0; sample < steps.size(); ++sample)
            expected[window][sample] = city::skyscraper_window_light(
                address(window), seed, steps[sample], 20.5f / 24.0f, 0.86f);

    // Query in the opposite order. A global RNG or accumulator makes this
    // fail even if two consecutive calls happen to agree.
    for (std::size_t reverse_window = expected.size(); reverse_window-- > 0;)
        for (std::size_t reverse_sample = steps.size(); reverse_sample-- > 0;) {
            const auto got = city::skyscraper_window_light(
                address(reverse_window), seed, steps[reverse_sample],
                20.5f / 24.0f, 0.86f);
            REQUIRE(same_light(got,
                               expected[reverse_window][reverse_sample]));
        }
    apricot_test::pass(
        "window schedule is pure and independent of evaluation order");
}

void seed_and_full_address_change_the_pattern() {
    constexpr uint64_t seed_a = 0x91c637a483f2eb5dull;
    constexpr uint64_t seed_b = 0xa84b159c761b34deull;
    std::size_t seed_differences = 0;
    std::size_t address_differences = 0;
    std::size_t shared_lit_samples = 0;
    for (uint64_t i = 0; i < 320u; ++i) {
        const auto a = address(i);
        const city::SkyscraperWindowAddress shifted{
            a.building_key + 17u, a.floor_key + 5u, a.window_key + 11u,
            a.use};
        const auto baseline = city::skyscraper_window_light(
            a, seed_a, 393120u, 20.0f / 24.0f, 1.0f);
        const auto other_seed = city::skyscraper_window_light(
            a, seed_b, 393120u, 20.0f / 24.0f, 1.0f);
        const auto other_address = city::skyscraper_window_light(
            shifted, seed_a, 393120u, 20.0f / 24.0f, 1.0f);
        seed_differences += !same_light(baseline, other_seed);
        address_differences += !same_light(baseline, other_address);
        if (baseline.lit && other_seed.lit) {
            REQUIRE(same_tint(baseline.tint, other_seed.tint));
            ++shared_lit_samples;
        }
    }
    REQUIRE(seed_differences > 160u);
    REQUIRE(address_differences > 160u);
    REQUIRE(shared_lit_samples > 40u);
    std::printf("      divergent suites: seed=%zu address=%zu, shared-color=%zu\n",
                seed_differences, address_differences,
                shared_lit_samples);
    apricot_test::pass(
        "session seed changes occupancy but not a building's permanent color");
}

void every_building_has_one_permanent_color() {
    std::set<std::array<int, 3>> colors;
    std::size_t buildings_with_mixed_window_states = 0;
    for (uint64_t building = 1000u; building < 1064u; ++building) {
        const glm::vec3 expected =
            city::skyscraper_building_window_tint(building);
        REQUIRE(approved_building_tint(expected));
        colors.insert({static_cast<int>(std::lround(expected.x * 100.0f)),
                       static_cast<int>(std::lround(expected.y * 100.0f)),
                       static_cast<int>(std::lround(expected.z * 100.0f))});
        std::size_t lit_samples = 0;
        for (uint64_t seed : {17u, 491u, 9001u}) {
            for (uint64_t suite = 0; suite < 48u; ++suite) {
                const city::SkyscraperWindowAddress pane{
                    building, suite / 12u, suite,
                    city::SkyscraperUse::Mixed};
                for (uint64_t second : {0u, 31u, 79u, 143u}) {
                    const auto light = city::skyscraper_window_light(
                        pane, seed, second * 120u, 20.0f / 24.0f, 1.0f);
                    if (!light.lit) continue;
                    REQUIRE(same_tint(light.tint, expected));
                    ++lit_samples;
                }
            }
        }
        REQUIRE(lit_samples > 0u);

        std::size_t lit_now = 0;
        for (uint64_t suite = 0; suite < 48u; ++suite) {
            const city::SkyscraperWindowAddress pane{
                building, suite / 12u, suite,
                city::SkyscraperUse::Mixed};
            lit_now += city::skyscraper_window_light(
                           pane, 17u, 31u * 120u, 20.0f / 24.0f, 1.0f)
                           .lit;
        }
        buildings_with_mixed_window_states +=
            lit_now > 0u && lit_now < 48u;
    }
    REQUIRE(colors.size() == 5u);
    REQUIRE(buildings_with_mixed_window_states == 64u);
    apricot_test::pass(
        "each building keeps one yellowish/whiteish shade while suites vary");
}

void occupancy_profiles_follow_how_buildings_are_used() {
    const float office_dinner = city::skyscraper_occupancy_probability(
        city::SkyscraperUse::Office, 18.0f / 24.0f);
    const float office_midnight = city::skyscraper_occupancy_probability(
        city::SkyscraperUse::Office, 0.0f);
    const float office_predawn = city::skyscraper_occupancy_probability(
        city::SkyscraperUse::Office, 4.0f / 24.0f);
    const float homes_evening = city::skyscraper_occupancy_probability(
        city::SkyscraperUse::Residential, 20.0f / 24.0f);
    const float homes_midnight = city::skyscraper_occupancy_probability(
        city::SkyscraperUse::Residential, 0.0f);
    const float homes_predawn = city::skyscraper_occupancy_probability(
        city::SkyscraperUse::Residential, 4.0f / 24.0f);
    REQUIRE(office_dinner > office_midnight * 5.0f);
    REQUIRE(office_midnight > office_predawn);
    REQUIRE(homes_evening > homes_midnight);
    REQUIRE(homes_midnight > homes_predawn * 4.0f);
    REQUIRE(homes_evening > city::skyscraper_occupancy_probability(
                                city::SkyscraperUse::Office,
                                20.0f / 24.0f));

    for (int hour = -24; hour <= 48; ++hour) {
        for (const auto use : {city::SkyscraperUse::Office,
                               city::SkyscraperUse::Residential,
                               city::SkyscraperUse::Mixed}) {
            const float p = city::skyscraper_occupancy_probability(
                use, static_cast<float>(hour) / 24.0f);
            REQUIRE(p >= 0.0f && p <= 1.0f);
        }
    }
    REQUIRE_NEAR(city::skyscraper_occupancy_probability(
                     city::SkyscraperUse::Mixed, -0.10f),
                 city::skyscraper_occupancy_probability(
                     city::SkyscraperUse::Mixed, 0.90f),
                 1e-6);
    apricot_test::pass(
        "office, residential and mixed towers keep different nightly profiles");
}

void daylight_is_dark_and_twilight_only_adds_light() {
    constexpr uint64_t seed = 0x40c7fbc4a53be98aull;
    constexpr std::array<float, 8> darkness_levels{{
        0.0f, 0.04f, 0.10f, 0.18f, 0.28f, 0.45f, 0.72f, 1.0f,
    }};
    std::size_t occupied_windows = 0;
    std::size_t gradual_windows = 0;
    for (uint64_t i = 0; i < 256u; ++i) {
        bool previously_lit = false;
        float previous_alpha = 1.0f;
        bool saw_unlit_twilight = false;
        bool saw_lit_twilight = false;
        for (const float darkness : darkness_levels) {
            const auto light = city::skyscraper_window_light(
                address(i), seed, 770113u, 20.0f / 24.0f, darkness);
            if (darkness == 0.0f) {
                REQUIRE(!light.lit);
                REQUIRE(light.emissive_alpha == 1.0f);
            }
            if (previously_lit) REQUIRE(light.lit);
            REQUIRE(light.emissive_alpha >= previous_alpha);
            if (!light.lit) REQUIRE(light.emissive_alpha == 1.0f);
            if (darkness > 0.0f && darkness < 0.45f) {
                saw_unlit_twilight = saw_unlit_twilight || !light.lit;
                saw_lit_twilight = saw_lit_twilight || light.lit;
            }
            previously_lit = light.lit;
            previous_alpha = light.emissive_alpha;
        }
        const auto night = city::skyscraper_window_light(
            address(i), seed, 770113u, 20.0f / 24.0f, 1.0f);
        occupied_windows += night.lit;
        gradual_windows += night.lit && saw_unlit_twilight && saw_lit_twilight;
    }
    REQUIRE(occupied_windows > 100u);
    REQUIRE(gradual_windows > 60u);

    const auto invalid_darkness = city::skyscraper_window_light(
        address(7u), seed, 770113u, 20.0f / 24.0f,
        std::numeric_limits<float>::quiet_NaN());
    REQUIRE(!invalid_darkness.lit);
    REQUIRE(invalid_darkness.emissive_alpha == 1.0f);
    apricot_test::pass(
        "day emits nothing and twilight reveals each occupied suite monotonically");
}

void fixed_midnight_still_evolves_without_flicker_or_facade_beats() {
    constexpr uint64_t seed = 0x28d7dc5c22f11b61ull;
    constexpr std::size_t kWindowCount = 300u;
    std::array<bool, kWindowCount> previous{};
    std::array<int, kWindowCount> last_transition{};
    std::array<int, kWindowCount> transition_count{};
    std::set<int> transition_seconds;
    std::size_t windows_that_changed = 0;
    std::size_t windows_with_multiple_changes = 0;
    int busiest_second = 0;

    for (std::size_t i = 0; i < kWindowCount; ++i)
        previous[i] = city::skyscraper_window_light(
                          address(i), seed, 0u, 0.0f, 1.0f)
                          .lit;

    for (int second = 1; second <= 900; ++second) {
        int changes_this_second = 0;
        for (std::size_t i = 0; i < kWindowCount; ++i) {
            const bool lit = city::skyscraper_window_light(
                                 address(i), seed,
                                 static_cast<uint64_t>(second) * 120u, 0.0f,
                                 1.0f)
                                 .lit;
            if (lit != previous[i]) {
                if (transition_count[i] > 0)
                    REQUIRE(second - last_transition[i] >=
                            static_cast<int>(
                                city::kSkyscraperWindowMinDwellSeconds));
                if (transition_count[i] == 0) ++windows_that_changed;
                if (transition_count[i] == 1) ++windows_with_multiple_changes;
                ++transition_count[i];
                last_transition[i] = second;
                transition_seconds.insert(second);
                ++changes_this_second;
            }
            previous[i] = lit;
        }
        busiest_second = std::max(busiest_second, changes_this_second);
    }

    REQUIRE(windows_that_changed > 200u);
    REQUIRE(windows_with_multiple_changes > 120u);
    REQUIRE(transition_seconds.size() > 160u);
    REQUIRE(busiest_second < 18);
    std::printf(
        "      fixed midnight: %zu/%zu suites changed across %zu distinct seconds; busiest=%d\n",
        windows_that_changed, kWindowCount, transition_seconds.size(),
        busiest_second);
    apricot_test::pass(
        "fixed midnight evolves on independent suite clocks with no fast flicker");
}

void moving_sky_does_not_break_suite_dwell() {
    constexpr uint64_t seed = 0x7ad1b4932f6c805eull;
    constexpr std::size_t kWindowCount = 420u;
    constexpr float kSkySpeed = 5.0f;
    constexpr float kSecondsPerDay = 1200.0f;
    constexpr float kStepsPerSecond = 120.0f;
    constexpr float kTimePerStep =
        kSkySpeed / (kSecondsPerDay * kStepsPerSecond);
    std::array<bool, kWindowCount> previous{};
    std::array<int, kWindowCount> last_transition{};
    std::array<int, kWindowCount> transition_count{};
    std::size_t transitions = 0;

    for (std::size_t i = 0; i < kWindowCount; ++i)
        previous[i] = city::skyscraper_window_light(
                          address(i), seed, 0u, 0.28f, 1.0f, kTimePerStep)
                          .lit;

    for (int second = 1; second <= 480; ++second) {
        const auto step = static_cast<uint64_t>(second) * 120u;
        const float time_of_day =
            0.28f + static_cast<float>(second) * kSkySpeed / kSecondsPerDay;
        for (std::size_t i = 0; i < kWindowCount; ++i) {
            const bool lit = city::skyscraper_window_light(
                                 address(i), seed, step, time_of_day, 1.0f,
                                 kTimePerStep)
                                 .lit;
            if (lit != previous[i]) {
                if (transition_count[i] > 0)
                    REQUIRE(second - last_transition[i] >=
                            static_cast<int>(
                                city::kSkyscraperWindowMinDwellSeconds));
                ++transition_count[i];
                last_transition[i] = second;
                ++transitions;
            }
            previous[i] = lit;
        }
    }
    REQUIRE(transitions > 300u);
    apricot_test::pass(
        "moving day clock preserves every suite's minimum dwell");
}

void distance_lod_handoff_is_static_continuous_and_hysteretic() {
    constexpr uint64_t seed = 0xd76c1d4a6853b92full;
    constexpr std::size_t kWindowCount = 320u;
    constexpr float near_distance =
        city::kSkyscraperWindowDynamicEnterDistanceM - 1.0f;
    constexpr float far_distance =
        city::kSkyscraperWindowDynamicExitDistanceM + 1.0f;
    constexpr float hysteresis_distance =
        (city::kSkyscraperWindowDynamicEnterDistanceM +
         city::kSkyscraperWindowDynamicExitDistanceM) * 0.5f;
    REQUIRE(city::skyscraper_window_dynamic_lod(near_distance, false));
    REQUIRE(!city::skyscraper_window_dynamic_lod(far_distance, true));
    REQUIRE(!city::skyscraper_window_dynamic_lod(hysteresis_distance, false));
    REQUIRE(city::skyscraper_window_dynamic_lod(hysteresis_distance, true));
    REQUIRE(!city::skyscraper_window_dynamic_lod(-1.0f, true));
    REQUIRE(!city::skyscraper_window_dynamic_lod(
        std::numeric_limits<float>::quiet_NaN(), true));

    std::array<city::SkyscraperWindowLodState, kWindowCount> states{};
    std::array<city::SkyscraperWindowLight, kWindowCount> static_pattern{};
    std::array<city::SkyscraperWindowLight, kWindowCount> updated_pattern{};
    std::size_t lit_windows = 0;
    std::size_t unlit_windows = 0;
    for (std::size_t i = 0; i < kWindowCount; ++i) {
        static_pattern[i] = city::skyscraper_window_lod_light(
            states[i], address(i), seed, 0u, 18.0f / 24.0f, 1.0f,
            far_distance);
        REQUIRE(!states[i].dynamic);
        lit_windows += static_pattern[i].lit;
        unlit_windows += !static_pattern[i].lit;

        const auto far_later = city::skyscraper_window_lod_light(
            states[i], address(i), seed, 240000u, 3.0f / 24.0f, 1.0f,
            far_distance, 0.0001f);
        REQUIRE(same_light(far_later, static_pattern[i]));
        const auto far_inside_band = city::skyscraper_window_lod_light(
            states[i], address(i), seed, 240120u, 3.0f / 24.0f, 1.0f,
            hysteresis_distance, 0.0001f);
        REQUIRE(!states[i].dynamic);
        REQUIRE(same_light(far_inside_band, static_pattern[i]));

        const auto near_loaded = city::skyscraper_window_lod_light(
            states[i], address(i), seed, 240240u, 3.0f / 24.0f, 1.0f,
            near_distance, 0.0001f);
        REQUIRE(states[i].dynamic);
        REQUIRE(same_light(near_loaded, static_pattern[i]));
        REQUIRE(states[i].next_dynamic_update_step > 240240u);

        const uint64_t before_first_update =
            states[i].next_dynamic_update_step - 1u;
        const auto near_held = city::skyscraper_window_lod_light(
            states[i], address(i), seed, before_first_update,
            23.0f / 24.0f, 1.0f, near_distance, 0.0001f);
        REQUIRE(same_light(near_held, static_pattern[i]));

        updated_pattern[i] = city::skyscraper_window_lod_light(
            states[i], address(i), seed,
            240240u + city::kSkyscraperWindowMaxDwellSeconds * 120u,
            23.0f / 24.0f, 1.0f, near_distance, 0.0001f);
    }
    REQUIRE(lit_windows > 80u);
    REQUIRE(unlit_windows > 80u);

    std::size_t updated_windows = 0;
    for (std::size_t i = 0; i < kWindowCount; ++i) {
        updated_windows += !same_light(updated_pattern[i], static_pattern[i]);
        const uint64_t exit_step =
            240360u + city::kSkyscraperWindowMaxDwellSeconds * 120u;
        const auto far_saved = city::skyscraper_window_lod_light(
            states[i], address(i), seed, exit_step, 23.0f / 24.0f, 1.0f,
            far_distance, 0.0001f);
        REQUIRE(!states[i].dynamic);
        REQUIRE(same_light(far_saved, updated_pattern[i]));

        const auto saved_later = city::skyscraper_window_lod_light(
            states[i], address(i), seed, exit_step + 900000u,
            1.0f / 24.0f, 1.0f, far_distance, 0.0001f);
        REQUIRE(same_light(saved_later, updated_pattern[i]));
        const auto saved_in_band = city::skyscraper_window_lod_light(
            states[i], address(i), seed, exit_step + 900120u,
            1.0f / 24.0f, 1.0f, hysteresis_distance, 0.0001f);
        REQUIRE(!states[i].dynamic);
        REQUIRE(same_light(saved_in_band, updated_pattern[i]));

        const auto near_reloaded = city::skyscraper_window_lod_light(
            states[i], address(i), seed, exit_step + 900240u,
            1.0f / 24.0f, 1.0f, near_distance, 0.0001f);
        REQUIRE(states[i].dynamic);
        REQUIRE(same_light(near_reloaded, updated_pattern[i]));
        const bool saved_occupancy = states[i].occupied;
        const auto daylight = city::skyscraper_window_lod_light(
            states[i], address(i), seed, exit_step + 900360u,
            12.0f / 24.0f, 0.0f, near_distance, 0.0001f);
        REQUIRE(!daylight.lit);
        REQUIRE(daylight.emissive_alpha == 1.0f);
        REQUIRE(states[i].occupied == saved_occupancy);
    }
    REQUIRE(updated_windows > 80u);
    apricot_test::pass(
        "window LOD hands off exact patterns and resists boundary thrashing");
}

void authored_skyscrapers_expose_one_light_per_window_bay() {
    constexpr std::array<city::SkyscraperUse, 17> expected_uses{{
        city::SkyscraperUse::Office, city::SkyscraperUse::Office,
        city::SkyscraperUse::Residential, city::SkyscraperUse::Mixed,
        city::SkyscraperUse::Office, city::SkyscraperUse::Office,
        city::SkyscraperUse::Mixed, city::SkyscraperUse::Residential,
        city::SkyscraperUse::Mixed, city::SkyscraperUse::Office,
        city::SkyscraperUse::Mixed,
        city::SkyscraperUse::Residential, city::SkyscraperUse::Office,
        city::SkyscraperUse::Mixed, city::SkyscraperUse::Residential,
        city::SkyscraperUse::Office, city::SkyscraperUse::Mixed,
    }};
    std::size_t neighborhood_lights = 0;
    for (std::size_t i = 0; i < city::kNeighborhoodTowers.size(); ++i) {
        REQUIRE(city::kNeighborhoodTowers[i].use == expected_uses[i]);
        const auto parts = city::bake_neighborhood_tower(i);
        std::size_t lights = 0;
        for (const auto& part : parts) {
            if (std::strcmp(part.name, "tower office window light") != 0)
                continue;
            ++lights;
            REQUIRE(part.finish == city::BuildingFinish::Glass);
            REQUIRE(!part.solid);
        }
        const std::size_t expected = static_cast<std::size_t>(
            city::kNeighborhoodTowers[i].floors *
            city::kTowerWindowsPerFloor);
        REQUIRE(lights == expected);
        neighborhood_lights += lights;
    }

    std::size_t twin_lights = 0;
    constexpr std::array<int, 2> twin_floor_totals{{24 + 21, 28 + 25}};
    for (std::size_t i = 0; i < city::kTwinSkyscraperBlockSites.size(); ++i) {
        const auto parts = city::bake_twin_skyscraper_block(i);
        std::size_t lights = 0;
        for (const auto& part : parts) {
            if (std::strcmp(part.name,
                            "twin tower office window light") != 0)
                continue;
            ++lights;
            REQUIRE(part.finish == city::StartFinish::Glass);
            REQUIRE(!part.solid);
        }
        REQUIRE(lights ==
                static_cast<std::size_t>(twin_floor_totals[i] * 20));
        twin_lights += lights;
    }
    REQUIRE(neighborhood_lights == 12124u);
    REQUIRE(twin_lights == 1960u);
    std::printf("      authored dynamic panes: neighborhood=%zu twins=%zu\n",
                neighborhood_lights, twin_lights);
    apricot_test::pass(
        "every completed skyscraper has independent suite-scale light panes");
}

}  // namespace

int main() {
    schedule_is_pure_and_order_independent();
    seed_and_full_address_change_the_pattern();
    every_building_has_one_permanent_color();
    occupancy_profiles_follow_how_buildings_are_used();
    daylight_is_dark_and_twilight_only_adds_light();
    fixed_midnight_still_evolves_without_flicker_or_facade_beats();
    moving_sky_does_not_break_suite_dwell();
    distance_lod_handoff_is_static_continuous_and_hysteretic();
    authored_skyscrapers_expose_one_light_per_window_bay();
    return apricot_test::done("skyscraper_window_lighting_tests");
}
