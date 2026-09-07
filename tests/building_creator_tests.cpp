#include <cmath>
#include <cstdio>
#include <vector>

#include "city/building_creator.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool overlaps(const city::BuildingPiece& p, float x0, float x1, float y0,
              float y1) {
    const float px0 = p.centre.x - p.width_m * 0.5f;
    const float px1 = p.centre.x + p.width_m * 0.5f;
    const float py0 = p.bottom_m;
    const float py1 = p.bottom_m + p.height_m;
    return px0 < x1 && px1 > x0 && py0 < y1 && py1 > y0;
}

void stacked_openings_stay_cut_from_the_wall() {
    static constexpr city::BuildingOpening openings[] = {
        {"door", city::OpeningKind::Door, 5.0f, 2.4f, 0.0f, 2.35f,
         city::BuildingFinish::TealDoor, 0, 0},
        {"transom", city::OpeningKind::Window, 5.0f, 2.4f, 2.55f, 0.65f,
         city::BuildingFinish::Glass, 2, 0},
    };
    static constexpr city::BuildingWall walls[] = {
        {"front", {0.0f, 0.0f}, {10.0f, 0.0f}, 0.0f, 3.6f, 0.24f,
         city::BuildingFinish::WarmWall, openings, 2},
    };
    static constexpr city::BuildingPlan plan{
        "stacked opening", walls, 1, nullptr, 0};

    const std::vector<city::BuildingPiece> pieces = city::bake_building(plan);
    REQUIRE(!pieces.empty());

    bool header_band = false;
    for (const city::BuildingPiece& p : pieces) {
        if (!p.solid) continue;
        REQUIRE_MSG(!overlaps(p, 3.9f, 6.1f, 0.1f, 2.25f),
                    "solid wall refilled the door opening", p.name);
        REQUIRE_MSG(!overlaps(p, 3.9f, 6.1f, 2.65f, 3.1f),
                    "solid wall refilled the transom opening", p.name);
        if (overlaps(p, 4.8f, 5.2f, 2.38f, 2.52f)) header_band = true;
    }
    REQUIRE_MSG(header_band, "door/transom separator disappeared", "wall bake");
    apricot_test::pass("stacked door and transom remain real wall cutouts");
}

void window_styles_bake_frames_and_mullions() {
    static constexpr city::BuildingOpening opening{
        "four pane", city::OpeningKind::Window, 3.0f, 3.0f, 0.8f, 1.8f,
        city::BuildingFinish::Glass, 1, 1};
    static constexpr city::BuildingWall wall{
        "front", {0.0f, 0.0f}, {6.0f, 0.0f}, 0.0f, 3.2f, 0.24f,
        city::BuildingFinish::WarmWall, &opening, 1};
    static constexpr city::BuildingPlan plan{
        "window style", &wall, 1, nullptr, 0};

    const std::vector<city::BuildingPiece> pieces = city::bake_building(plan);
    int glass = 0;
    int trim = 0;
    for (const city::BuildingPiece& p : pieces) {
        if (p.finish == city::BuildingFinish::Glass) ++glass;
        if (!p.solid && p.finish == city::BuildingFinish::RedTrim) ++trim;
    }
    REQUIRE(glass == 1);
    REQUIRE_MSG(trim >= 6, "window lost its frame or mullions", "window style");
    apricot_test::pass("window style emits an inset pane, frame and mullions");
}

void open_doors_keep_the_collision_gap_and_custom_frame() {
    static constexpr city::BuildingOpening opening{
        "open bank door", city::OpeningKind::Door, 3.0f, 2.4f, 0.0f, 2.8f,
        city::BuildingFinish::Glass, 0, 0, city::BuildingFinish::Steel, false};
    static constexpr city::BuildingWall wall{
        "front", {0.0f, 0.0f}, {6.0f, 0.0f}, 0.0f, 3.2f, 0.24f,
        city::BuildingFinish::Brick, &opening, 1};
    static constexpr city::BuildingPlan plan{
        "open door", &wall, 1, nullptr, 0};

    const std::vector<city::BuildingPiece> pieces = city::bake_building(plan);
    int steel_frame = 0;
    int door_leaf = 0;
    for (const city::BuildingPiece& p : pieces) {
        if (!p.solid && p.finish == city::BuildingFinish::Steel) ++steel_frame;
        if (!p.solid && p.finish == city::BuildingFinish::Glass) ++door_leaf;
        if (p.solid) {
            REQUIRE_MSG(!overlaps(p, 2.0f, 4.0f, 0.1f, 2.7f),
                        "solid wall refilled the open door", p.name);
        }
    }
    REQUIRE_MSG(steel_frame == 3, "open door lost a jamb or head", opening.name);
    REQUIRE_MSG(door_leaf == 0, "leaf=false still baked a closed panel",
                opening.name);
    apricot_test::pass("open doors keep a real gap and their authored frame");
}

