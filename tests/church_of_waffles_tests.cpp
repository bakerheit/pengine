// The Church of Waffles: the second imported restaurant shell, cooked from the
// supplied Quequis House GLB. The parcel checks mirror burgerpiz_tests; the
// traversal route is this building's own, because the shell is a long shallow
// diner with one glazed entrance rather than the BurgerPiz box.
#include <cstring>
#include <filesystem>
#include <fstream>

#include "city/building_access.h"
#include "city/burgerpiz_asset.h"
#include "city/church_of_waffles.h"
#include "city/interior_streaming.h"
#include "city/spines.h"
#include "city/terrain_ops.h"
#include "core/emesh_reader.h"
#include "game/character.h"
#include "gfx/street_lamp_light.h"
#include "test_assert.h"

using namespace apricot;

namespace {

const city::StartSite& site() { return city::kChurchOfWafflesSite; }
const char* root() { return city::kChurchOfWafflesAssetRoot; }

glm::vec3 world(glm::vec3 p) { return city::burgerpiz_world(p, site()); }
std::string path(const std::string& file) { return city::burgerpiz_path(file, root()); }

PlayerCharacterState walk(PlayerCharacterState s, const TerrainCollider& collider,
                          glm::vec3 goal) {
    const CharacterTuning tuning;
    constexpr float dt = 1.f / 120;
    for (int i = 0; i < 2200; ++i) {
        glm::vec2 d{goal.x - s.position.x, goal.z - s.position.z};
        if (glm::length(d) < .025f) break;
        InputFrame input;
        input.look_dx = std::atan2(d.x, -d.y) - s.view_yaw;
        input.throttle = std::min(1.f, glm::length(d) / (tuning.walk_speed_mps * dt));
        s = step_character(s, tuning, input, collider, dt);
        REQUIRE(character_position_clear(collider, s.position, tuning));
    }
    return s;
}

void parcel_and_frontage() {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    auto ribbon = bake_ribbons(roads, ground.sampler());
    const auto access = city::bake_building_access(roads, ribbon, ground.sampler());
    bool connected = false;
    for (const auto& lot : access.lots)
        if (city::access_same_site(lot.site, site())) {
            REQUIRE(lot.connected);
            REQUIRE((lot.entrance.road_key >> 32) == 38u);  // Eighth Street
            REQUIRE(lot.width_m >= 6.f);
            connected = true;
        }
    REQUIRE(connected);
    for (float x : {-30.f, 0.f, 30.f})
        for (float z : {-19.f, 0.f, 19.f}) {
            const auto p = world({x, 0, z});
            REQUIRE_NEAR(mesh_height_at(city::kMapSeed, p.x, p.z), 12.f, .02f);
            REQUIRE(city::authored_site_clearance_weight(p.x, p.z) > .99f);
        }
    // The retained parcel must clear every ground-level Pinatty carriageway,
    // including the Eighth Street edge its own driveway crosses.
    for (const auto& road : city::kRoads) {
        if (road.district != city::DistrictId::PinattyRow ||
            road.structure != city::RoadStructure::Ground)
            continue;
        for (int i = 1; i < road.count; ++i)
            for (float x : {-30.f, 30.f})
                for (float z : {-19.f, 19.f}) {
                    const auto p = world({x, 0, z});
                    const glm::vec2 a{road.path[i - 1].x, road.path[i - 1].z};
                    const glm::vec2 b{road.path[i].x, road.path[i].z};
                    const auto v = b - a, delta = glm::vec2{p.x, p.z} - a;
                    const float t = glm::clamp(glm::dot(delta, v) / glm::dot(v, v), 0.f, 1.f);
                    REQUIRE(glm::length(delta - t * v) > city::road_ribbon_half_m(road.cls));
                }
    }
    // The lot plan is this shell's own, not the BurgerPiz one it renders beside.
    const auto parts = city::bake_church_of_waffles_lot();
    REQUIRE(parts.size() == std::size(city::kChurchOfWafflesLotParts));
    REQUIRE(city::kChurchOfWafflesLotPlan.fixtures != city::kBurgerPizLotParts);
    bool floor = false;
    for (const auto& part : parts) floor |= city::is_interior_floor(part);
    REQUIRE(floor);
    apricot_test::pass(
        "waffle-house parcel clears streets, connects to Eighth Street and keeps level ground");
}

void cooked_asset(const city::BurgerPizAsset& asset) {
    REQUIRE(asset.materials.size() >= 50);
    REQUIRE(asset.lights.size() >= 16);
    // The two supplied lamp posts carry two lens quads each.
    REQUIRE(asset.parking_lights.size() == 4);
    for (const auto p : asset.parking_lights) {
        REQUIRE(p.y > 8 && p.y < 9);
        REQUIRE(std::fabs(p.x) > 12 && std::fabs(p.x) < 21);
        REQUIRE(p.z > 13 && p.z < 19);  // in the forecourt, inside the parcel
        const auto position = world(p);
        REQUIRE_NEAR(position.y, site().ground_m + p.y, .001f);
        REQUIRE(!parking_lamp_light(position, position, 0.f));
        REQUIRE(!parking_lamp_light(position, position + glm::vec3{261, 0, 0}, 1.f));
        const auto night = parking_lamp_light(position, position, 1.f);
        const auto dusk = parking_lamp_light(position, position, .5f);
        REQUIRE(night.has_value() && dusk.has_value());
        REQUIRE_NEAR(night->direction_power.w, 10.f, .001f);
        REQUIRE_NEAR(dusk->direction_power.w, 5.f, .001f);
        REQUIRE(night->position_range.w > p.y);  // beam actually reaches the lot
    }
    StaticEmesh lens;
    REQUIRE(read_static_emesh(path("parking_lens.emesh"), lens));
    for (const auto p : asset.parking_lights) {
        REQUIRE(p.x >= lens.bounds.min.x && p.x <= lens.bounds.max.x);
        REQUIRE(p.z >= lens.bounds.min.z && p.z <= lens.bounds.max.z);
        REQUIRE_NEAR(p.y + .04f, lens.bounds.min.y, .01f);
    }
    std::size_t triangles = 0;
    bool glass = false, purple = false, emissive_sign = false;
    for (const auto& material : asset.materials) {
        StaticEmesh mesh;
        REQUIRE(read_static_emesh(path(material.mesh), mesh));
        triangles += mesh.indices.size() / 3;
        REQUIRE(mesh.bounds.min.x >= -30 && mesh.bounds.max.x <= 30);
        REQUIRE(mesh.bounds.min.z >= -19 && mesh.bounds.max.z <= 19);
        REQUIRE(mesh.bounds.min.y >= .0f);
        if (material.texture != "-") REQUIRE(std::filesystem::exists(path(material.texture)));
        glass |= material.glass && material.tint.a < .5f;
        // A purple rebrand: some surface is tinted with blue clearly ahead of
        // green and at least matching red. The original shell was yellow.
        purple |= !material.glass && material.tint.b > material.tint.g + .2f &&
                  material.tint.b >= material.tint.r;
        emissive_sign |= material.emissive && material.texture == "-" && material.tint.b > .9f;
    }
    REQUIRE(triangles > 40000);
    REQUIRE(glass);
    REQUIRE(purple);
    REQUIRE(emissive_sign);
    // The rebrand is in the manifest, and the supplied name is not.
    std::ifstream manifest(path("manifest.json"));
    const std::string content((std::istreambuf_iterator<char>(manifest)), {});
    REQUIRE(content.find("Church of Waffles") != std::string::npos);
    REQUIRE(content.find("\"Text\"") == std::string::npos);
    apricot_test::pass(
        "cooked shell loads: translucent glazing, purple rebrand, lit sign, no demo-city meshes");
}

void traversal(const city::BurgerPizAsset& asset) {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    auto ribbon = bake_ribbons(roads, ground.sampler());
    const auto access = city::bake_building_access(roads, ribbon, ground.sampler());
    city::append_building_access(ribbon, access);
    TerrainCollider collider{city::kMapSeed};
    collider.set_road_collision(build_road_collision(ribbon));
    const float yaw = std::atan2(site().sin_yaw, site().cos_yaw);
    collider.add_static_ground_rect({site().origin.x, site().origin.z}, 12.1f, {30, 19}, yaw,
                                    Surface::Rock);
    city::add_burgerpiz_collision(collider, asset, site());
    // Forecourt -> the single glazed door -> the dining aisle -> the counter
    // -> the east booths -> back out. The door centre is local x -0.35; the
    // aisle runs between the counter front (z -2.50) and the window seating
    // (z +0.78), so the first move inside is south, clear of the armchairs.
    auto start = world({-.35f, 0, 14});
    auto state = spawn_character(collider, start.x, start.z);
    for (const auto p : std::vector<glm::vec2>{{-.35f, 4}, {-.35f, 1.6f}, {-.35f, 0},
                                               {-6, -1.5f}, {-11, -1.5f}, {-13.9f, 0},
                                               {-6, -1.5f}, {-.35f, 0}, {-.35f, 1.6f},
                                               {-.35f, 4}, {-.35f, 14}}) {
        const auto goal = world({p.x, 0, p.y});
        state = walk(state, collider, goal);
        const auto reached = city::access_local(site(), {state.position.x, state.position.z});
        std::printf("  walk %.2f %.2f -> %.2f %.2f height %.2f\n", static_cast<double>(p.x),
                    static_cast<double>(p.y), static_cast<double>(reached.x),
                    static_cast<double>(reached.y), static_cast<double>(state.position.y));
        REQUIRE_NEAR(state.position.x, goal.x, .05f);
        REQUIRE_NEAR(state.position.z, goal.z, .05f);
        REQUIRE(state.position.y >= 12.f && state.position.y < 12.5f);
    }
    apricot_test::pass(
        "real character walks in through the propped entrance, down the dining aisle and back out");

    // Negative controls, so the walk above is collision-backed and not a
    // character sliding through absent geometry.
    start = world({-6, 0, -1.5f});
    state = spawn_character(collider, start.x, start.z);
    state = walk(state, collider, world({-6, 0, -5}));
    REQUIRE(city::access_local(site(), {state.position.x, state.position.z}).y > -2.6f);
    const auto entrance = world({-.35f, 1.5f, 2.55f});
    collider.add_static_oriented_box(entrance, {.75f, 1.5f, .12f}, yaw);
    start = world({-.35f, 0, 6});
    state = spawn_character(collider, start.x, start.z);
    state = walk(state, collider, world({-.35f, 0, 1.6f}));
    REQUIRE(city::access_local(site(), {state.position.x, state.position.z}).y > 2.8f);
    apricot_test::pass("the service counter and a sealed doorway both stop the same character");
}

}  // namespace

int main(int argc, char** argv) {
    const bool required = argc > 1 && std::strcmp(argv[1], "--require-assets") == 0;
    parcel_and_frontage();
    if (!std::filesystem::exists(path("materials.txt")) && !required) {
        std::puts("SKIP private Church of Waffles geometry; cook the supplied GLB to run "
                  "asset and traversal checks");
        return apricot_test::done("church_of_waffles_tests");
    }
    city::BurgerPizAsset asset;
    REQUIRE(city::load_burgerpiz_asset(asset, root()));
    cooked_asset(asset);
    traversal(asset);
    return apricot_test::done("church_of_waffles_tests");
}
