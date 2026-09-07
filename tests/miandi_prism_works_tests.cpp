#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

#include "city/miandi_prism_works.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool named(const city::BuildingPiece& piece, std::string_view text) {
    return piece.name && std::string_view(piece.name).find(text) != std::string_view::npos;
}

float half_x(const city::BuildingPiece& piece) {
    const float r = piece.yaw_deg * 0.01745329251994329577f;
    return std::fabs(std::cos(r)) * piece.width_m * .5f +
           std::fabs(std::sin(r)) * piece.depth_m * .5f;
}

float half_z(const city::BuildingPiece& piece) {
    const float r = piece.yaw_deg * 0.01745329251994329577f;
    return std::fabs(std::sin(r)) * piece.width_m * .5f +
           std::fabs(std::cos(r)) * piece.depth_m * .5f;
}

bool solid_overlaps(const city::BuildingPiece& piece, float x0, float x1,
                    float z0, float z1) {
    return piece.solid && piece.centre.x - half_x(piece) < x1 &&
           piece.centre.x + half_x(piece) > x0 &&
           piece.centre.z - half_z(piece) < z1 &&
           piece.centre.z + half_z(piece) > z0;
}

void site_plan_and_bake_are_pinned() {
    const auto& site = city::kMiandiPrismWorksSite;
    REQUIRE_NEAR(site.origin.x, 7200.0f, 1e-5f);
    REQUIRE_NEAR(site.origin.z, 8500.0f, 1e-5f);
    REQUIRE_NEAR(site.ground_m, 8.0f, 1e-5f);
    REQUIRE_NEAR(site.lot_width_m, 160.0f, 1e-5f);
    REQUIRE_NEAR(site.lot_depth_m, 150.0f, 1e-5f);
    REQUIRE(site.cos_yaw == 1.0f && site.sin_yaw == 0.0f);
    REQUIRE(city::valid_building_plan(city::kMiandiPrismWorksPlan));
    const auto a = city::bake_miandi_prism_works();
    const auto b = city::bake_miandi_prism_works();
    REQUIRE(a.size() >= 65u && a.size() <= 400u);  // bounded two-layer club name
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::strcmp(a[i].name, b[i].name) == 0);
        REQUIRE_NEAR(a[i].centre.x, b[i].centre.x, 1e-6f);
        REQUIRE_NEAR(a[i].centre.z, b[i].centre.z, 1e-6f);
        REQUIRE(a[i].solid == b[i].solid);
    }
    apricot_test::pass("N2 pins the 7200/8500 parcel and deterministic creator bake");
}

void industrial_shell_openings_and_roof_rhythm_are_real() {
    const auto parts = city::bake_miandi_prism_works();
    int monitors = 0;
    bool gallery_gap = false;
    bool club_gap = false;
    bool loading_gap = false;
    for (const auto& piece : parts) {
        monitors += named(piece, "sawtooth monitor");
        if (named(piece, "gallery door gap")) { gallery_gap = true; REQUIRE(!piece.solid); }
        if (named(piece, "club door gap")) { club_gap = true; REQUIRE(!piece.solid); }
        if (named(piece, "loading door gap")) { loading_gap = true; REQUIRE(!piece.solid); }
    }
    REQUIRE(monitors >= 8);  // four pitched monitors bake to two roof planes each.
    REQUIRE(gallery_gap && club_gap && loading_gap);
    for (const auto& piece : parts)
        if (piece.solid && piece.bottom_m > .25f && piece.bottom_m < 4.0f)
            REQUIRE_MSG(!solid_overlaps(piece, -41.0f, -35.0f, -46.0f, -40.0f),
                        "gallery door gap has solid collision", piece.name);
    apricot_test::pass("N2 has separate real gallery, club, loading gaps and four-monitor industrial roof rhythm");
}