void gable_roofs_are_two_real_slopes() {
    static constexpr city::BuildingRoof roof{
        "gable", {0.0f, 0.0f}, 4.0f, 12.0f, 8.0f, 2.0f, 0.25f, 0.5f,
        0.0f, city::RoofStyle::Gable, city::RidgeAxis::AlongX,
        city::BuildingFinish::DarkRoof, city::BuildingFinish::Brick};
    static constexpr city::BuildingPlan plan{"gable", nullptr, 0, &roof, 1};

    const std::vector<city::BuildingPiece> pieces = city::bake_building(plan);
    REQUIRE(pieces.size() == 2u);
    REQUIRE(pieces[0].pitch_deg < 0.0f);
    REQUIRE(pieces[1].pitch_deg > 0.0f);
    REQUIRE_NEAR(pieces[0].depth_m, pieces[1].depth_m, 1e-5);
    REQUIRE_MSG(pieces[0].width_m > roof.width_m,
                "roof overhang was not included", roof.name);
    apricot_test::pass("gable roof bakes as two pitched overhanging panels");
}

void stairs_bake_to_the_requested_landing_height() {
    static constexpr city::BuildingStair stair{
        "outside stair", {4.0f, 6.0f}, 0.10f, 1.8f, 4.0f, 3.2f,
        8, 90.0f, city::BuildingFinish::Concrete, true};
    static constexpr city::BuildingPlan plan{
        "stairs", nullptr, 0, nullptr, 0, nullptr, 0, &stair, 1};

    const std::vector<city::BuildingPiece> pieces = city::bake_building(plan);
    REQUIRE(pieces.size() == 8u);
    REQUIRE_NEAR(pieces.front().height_m, 0.4f, 1e-5);
    REQUIRE_NEAR(pieces.back().bottom_m + pieces.back().height_m, 3.30f,
                 1e-5);
    REQUIRE(pieces.front().centre.x < pieces.back().centre.x);
    REQUIRE_NEAR(pieces.front().centre.z, pieces.back().centre.z, 1e-5);
    REQUIRE(pieces.back().solid);
    apricot_test::pass("creator stairs rise to their exact landing height");
}

void enclosed_gables_meet_the_walls_and_roof_on_both_axes() {
    for (const auto axis : {city::RidgeAxis::AlongX,city::RidgeAxis::AlongZ}) {
        city::BuildingRoof roof{"closed gable",{3.0f,-2.0f},4.0f,12.0f,8.0f,
            2.0f,.18f,.55f,0.0f,city::RoofStyle::Gable,axis,
            city::BuildingFinish::DarkRoof,city::BuildingFinish::Brick,
            .24f,city::BuildingFinish::WarmWall};
        const city::BuildingPlan plan{"closed gable",nullptr,0,&roof,1};
        REQUIRE(city::valid_building_plan(plan));
        const auto pieces = city::bake_building(plan);
        REQUIRE(pieces.size()==6u);
        const bool along_x = axis==city::RidgeAxis::AlongX;
        const float run = (along_x ? roof.depth_m : roof.width_m)*.5f+roof.overhang_m;
        for (std::size_t i : {2u,4u}) {
            const auto& band=pieces[i]; const auto& end=pieces[i+1u];
            REQUIRE(end.shape==city::BuildingPieceShape::GablePrism);
            REQUIRE(band.shape==city::BuildingPieceShape::Box);
            REQUIRE(!end.solid && !band.solid);
            REQUIRE(end.finish==roof.gable_end_finish);
            REQUIRE(band.finish==roof.gable_end_finish);
            REQUIRE_NEAR(end.depth_m,.24f,.0001f);
            REQUIRE_NEAR(end.yaw_deg,along_x ? 90.f : 0.f,.0001f);
            REQUIRE_NEAR(band.bottom_m+band.height_m,end.bottom_m,.0001f);
            REQUIRE(band.bottom_m<roof.bottom_m);
            REQUIRE_NEAR(end.bottom_m+end.height_m,roof.bottom_m+roof.rise_m,.0001f);
            REQUIRE_NEAR(along_x ? std::fabs(end.centre.x-roof.centre.x) :
                std::fabs(end.centre.z-roof.centre.z),
                (along_x ? roof.width_m : roof.depth_m)*.5f,.0001f);
            // Sample the entire end-wall silhouette, not just its apex. The
            // prism touches the panel's centre plane and overlaps its lower
            // half, so no triangular daylight gap survives under either pitch.
            for (int j=0;j<=40;++j) {
                const float x=end.width_m*(static_cast<float>(j)/40.f-.5f);
                const float wall_top=end.bottom_m+end.height_m*(1.f-std::fabs(x)/(end.width_m*.5f));
                const float roof_line=roof.bottom_m+roof.rise_m*(1.f-std::fabs(x)/run);
                REQUIRE_NEAR(wall_top,roof_line,.0001f);
            }
        }
        // Flat roofs and unclosed shelters retain their original primitives.
        roof.gable_end_thickness_m=0.f;
        REQUIRE(city::bake_building(plan).size()==2u);
        roof.gable_end_thickness_m=.24f; roof.style=city::RoofStyle::Flat;
        REQUIRE(city::bake_building(plan).size()==1u);
    }
    apricot_test::pass("closed gable ends meet wall tops and both roof pitches on either ridge axis; other roofs unchanged");
}

}  // namespace

int main() {
    stacked_openings_stay_cut_from_the_wall();
    window_styles_bake_frames_and_mullions();
    open_doors_keep_the_collision_gap_and_custom_frame();
    gable_roofs_are_two_real_slopes();
    enclosed_gables_meet_the_walls_and_roof_on_both_axes();
    stairs_bake_to_the_requested_landing_height();
    return apricot_test::done("building_creator_tests");
}
