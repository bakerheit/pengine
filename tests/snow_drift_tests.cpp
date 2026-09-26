// Snow under roofs: drift in from the open edges of a canopy, never inside a
// building. Everything here runs on the real producers: the authored site
// parts, append_precipitation_cover(), SnowShelterField, the packed shader grid
// and the collider that feeds tyre grip.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

#include <glm/gtc/quaternion.hpp>

#include "city/building_access.h"
#include "city/precipitation_cover.h"
#include "core/asset_root.h"
#include "gfx/snow_shelter_grid.h"
#include "physics/snow_shelter.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// world.cpp's part_transform(), repeated: a site part as the world places it.
Transform part_transform(const city::StartSite& site, const city::StartPart& part) {
    Transform t;
    t.position = {site.origin.x + site.cos_yaw * part.centre.x + site.sin_yaw * part.centre.z,
                  site.ground_m + part.bottom_m + part.height_m * 0.5f,
                  site.origin.z - site.sin_yaw * part.centre.x + site.cos_yaw * part.centre.z};
    t.rotation = glm::angleAxis(std::atan2(site.sin_yaw, site.cos_yaw), glm::vec3{0, 1, 0}) *
                 glm::quat(glm::radians(glm::vec3{part.pitch_deg, part.yaw_deg, part.roll_deg}));
    t.scale = {part.width_m, part.height_m, part.depth_m};
    return t;
}

glm::vec3 site_point(const city::StartSite& site, float x, float y, float z) {
    return {site.origin.x + site.cos_yaw * x + site.sin_yaw * z, site.ground_m + y,
            site.origin.z - site.sin_yaw * x + site.cos_yaw * z};
}

const city::StartPart* find_part(const std::vector<city::StartPart>& parts, const char* name) {
    for (const auto& part : parts)
        if (part.name && std::strcmp(part.name, name) == 0) return &part;
    return nullptr;
}

std::vector<StaticBox> site_covers(const city::StartSite& site,
                                   const std::vector<city::StartPart>& parts) {
    std::vector<StaticBox> covers;
    for (const auto& part : parts)
        city::append_precipitation_cover(part, part_transform(site, part), covers);
    return covers;
}

void shared_falloff_shape() {
    // Edge of an open roof is open ground; the middle keeps only a dusting.
    REQUIRE_NEAR(snow_drift_exposure(0.0f, 5.0f), 1.0f, 1e-6);
    REQUIRE_NEAR(snow_drift_exposure(50.0f, 5.0f), snow_drift_shader::kSnowDriftDusting, 1e-6);
    // Reach grows with headroom: the same 1.5 m in holds more snow under a
    // tall canopy than under a low carport.
    REQUIRE(snow_drift_exposure(1.5f, 5.0f) > snow_drift_exposure(1.5f, 2.2f));
    // Monotonic inward, bounded, finite.
    float previous = 2.0f;
    for (int i = 0; i <= 80; ++i) {
        const float value = snow_drift_exposure(static_cast<float>(i) * 0.05f, 4.0f);
        REQUIRE(std::isfinite(value) && value >= 0.0f && value <= 1.0f);
        REQUIRE(value <= previous);
        previous = value;
    }
    // No headroom (a point level with the underside) means no reach.
    REQUIRE_NEAR(snow_drift_exposure(0.2f, 0.0f), snow_drift_shader::kSnowDriftDusting, 1e-6);
    apricot_test::pass("drift falloff: full at the edge, dusting deep inside, reach scales with headroom");
}