void public_loading_and_yard_circulation_stay_clear() {
    const auto parts = city::bake_miandi_prism_works();
    bool public_walk = false;
    bool club_public_walk = false;
    bool loading_route = false;
    bool loading_threshold = false;
    bool food_canopy = false;
    int queue_rails = 0;
    int fence_pieces = 0;
    for (const auto& piece : parts) {
        if (named(piece, "north public walk")) {
            public_walk = true;
            REQUIRE(!piece.solid);
            REQUIRE_NEAR(piece.centre.z - half_z(piece), -86.0f, 1e-5f);
        }
        if (named(piece, "club public walk")) {
            club_public_walk = true;
            REQUIRE(!piece.solid);
            REQUIRE_NEAR(piece.centre.z - half_z(piece), -86.0f, 1e-5f);
        }
        if (named(piece, "south loading route")) { loading_route = true; REQUIRE(!piece.solid); }
        if (named(piece, "loading threshold connector")) { loading_threshold = true; REQUIRE(!piece.solid); }
        food_canopy |= named(piece, "food yard canopy");
        queue_rails += named(piece, "queue rail");
        fence_pieces += named(piece, "loading fence");
        if (named(piece, "north public walk") || named(piece, "threshold connector") ||
            named(piece, "south loading route") || named(piece, "loading court apron"))
            REQUIRE(!piece.solid);
        // North is a pedestrian arrival strip, south is a discrete east-side
        // truck run; neither corridor is allowed to become a fake through road.
        if (piece.solid && !named(piece, "warehouse floor") && piece.bottom_m < 2.0f) {
            REQUIRE_MSG(!solid_overlaps(piece, -39.5f, -36.5f, -90.0f, -40.0f),
                        "solid pinches Bayfront public route", piece.name);
            REQUIRE_MSG(!solid_overlaps(piece, 58.0f, 74.0f, 26.0f, 75.0f),
                        "solid pinches Coral loading route", piece.name);
        }
    }
    REQUIRE(public_walk && club_public_walk && loading_route &&
            loading_threshold && food_canopy);
    REQUIRE(queue_rails >= 4 && fence_pieces >= 3);
    apricot_test::pass("N2 keeps Bayfront public and Coral loading routes non-solid, clear, and purpose-specific");
}

void mural_neon_and_height_contracts_hold() {
    const auto parts = city::bake_miandi_prism_works();
    int mural = 0;
    int street_detail = 0;
    bool violet = false;
    bool aqua = false;
    bool warm_white = false;
    float top = 0.0f;
    for (const auto& piece : parts) {
        top = std::max(top, piece.bottom_m + piece.height_m);
        if (named(piece, "mural")) { ++mural; REQUIRE(!piece.solid); REQUIRE(piece.width_m <= .11f); }
        if (named(piece, "front brick pier") || named(piece, "food yard stool") ||
            named(piece, "loading dock bumper") || named(piece, "monitor glazed end"))
            ++street_detail;
        if (named(piece, "miandi neon")) {
            REQUIRE(!piece.solid);
            violet |= named(piece, "violet");
            aqua |= named(piece, "aqua");
            warm_white |= named(piece, "warm-white");
        }
        if (piece.solid) {
            REQUIRE(piece.centre.x - half_x(piece) >= -80.001f);
            REQUIRE(piece.centre.x + half_x(piece) <= 80.001f);
            REQUIRE(piece.centre.z - half_z(piece) >= -75.001f);
            REQUIRE(piece.centre.z + half_z(piece) <= 75.001f);
        }
    }
    REQUIRE(mural >= 4);
    REQUIRE(street_detail >= 16);
    REQUIRE(violet && aqua && warm_white);
    REQUIRE(top <= 18.0f);
    apricot_test::pass("N2 uses mural panels, reuse details, non-solid miandi neon, parcel bounds, and 18 m cap");
}

}  // namespace

int main() {
    site_plan_and_bake_are_pinned();
    industrial_shell_openings_and_roof_rhythm_are_real();
    public_loading_and_yard_circulation_stay_clear();
    mural_neon_and_height_contracts_hold();
    return apricot_test::done("miandi_prism_works_tests");
}
