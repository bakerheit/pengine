#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "city/east_arm_plaza.h"
#include "city/terrain_ops.h"
#include "terrain/chunk.h"
#include "terrain/heightmap.h"
#include "terrain/scatter.h"
#include "test_assert.h"

using namespace apricot;

namespace {

glm::vec2 world_point(const city::StartSite& site, city::Vec2 local) {
    return {site.origin.x + site.cos_yaw * local.x +
                site.sin_yaw * local.z,
            site.origin.z - site.sin_yaw * local.x +
                site.cos_yaw * local.z};
}

const city::StartPart& named_part(const std::vector<city::StartPart>& parts,
                                  const char* name) {
    for (const auto& part : parts)
        if (std::strcmp(part.name, name) == 0) return part;
    REQUIRE_MSG(false, "authored Galleria part is missing", name);
    return parts.front();
}

void parcel_grade_is_engineered() {
    const auto& site = city::kEastArmPlazaSite;
    float low = 1e9f;
    float high = -1e9f;
    float worst_delta = 0.0f;
    city::Vec2 worst_local{};
    const auto& op = city::kEastArmGalleriaTerrainOp;
    REQUIRE(op.kind == city::OpKind::Flatten);
    REQUIRE(op.shape == city::OpShape::Rect);
    REQUIRE_NEAR(op.centre.x, site.origin.x, .001f);
    REQUIRE_NEAR(op.centre.z, site.origin.z, .001f);
    REQUIRE_NEAR(op.target_m, site.ground_m, .001f);

    for (int zi = -8; zi <= 8; ++zi) {
        for (int xi = -8; xi <= 8; ++xi) {
            const city::Vec2 local{
                static_cast<float>(xi) * site.lot_width_m / 16.0f,
                static_cast<float>(zi) * site.lot_depth_m / 16.0f};
            const glm::vec2 p = world_point(site, local);
            const float y = mesh_height_at(city::kMapSeed, p.x, p.y);
            low = std::min(low, y);
            high = std::max(high, y);
            const float delta = std::fabs(y - site.ground_m);
            if (delta > worst_delta) {
                worst_delta = delta;
                worst_local = local;
            }
            if (local.z <= 20.0f)
                REQUIRE_NEAR(y, site.ground_m, .02f);

            for (int k = city::kBaseOpCount; k < city::kTerrainOpCount; ++k) {
                float profile = 0.0f;
                const float weight = city::op_weight(
                    city::kTerrainOps[k], p.x, p.y, profile);
                if (weight <= 0.0f) continue;
                REQUIRE_MSG(std::strcmp(city::kTerrainOps[k].note,
                                        "the East Arm") == 0,
                            "another road terrain corridor enters the Galleria parcel",
                            city::kTerrainOps[k].note);
                REQUIRE_MSG(local.z > 20.0f,
                            "East Arm grade reaches the mall or pedestrian core",
                            "the East Arm");
            }
        }
    }
    const float terrace_top = site.ground_m +
                              city::east_arm_plaza_ground_piece().height_m;
    std::printf("  East Arm terrace diagnostic %.3f..%.3f m; worst delta %.3f at (%.1f, %.1f)\n",
                low, high, worst_delta, worst_local.x, worst_local.z);
    REQUIRE(terrace_top - low <= city::kEastArmPlazaRetainingDepthM + .01f);
    REQUIRE(high <= site.ground_m + .05f);
    std::printf("  East Arm terrace %.3f..%.3f m; flat through local z 20; "
                "worst retained edge %.3f m at (%.1f, %.1f)\n",
                low, high, terrace_top - low,
                worst_local.x, worst_local.z);
    apricot_test::pass("mall and pedestrian core are level; only the retained parking frontage follows the East Arm");
}

void developed_site_is_clear_of_wild_scatter() {
    const auto& op = city::kEastArmGalleriaTerrainOp;
    const ChunkCoord lo = chunk_at(op.centre.x - op.half_m.x - op.feather_m,
                                  op.centre.z - op.half_m.z - op.feather_m);
    const ChunkCoord hi = chunk_at(op.centre.x + op.half_m.x + op.feather_m,
                                  op.centre.z + op.half_m.z + op.feather_m);
    std::size_t nearby = 0;
    for (int cz = lo.z; cz <= hi.z; ++cz) {
        for (int cx = lo.x; cx <= hi.x; ++cx) {
            for (const ScatterProp& prop :
                 scatter_chunk(city::kMapSeed, ChunkCoord{cx, cz})) {
                ++nearby;
                REQUIRE_MSG(city::authored_site_clearance_weight(
                                prop.position.x, prop.position.z) == 0.0f,
                            "wild terrain prop survived inside the Galleria clearance",
                            "East Arm Galleria");
            }
        }
    }
    REQUIRE(nearby > 0u);
    apricot_test::pass("wild trees and rocks stay outside the graded Galleria envelope");
}

void inventory_and_uses_are_explicit() {
    const auto parts = city::bake_east_arm_plaza();
    REQUIRE(!parts.empty());
    REQUIRE(parts.size() < city::kEastArmPlazaMaxParts);
    for (const auto use : {city::EastArmPlazaUse::Mall,
                           city::EastArmPlazaUse::CinemaLobby,
                           city::EastArmPlazaUse::Bookstore,
                           city::EastArmPlazaUse::CoffeeShop,
                           city::EastArmPlazaUse::Restaurant})
        REQUIRE_MSG(city::east_arm_plaza_has_use(parts, use),
                    "plaza is missing named tenant use", city::east_arm_plaza_use_name(use));
    std::size_t floors = 0, signs = 0, thresholds = 0;
    for (const auto& part : parts) {
        floors += std::strstr(part.name, "interior floor") != nullptr;
        signs += std::strstr(part.name, "sign face") != nullptr;
        thresholds += std::strstr(part.name, "threshold") != nullptr;
    }
    REQUIRE(floors >= 5u);
    REQUIRE(signs >= 5u);
    REQUIRE(thresholds >= 4u);
    apricot_test::pass("plaza carries all five named uses with tenant floors, thresholds and sign faces");
}

void geometry_stays_in_authored_lot() {
    const auto parts = city::bake_east_arm_plaza();
    const auto& site = city::kEastArmPlazaSite;
    REQUIRE_NEAR(site.origin.x, 351.968f, .0001f);
    REQUIRE_NEAR(site.origin.z, 915.617f, .0001f);
    REQUIRE_NEAR(site.lot_width_m, 148.0f, .0001f);
    REQUIRE_NEAR(site.lot_depth_m, 170.0f, .0001f);
    for (const auto& part : parts) {
        REQUIRE(part.name != nullptr);
        REQUIRE(part.width_m > 0 && part.height_m > 0 && part.depth_m > 0);
        REQUIRE(std::fabs(part.centre.x) + part.width_m * .5f <= site.lot_width_m * .5f + .001f);
        REQUIRE(std::fabs(part.centre.z) + part.depth_m * .5f <= site.lot_depth_m * .5f + .001f);
    }
    const auto ground = city::east_arm_plaza_ground_piece();
    REQUIRE(std::strcmp(ground.name, "east arm plaza parking lot") == 0);
    REQUIRE_NEAR(ground.width_m, site.lot_width_m, .001f);
    REQUIRE_NEAR(ground.depth_m, site.lot_depth_m, .001f);
    apricot_test::pass("plaza source parts remain inside its East Arm parcel");
}

void open_threshold_routes_are_structurally_clear() {
    const auto parts = city::bake_east_arm_plaza();
    REQUIRE(city::east_arm_plaza_walk_routes_clear(parts));
    for (const city::Vec2 gap : {city::Vec2{0, 36}, city::Vec2{0, -32},
                                 city::Vec2{-19, -46}, city::Vec2{-20, 18.5f},
                                 city::Vec2{20, 18.5f}, city::Vec2{-46, 36},
                                 city::Vec2{48, 36}})
        REQUIRE_MSG(city::east_arm_plaza_point_clear(parts, gap),
                    "named entrance gap is blocked by a solid authored part", "entry gap");
    REQUIRE(!city::east_arm_plaza_point_clear(parts, {-68, -17}));
    REQUIRE(!city::east_arm_plaza_point_clear(parts, {48, 7.5f}));
    apricot_test::pass("plaza approaches cross genuine entrance gaps and stop at solid fixtures");
}

void blueprint_has_mall_scale_and_real_circulation() {
    const auto parts = city::bake_east_arm_plaza();
    const auto& shell = named_part(parts, "mall main interior floor");
    const auto& spine = named_part(parts, "mall concourse interior floor");
    const auto& cross = named_part(parts, "mall cross gallery interior floor");
    const auto& service = named_part(parts, "east arm plaza service lane");
    REQUIRE(shell.width_m * shell.depth_m >= 14000.0f);
    REQUIRE(spine.width_m >= 14.0f && spine.depth_m >= 90.0f);
    REQUIRE(cross.width_m >= 100.0f && cross.depth_m >= 10.0f);
    REQUIRE(service.width_m >= 125.0f && service.depth_m >= 10.0f);
    REQUIRE(named_part(parts, "cinema auditorium interior floor").depth_m >= 25.0f);
    REQUIRE(named_part(parts, "bookstore interior floor").width_m >= 40.0f);
    REQUIRE(named_part(parts, "coffee shop mall threshold").depth_m >= 4.0f);
    REQUIRE(named_part(parts, "restaurant mall threshold").depth_m >= 4.0f);
    apricot_test::pass("14,000 square metre shell has a public spine, cross-gallery, anchors and rear service lane");
}

void exterior_reads_as_a_finished_landmark() {
    const auto parts = city::bake_east_arm_plaza();
    std::size_t glass = 0, palms = 0, arcade_columns = 0, lights = 0;
    float highest_top = 0.0f;
    for (const auto& part : parts) {
        glass += part.finish == city::StartFinish::Glass;
        palms += std::strstr(part.name, "palm crown") != nullptr;
        arcade_columns += std::strstr(part.name, "arcade column") != nullptr;
        lights += std::strstr(part.name, "light lens") != nullptr ||
                  std::strstr(part.name, "lamp head") != nullptr;
        highest_top = std::max(highest_top, part.bottom_m + part.height_m);
    }
    REQUIRE(glass >= 10u);
    REQUIRE(palms >= 3u);
    REQUIRE(arcade_columns >= 7u);
    REQUIRE(lights >= 11u);
    REQUIRE(highest_top >= 11.4f);
    apricot_test::pass("layered roofline, glazed frontage, arcade, palms and lighting are authored");
}

void neon_sign_textures_fit_their_facades() {
    const auto parts = city::bake_east_arm_plaza();
    struct SignFace {
        const char* name;
        float texture_aspect;
    };
    constexpr SignFace signs[] = {
        {"mall arcade sign face", 2048.0f / 273.0f},
        {"cinema marquee sign face", 2048.0f / 326.0f},
        {"bookstore sign face", 2048.0f / 248.0f},
        {"coffee shop sign face", 2048.0f / 322.0f},
        {"restaurant sign face", 2048.0f / 248.0f},
    };
    for (const auto& sign : signs) {
        const auto& part = named_part(parts, sign.name);
        const float face_aspect = part.width_m / part.height_m;
        REQUIRE_MSG(face_aspect > 6.0f,
                    "tenant sign regressed to a boxy generic aspect", sign.name);
        REQUIRE_NEAR(face_aspect, sign.texture_aspect, .03f);
    }
    apricot_test::pass("five neon textures match their individual facade proportions without stretch");
}

}  // namespace

int main() {
    parcel_grade_is_engineered();
    developed_site_is_clear_of_wild_scatter();
    inventory_and_uses_are_explicit();
    geometry_stays_in_authored_lot();
    open_threshold_routes_are_structurally_clear();
    blueprint_has_mall_scale_and_real_circulation();
    exterior_reads_as_a_finished_landmark();
    neon_sign_textures_fit_their_facades();
    return apricot_test::done("east_arm_plaza_tests");
}
