#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

#include "city/miandi_prism_works.h"
#include "city/miandi_presentation.h"
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
    // The detail pass took the bake from 239 to 508 instanced boxes. The
    // ceiling is a real budget, not a formality: every piece here is one
    // scene node on the shared unit-box mesh, and the whole Miandi set has to
    // fit alongside the road ribbons in the same frame. Rolling this pass out
    // to the other five venues is what this number is really guarding.
    REQUIRE(a.size() >= 300u && a.size() <= 560u);
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
    int relief = 0;
    bool violet = false;
    bool aqua = false;
    bool warm_white = false;
    float top = 0.0f;
    float longest_tube = 0.0f;
    for (const auto& piece : parts) {
        top = std::max(top, piece.bottom_m + piece.height_m);
        if (named(piece, "mural")) { ++mural; REQUIRE(!piece.solid); REQUIRE(piece.width_m <= .11f); }
        if (named(piece, "food yard stool") || named(piece, "loading dock bumper") ||
            named(piece, "monitor glazed end") || named(piece, "alley bike rack") ||
            named(piece, "east pallet stack") || named(piece, "south keg") ||
            named(piece, "lot wheel stop") || named(piece, "roof drain hopper"))
            ++street_detail;
        // Facade relief is measured in the shadow it casts. The first pass's
        // 0.14 m piers were invisible from the far kerb and gone entirely from
        // a moving car; anything calling itself articulation now has to be at
        // least kMiandiMirageReliefDepthM proud of its wall. Tube that traces
        // that articulation is exempt and has to stay thin: it is the line on
        // the shape, not the shape.
        if (!named(piece, "miandi neon") &&
            (named(piece, "brick pilaster") || named(piece, "base course") ||
             named(piece, "cornice") || named(piece, "string course") ||
             named(piece, "entrance attic") || named(piece, "portal jamb"))) {
            ++relief;
            REQUIRE_MSG(std::min(piece.width_m, piece.depth_m) >=
                            city::kMiandiMirageReliefDepthM,
                        "facade relief is too shallow to read", piece.name);
        }
        if (named(piece, "miandi neon")) {
            REQUIRE(!piece.solid);
            REQUIRE_MSG(std::min(piece.width_m, piece.depth_m) <= .35f,
                        "neon tube is too fat to be a tube", piece.name);
            violet |= named(piece, "violet");
            aqua |= named(piece, "aqua");
            warm_white |= named(piece, "warm-white");
            // A club whose longest tube is 13 m goes black at midnight beside
            // Ocean Drive's 46 m strokes. The architectural runs trace the
            // cornice and the string courses for the full length of the shell.
            longest_tube = std::max(longest_tube,
                                    std::max(piece.width_m, piece.height_m));
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
    REQUIRE(relief >= 20);
    REQUIRE(violet && aqua && warm_white);
    REQUIRE(longest_tube >= 100.0f);
    // The shell is 8 m and the roof parapet tops out under 10. Something has
    // to break that line or the club has no silhouette from Bayfront; the
    // entrance attic and the roof sign gantry are that something.
    REQUIRE(top > 13.0f && top <= 18.0f);
    apricot_test::pass("N2 uses mural panels, reuse details, deep facade relief, non-solid miandi neon, parcel bounds, and 18 m cap");
}


// The two layers the first pass skipped entirely, pinned so a later edit
// cannot quietly put the club back on a lawn or blank a street frontage.
void the_block_is_paved_and_every_street_face_has_a_job() {
    const auto parts = city::bake_miandi_prism_works();
    struct Quarter { const char* name; float x0, x1, z0, z1; bool found; };
    Quarter quarters[] = {
        {"Mirage north forecourt paving", -90.f, 90.f, -55.f, -43.f, false},
        {"Mirage north kerbside lot paving", -90.f, 90.f, -86.f, -55.f, false},
        {"Mirage west alley paving", -90.f, -59.f, -43.f, 29.f, false},
        {"Mirage east yard paving", 53.f, 90.f, -43.f, 29.f, false},
        {"Mirage south yard paving", -90.f, 90.f, 29.f, 90.f, false},
    };
    for (const auto& piece : parts) {
        for (auto& q : quarters) {
            if (std::strcmp(piece.name, q.name) != 0) continue;
            q.found = true;
            REQUIRE_MSG(!piece.solid, "block paving must not be a collision box",
                        piece.name);
            REQUIRE_NEAR(piece.centre.x - half_x(piece), q.x0, 1e-4f);
            REQUIRE_NEAR(piece.centre.x + half_x(piece), q.x1, 1e-4f);
            REQUIRE_NEAR(piece.centre.z - half_z(piece), q.z0, 1e-4f);
            REQUIRE_NEAR(piece.centre.z + half_z(piece), q.z1, 1e-4f);
            // The paving kit only recognises ground pieces under 0.35 m that
            // name a surface; miss that and the player drives on raw terrain
            // through geometry that looks like a car park.
            REQUIRE_MSG(city::miandi_ground_piece(piece),
                        "block paving is not firm ground", piece.name);
        }
    }
    for (const auto& q : quarters)
        REQUIRE_MSG(q.found, "a quarter of the block has no authored surface",
                    q.name);

    // Four streets, four faces, four jobs. Every wall of the shell carries at
    // least one real opening: the north front, the east loading facade, the
    // rear onto the food yard and loading court, and the Solana alley.
    for (const auto& wall : city::kMiandiPrismWorksWalls)
        REQUIRE_MSG(wall.opening_count > 0u,
                    "a street-facing wall has no opening at all", wall.name);
    bool alley_door = false, crew_door = false, kitchen_door = false;
    bool attic = false, gantry = false, portal = false, queue_canopy = false;
    for (const auto& piece : parts) {
        alley_door |= named(piece, "alley fire door");
        crew_door |= named(piece, "crew door");
        kitchen_door |= named(piece, "kitchen door");
        attic |= named(piece, "entrance attic coping");
        gantry |= named(piece, "roof sign gantry rail");
        portal |= named(piece, "club portal head");
        queue_canopy |= named(piece, "queue canopy");
    }
    REQUIRE(alley_door && crew_door && kitchen_door);
    // The threshold is a sequence, not a doorway: queue canopy, portal, attic
    // over it, sign gantry above that.
    REQUIRE(queue_canopy && portal && attic && gantry);
    apricot_test::pass("N2 paves its whole block and gives all four street faces an opening and a job");
}

}  // namespace

int main() {
    site_plan_and_bake_are_pinned();
    industrial_shell_openings_and_roof_rhythm_are_real();
    public_loading_and_yard_circulation_stay_clear();
    mural_neon_and_height_contracts_hold();
    the_block_is_paved_and_every_street_face_has_a_job();
    return apricot_test::done("miandi_prism_works_tests");
}