void shader_compiles_the_same_source() {
    // physics/snow_shelter.h compiles snow_drift.glsl as C++. The shader must
    // include that file and call it, never grow a private copy of the numbers.
    std::ifstream file(asset_path("shaders/lit.frag"));
    REQUIRE_MSG(file.good(), "could not read lit.frag", "shader");
    const std::string lit{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    REQUIRE(lit.find("#include \"snow_drift.glsl\"") != std::string::npos);
    REQUIRE(lit.find("snow_drift_exposure(edge,anchor.z-position.y)") != std::string::npos);
    REQUIRE(lit.find("kSnowDrift") == std::string::npos);
    REQUIRE(lit.find("sheltered_from_snow") == std::string::npos);
    apricot_test::pass("lit.frag includes the one drift implementation and holds no copy of its constants");
}

void gas_station_canopy_drifts_in_from_its_edges() {
    const auto& site = city::kGasStationSite;
    const auto parts = city::bake_building(city::kGasStationPlan);
    const auto covers = site_covers(site, parts);
    const city::StartPart* canopy = find_part(parts, "canopy roof");
    REQUIRE(canopy != nullptr);
    std::size_t open = 0;
    for (const auto& cover : covers) open += cover.open_sided ? 1u : 0u;
    REQUIRE(open == 1u);
    SnowShelterField field;
    field.build(covers);

    // Walk in from the canopy's south (open) edge to its centre, at the slab.
    const float south = canopy->centre.z + canopy->depth_m * 0.5f;
    const float x = -8.1f;  // the lane between the two pump islands
    const float underside = canopy->bottom_m;
    float previous = 2.0f;
    for (float in = 0.02f; in < canopy->depth_m * 0.5f; in += 0.25f) {
        const glm::vec3 p = site_point(site, x, 0.05f, south - in);
        const float exposure = field.exposure(p.x, p.y, p.z);
        REQUIRE(exposure <= previous + 1e-6f);
        REQUIRE_NEAR(exposure, snow_drift_exposure(in, underside - 0.05f - 0.01f), 0.02);
        previous = exposure;
    }
    const glm::vec3 edge = site_point(site, x, 0.05f, south - 0.05f);
    const glm::vec3 metre_in = site_point(site, x, 0.05f, south - 1.0f);
    const glm::vec3 centre = site_point(site, x, 0.05f, canopy->centre.z);
    const glm::vec3 outside = site_point(site, x, 0.05f, south + 0.5f);
    REQUIRE(field.exposure(edge.x, edge.y, edge.z) > 0.95f);
    const float one_metre = field.exposure(metre_in.x, metre_in.y, metre_in.z);
    REQUIRE(one_metre > 0.3f && one_metre < 0.8f);
    REQUIRE_NEAR(field.exposure(centre.x, centre.y, centre.z),
                 snow_drift_shader::kSnowDriftDusting, 1e-6);
    REQUIRE(field.exposure(outside.x, outside.y, outside.z) == 1.0f);
    // On top of the canopy it is open sky again.
    const glm::vec3 top = site_point(site, x, underside + canopy->height_m + 0.01f, canopy->centre.z);
    REQUIRE(field.exposure(top.x, top.y, top.z) == 1.0f);
    std::printf("  canopy: edge %.2f, 1 m in %.2f, centre %.2f\n",
                static_cast<double>(field.exposure(edge.x, edge.y, edge.z)),
                static_cast<double>(one_metre),
                static_cast<double>(field.exposure(centre.x, centre.y, centre.z)));
    apricot_test::pass("Halloway canopy: open ground at the edge, drift fading inward, dusting under the middle");
}

void packed_grid_matches_cpu_exposure() {
    const auto& site = city::kGasStationSite;
    const auto covers = site_covers(site, city::bake_building(city::kGasStationPlan));
    SnowShelterField field;
    field.build(covers);
    SnowShelterGrid grid;
    REQUIRE(grid.build(field));
    std::size_t drifted = 0;
    for (float x = -26.0f; x <= 28.0f; x += 0.37f)
        for (float z = -22.0f; z <= 12.0f; z += 0.41f)
            for (float y : {0.05f, 1.4f, 3.0f, 5.5f}) {
                const glm::vec3 p = site_point(site, x, y, z);
                const float cpu = field.exposure(p.x, p.y, p.z);
                REQUIRE(grid.exposure(p) == cpu);
                if (cpu > 0.0f && cpu < 1.0f) ++drifted;
            }
    REQUIRE(drifted > 1000u);
    REQUIRE(grid.exposure({std::numeric_limits<float>::quiet_NaN(), 0, 0}) == 1.0f);
    apricot_test::pass("packed shader grid reproduces CPU exposure bit for bit across the forecourt");
}

void no_enterable_interior_ever_collects_snow() {
    // Every authored lot the city builds with access, including the gas store
    // under the canopy's overhang, Rook's garage (a roof and no ceiling part)
    // and the museum's open portico beside its halls.
    const auto lots = city::authored_building_access_lots();
    std::vector<StaticBox> covers;
    for (const auto& lot : lots) {
        const auto more = site_covers(lot.site, lot.parts);
        covers.insert(covers.end(), more.begin(), more.end());
    }
    SnowShelterField field;
    field.build(covers);
    SnowShelterGrid grid;
    REQUIRE(grid.build(field));
    std::size_t floors = 0, samples = 0;
    for (const auto& lot : lots) {
        for (const auto& part : lot.parts) {
            if (!part.name || !std::strstr(part.name, "interior floor")) continue;
            ++floors;
            const Transform t = part_transform(lot.site, part);
            const float top = t.position.y + part.height_m * 0.5f;
            // Stay 0.15 m inside the slab so the wall line is the only edge.
            for (float u = -0.5f; u <= 0.5f; u += 0.05f)
                for (float v = -0.5f; v <= 0.5f; v += 0.05f) {
                    const glm::vec3 local{u * std::max(part.width_m - 0.3f, 0.0f), 0.0f,
                                          v * std::max(part.depth_m - 0.3f, 0.0f)};
                    const glm::vec3 w = t.position + t.rotation * local;
                    for (float above : {0.02f, 1.2f}) {
                        const glm::vec3 p{w.x, top + above, w.z};
                        if (!field.covered(p.x, p.y, p.z)) continue;  // stair voids, atria
                        ++samples;
                        REQUIRE_MSG(field.exposure(p.x, p.y, p.z) == 0.0f, part.name, "interior");
                        REQUIRE(grid.exposure(p) == 0.0f);
                    }
                }
        }
    }
    REQUIRE(floors >= 8u);
    REQUIRE(samples > 5000u);
    std::printf("  %zu interior floors, %zu covered samples, all bare\n", floors, samples);
    apricot_test::pass("no covered point on any authored interior floor receives drifted snow");
}

void physics_depth_follows_the_same_exposure() {
    const auto& site = city::kGasStationSite;
    const auto parts = city::bake_building(city::kGasStationPlan);
    SnowShelterField shelter;
    shelter.build(site_covers(site, parts));
    const city::StartPart* canopy = find_part(parts, "canopy roof");
    REQUIRE(canopy != nullptr);
    const TerrainCollider bare(city::kMapSeed);
    TerrainCollider collider(city::kMapSeed);
    collider.set_snow_shelter(&shelter);
    collider.set_snow_clearance(nullptr, 0.8f);
    collider.set_snow_collision_depth(0.7f);
    const float south = canopy->centre.z + canopy->depth_m * 0.5f;
    std::size_t partial = 0;
    for (float in : {-1.0f, 0.1f, 0.6f, 1.2f, 2.0f, 4.0f, 7.0f}) {
        const glm::vec3 p = site_point(site, -8.1f, 0.0f, south - in);
        const float ground = bare.height(p.x, p.z);
        const float exposure = shelter.exposure(p.x, ground, p.z);
        REQUIRE_NEAR(collider.snow_depth_at(p.x, ground, p.z), 0.8f * exposure, 1e-6);
        // The solid snow the tyres ride on, as the terrain reports it.
        const float collision = collider.height(p.x, p.z) - ground;
        if (exposure >= 1.0f) REQUIRE_NEAR(collision, 0.7f, 1e-5);
        else REQUIRE_NEAR(collision, std::max(0.8f * exposure - 0.10f, 0.0f), 1e-5);
        if (exposure > 0.2f && exposure < 1.0f) ++partial;
    }
    REQUIRE(partial > 0u);
    apricot_test::pass("tyre snow depth scales with the same exposure the shader draws");
}

}  // namespace

int main() {
    shared_falloff_shape();
    shader_compiles_the_same_source();
    gas_station_canopy_drifts_in_from_its_edges();
    packed_grid_matches_cpu_exposure();
    no_enterable_interior_ever_collects_snow();
    physics_depth_follows_the_same_exposure();
    return apricot_test::done("snow_drift");
}
