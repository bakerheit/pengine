#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "city/roads.h"
#include "city/westmere_streetscape.h"
#include "test_assert.h"

using namespace apricot;

namespace {

struct PlaneGround {
    float base = 0.0f;
    float dx = 0.0f;
    float dz = 0.0f;

    GroundSampler sampler() const { return {&sample, this}; }

    static float sample(const void* raw, float x, float z) {
        const auto& plane = *static_cast<const PlaneGround*>(raw);
        return plane.base + plane.dx * x + plane.dz * z;
    }
};

std::size_t named_count(const std::vector<city::StartPart>& parts,
                        const char* name) {
    return static_cast<std::size_t>(
        std::count_if(parts.begin(), parts.end(), [&](const auto& part) {
            return std::strcmp(part.name, name) == 0;
        }));
}

std::vector<city::StartPart> all_parts(GroundSampler ground) {
    std::vector<city::StartPart> out;
    for (std::size_t i = 0; i < city::kWestmereStreetscapeSites.size(); ++i) {
        auto parts = city::bake_westmere_streetscape_site(i, ground);
        out.insert(out.end(), parts.begin(), parts.end());
    }
    return out;
}

bool same_part_except_bottom(const city::StartPart& a,
                             const city::StartPart& b) {
    return std::strcmp(a.name, b.name) == 0 && a.centre.x == b.centre.x &&
           a.centre.z == b.centre.z && a.width_m == b.width_m &&
           a.height_m == b.height_m && a.depth_m == b.depth_m &&
           a.finish == b.finish && a.solid == b.solid &&
           a.pitch_deg == b.pitch_deg && a.yaw_deg == b.yaw_deg &&
           a.roll_deg == b.roll_deg && a.shape == b.shape;
}

void stable_site_table_matches_westmere() {
    REQUIRE(city::kWestmereStreetscapeSites.size() == 14u);
    REQUIRE(city::kWestmereStreetscapeSites[0].kind ==
            city::WestmereStreetscapeKind::Entry);
    REQUIRE(city::kWestmereStreetscapeSites[1].kind ==
            city::WestmereStreetscapeKind::Community);
    for (std::size_t i = 0; i < city::kWestmereCourtGreens.size(); ++i) {
        const auto& record = city::kWestmereStreetscapeSites[i + 2];
        REQUIRE(record.kind == city::WestmereStreetscapeKind::Court);
        REQUIRE(record.variant == i);
        REQUIRE_NEAR(record.site.origin.x,
                     city::kWestmereCourtGreens[i].origin.x, 0.001f);
        REQUIRE_NEAR(record.site.origin.z,
                     city::kWestmereCourtGreens[i].origin.z, 0.001f);
    }
    for (std::size_t i = 0; i < city::kLuxuryEstates.size(); ++i) {
        const auto& record = city::kWestmereStreetscapeSites[i + 5];
        REQUIRE(record.kind == city::WestmereStreetscapeKind::Estate);
        REQUIRE(record.variant == i);
        REQUIRE_NEAR(record.site.origin.x,
                     city::kLuxuryEstates[i].site.origin.x, 0.001f);
        REQUIRE_NEAR(record.site.origin.z,
                     city::kLuxuryEstates[i].site.origin.z, 0.001f);
    }
    apricot_test::pass(
        "stable entry, common, court, and estate streetscape table");
}

void inventory_is_complete_and_unbranded() {
    const PlaneGround terrain{30.0f, 0.0015f, -0.0010f};
    const auto parts = all_parts(terrain.sampler());
    REQUIRE(parts.size() == 197u);
    REQUIRE(city::valid_start_parts(parts.data(), parts.size()));

    REQUIRE(named_count(parts, "westmere streetscape entry sign face") == 1u);
    REQUIRE(named_count(parts, "westmere streetscape entry sign accent") ==
            2u);
    REQUIRE(named_count(parts, "westmere streetscape court sign face") == 3u);
    REQUIRE(named_count(parts, "westmere streetscape mailbox body") == 9u);
    REQUIRE(named_count(parts, "westmere streetscape fire hydrant body") ==
            3u);
    REQUIRE(named_count(parts, "westmere streetscape trash bin body") == 4u);
    REQUIRE(named_count(parts,
                        "westmere streetscape utility cabinet body") == 4u);
    REQUIRE(named_count(parts, "westmere streetscape palm trunk") == 2u);
    REQUIRE(named_count(parts, "westmere streetscape palm frond") == 8u);
    REQUIRE(named_count(parts, "westmere streetscape bulletin backing") ==
            1u);
    REQUIRE(named_count(parts, "westmere streetscape bulletin card") == 6u);
    REQUIRE(named_count(parts,
                        "westmere streetscape bulletin bench seat") == 1u);
    REQUIRE(named_count(parts,
                        "westmere streetscape entry planter curb") == 1u);
    REQUIRE(named_count(parts, "westmere streetscape ornamental shrub") ==
            45u);

    for (const auto& part : parts) {
        REQUIRE(std::strstr(part.name, "brand") == nullptr);
        REQUIRE(std::strstr(part.name, "logo") == nullptr);
        REQUIRE(std::strstr(part.name, "text") == nullptr);
        REQUIRE(std::strstr(part.name, "advert") == nullptr);
        if (std::strstr(part.name, "face") ||
            std::strstr(part.name, "accent") ||
            std::strstr(part.name, "card") ||
            std::strstr(part.name, "frond") ||
            std::strstr(part.name, "shrub") ||
            std::strstr(part.name, "soil") ||
            std::strstr(part.name, "lid") ||
            std::strstr(part.name, "slot") ||
            std::strstr(part.name, "flag")) {
            REQUIRE(!part.solid);
        }
    }

    const auto entry = city::bake_westmere_streetscape_site(
        0, terrain.sampler());
    const auto entry_face = std::find_if(
        entry.begin(), entry.end(), [](const auto& part) {
            return std::strcmp(part.name,
                               "westmere streetscape entry sign face") == 0;
        });
    REQUIRE(entry_face != entry.end());
    REQUIRE(entry_face->width_m >= 4.5f);
    REQUIRE(entry_face->height_m >= 1.0f);
    REQUIRE(entry_face->depth_m <= 0.05f);
    std::printf("  Westmere streetscape: %zu parts, 9 mailboxes, 3 hydrants\n",
                parts.size());
    apricot_test::pass(
        "low-poly signs and the full unbranded street-furniture set are authored");
}

bool grounded_root(const city::StartPart& part) {
    return std::strcmp(part.name,
                       "westmere streetscape entry planter curb") == 0 ||
           std::strcmp(part.name, "westmere streetscape palm trunk") == 0 ||
           std::strcmp(part.name,
                       "westmere streetscape court sign post") == 0 ||
           std::strcmp(part.name,
                       "westmere streetscape bulletin post") == 0 ||
           std::strcmp(part.name,
                       "westmere streetscape trash bin body") == 0 ||
           std::strcmp(part.name,
                       "westmere streetscape utility cabinet body") == 0 ||
           std::strcmp(part.name, "westmere streetscape mailbox post") == 0;
}

void every_cluster_follows_the_supplied_terrain() {
    const PlaneGround low{24.0f, 0.0030f, -0.0020f};
    const PlaneGround high{26.75f, 0.0030f, -0.0020f};
    std::size_t grounded = 0;
    for (std::size_t i = 0; i < city::kWestmereStreetscapeSites.size(); ++i) {
        const auto& record = city::kWestmereStreetscapeSites[i];
        const auto a = city::bake_westmere_streetscape_site(i, low.sampler());
        const auto b =
            city::bake_westmere_streetscape_site(i, high.sampler());
        REQUIRE(a.size() == b.size());
        for (std::size_t p = 0; p < a.size(); ++p) {
            REQUIRE(same_part_except_bottom(a[p], b[p]));
            REQUIRE_NEAR(b[p].bottom_m - a[p].bottom_m, 2.75f, 0.0001f);
            if (!grounded_root(a[p])) continue;
            const city::Vec2 world = city::westmere_streetscape_world(
                record.site, a[p].centre);
            const float expected =
                low.sampler().at(world.x, world.z) - record.site.ground_m;
            REQUIRE_NEAR(a[p].bottom_m, expected, 0.0001f);
            ++grounded;
        }
    }
    REQUIRE(grounded == 28u);
    apricot_test::pass(
        "all fourteen clusters follow local grade with one deterministic bake");
}

void real_westmere_ground_keeps_every_part_valid() {
    const TerrainGround terrain{city::kMapSeed};
    std::size_t total = 0;
    for (std::size_t i = 0; i < city::kWestmereStreetscapeSites.size(); ++i) {
        const auto parts = city::bake_westmere_streetscape_site(
            i, terrain.sampler());
        REQUIRE(city::valid_start_parts(parts.data(), parts.size()));
        total += parts.size();
    }
    REQUIRE(total == 197u);
    apricot_test::pass(
        "all streetscape geometry stays above Westmere's real bluff terrain");
}

float road_edge_clearance(city::Vec2 point) {
    float clearance = std::numeric_limits<float>::max();
    for (const auto& road : city::kRoads) {
        for (int i = 1; i < road.count; ++i) {
            const city::Vec2 a{road.path[i - 1].x, road.path[i - 1].z};
            const city::Vec2 b{road.path[i].x, road.path[i].z};
            const city::Vec2 delta{b.x - a.x, b.z - a.z};
            const float length2 = delta.x * delta.x + delta.z * delta.z;
            const float t = std::clamp(
                ((point.x - a.x) * delta.x +
                 (point.z - a.z) * delta.z) /
                    length2,
                0.0f, 1.0f);
            const float dx = point.x - (a.x + delta.x * t);
            const float dz = point.z - (a.z + delta.z * t);
            const float width = road.width_m > 0.0f
                                    ? road.width_m
                                    : city::road_width_m(road.cls);
            clearance = std::min(clearance,
                                 std::sqrt(dx * dx + dz * dz) - width * 0.5f);
        }
    }
    return clearance;
}

void solids_fit_their_sites_and_clear_carriageways() {
    const PlaneGround terrain{30.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < city::kWestmereStreetscapeSites.size(); ++i) {
        const auto& record = city::kWestmereStreetscapeSites[i];
        const auto parts = city::bake_westmere_streetscape_site(
            i, terrain.sampler());
        for (const auto& part : parts) {
            const float radius =
                0.5f * std::sqrt(part.width_m * part.width_m +
                                 part.depth_m * part.depth_m);
            REQUIRE(std::fabs(part.centre.x - record.site.lot_centre.x) +
                        radius <=
                    record.site.lot_width_m * 0.5f + 0.001f);
            REQUIRE(std::fabs(part.centre.z - record.site.lot_centre.z) +
                        radius <=
                    record.site.lot_depth_m * 0.5f + 0.001f);
            if (!part.solid) continue;
            const city::Vec2 world = city::westmere_streetscape_world(
                record.site, part.centre);
            REQUIRE_MSG(road_edge_clearance(world) > radius + 0.20f,
                        "solid streetscape prop reaches the carriageway",
                        part.name);
        }
    }
    apricot_test::pass(
        "physical props stay inside their sites and outside every carriageway");
}

}  // namespace

int main() {
    stable_site_table_matches_westmere();
    inventory_is_complete_and_unbranded();
    every_cluster_follows_the_supplied_terrain();
    real_westmere_ground_keeps_every_part_valid();
    solids_fit_their_sites_and_clear_carriageways();
    return apricot_test::done("westmere_streetscape_tests");
}
