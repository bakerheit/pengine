// The Mistral's folding canvas top: the toggle, the two-bow fold, and the
// claim that both bows stay rigid once the body has been stretched onto the
// shared player chassis.
//
// The fold is presentation, not simulation — it moves no dimension the physics
// reads and it is not in InputFrame. What it must not do is shear, come apart
// at the joint, or leave the canvas somewhere the body is solid. All three are
// pinned here against the actual cooked meshes.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <vector>

#include "app/mistral_soft_top.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// The same fit player_car_visual applies: half-track on X, wheelbase on Y and
// Z. Y and Z share a factor, which is the whole reason the hinge axis is X.
glm::vec3 fitted_body_scale(PlayerCarId model) {
    const auto& d = player_car_definition(model);
    const float track = d.physical_half_track > 0.f ? d.physical_half_track : .78f;
    const float wheelbase =
        d.physical_half_wheelbase > 0.f ? 2.f * d.physical_half_wheelbase : 2.7f;
    const float length = wheelbase / (d.wheel_front_z + d.wheel_rear_z);
    return {track / d.wheel_x, length, length};
}

Transform fitted_body(float yaw_degrees, float pitch_degrees) {
    Transform body;
    body.scale = fitted_body_scale(PlayerCarId::VesperMistral);
    body.position = {41.f, 6.5f, -12.f};
    body.set_euler_deg(pitch_degrees, yaw_degrees, 0.f);
    return body;
}

void only_the_mistral_has_a_top() {
    int convertibles = 0;
    for (const auto& car : kPlayerCars) {
        const bool expected = car.id == PlayerCarId::VesperMistral;
        REQUIRE_MSG(is_convertible(car.id) == expected,
                    "a car gained or lost a folding top", car.model);
        convertibles += expected ? 1 : 0;
    }
    REQUIRE(convertibles == 1);
    // Old checkpoints store the legacy cruiser slot; it must not answer yes.
    REQUIRE(!is_convertible(PlayerCarId::LegacyCruiser91CSlot));
    apricot_test::pass("exactly one catalog entry is a convertible");
}

void the_toggle_is_a_latch_not_a_switch() {
    MistralSoftTop top;
    REQUIRE(top.stowed == 1.f && top.target == 1.f);
    REQUIRE(!mistral_soft_top_moving(top));

    // One press starts it moving; the canvas does not teleport.
    top = step_mistral_soft_top(top, true, 1.f / 60.f);
    REQUIRE(top.target == 0.f);
    REQUIRE(top.stowed < 1.f && top.stowed > .98f);
    REQUIRE(mistral_soft_top_moving(top));

    // It arrives in kMistralTopSeconds, and then stops dead rather than
    // overshooting past the latch.
    int steps = 1;
    while (mistral_soft_top_moving(top) && steps < 10000) {
        top = step_mistral_soft_top(top, false, 1.f / 60.f);
        ++steps;
    }
    REQUIRE_NEAR(static_cast<double>(steps) / 60.0,
                 static_cast<double>(kMistralTopSeconds), .05);
    REQUIRE(top.stowed == 0.f);
    for (int i = 0; i < 60; ++i) top = step_mistral_soft_top(top, false, 1.f / 60.f);
    REQUIRE(top.stowed == 0.f);

    // A press halfway through reverses from where it is, with no jump.
    top = step_mistral_soft_top(top, true, 1.f / 60.f);
    for (int i = 0; i < 60; ++i) top = step_mistral_soft_top(top, false, 1.f / 60.f);
    const float halfway = top.stowed;
    REQUIRE(halfway > .2f && halfway < .8f);
    top = step_mistral_soft_top(top, true, 1.f / 60.f);
    REQUIRE(top.target == 0.f);
    REQUIRE(top.stowed < halfway && top.stowed > halfway - .05f);

    // A frame that owes no time, or a garbage dt, moves nothing.
    const MistralSoftTop held = top;
    REQUIRE(step_mistral_soft_top(held, false, 0.f).stowed == held.stowed);
    REQUIRE(step_mistral_soft_top(held, false, -1.f).stowed == held.stowed);
    REQUIRE(step_mistral_soft_top(held, false,
                                  std::numeric_limits<float>::quiet_NaN())
                .stowed == held.stowed);
    apricot_test::pass("the top latches at both ends and reverses from where it is");
}

// Rigid means: the hinge point does not move, and every pairwise distance in
// the panel is preserved. A sheared panel passes an eyeball and fails this.
void rigid(const Transform& before, const Transform& after, const glm::vec3& pivot,
           const std::vector<glm::vec3>& points, const char* case_name) {
    REQUIRE_MSG(glm::length(after.transform_point(pivot) -
                            before.transform_point(pivot)) < 1e-4f,
                "the hinge point moved", case_name);
    for (std::size_t i = 0; i < points.size(); ++i)
        for (std::size_t j = i + 1; j < points.size(); ++j) {
            const float was = glm::length(before.transform_point(points[i]) -
                                          before.transform_point(points[j]));
            const float now = glm::length(after.transform_point(points[i]) -
                                          after.transform_point(points[j]));
            REQUIRE_MSG(std::abs(was - now) < 1e-3f * (1.f + was),
                        "the canvas sheared as it folded", case_name);
        }
}

void the_fold_is_rigid_on_a_fitted_body() {
    // Deliberately off-axis body poses: a fold that is only rigid at the
    // origin, unrotated, is the classic way this goes wrong unnoticed.
    for (const auto body : {fitted_body(0.f, 0.f), fitted_body(117.f, 0.f),
                            fitted_body(-64.f, 11.f)}) {
        // Latched up, both bows sit exactly where they were cooked.
        REQUIRE(glm::length(mistral_top_rear_transform(body, 0.f).position -
                            body.position) < 1e-6f);
        REQUIRE(glm::length(mistral_top_front_transform(body, 0.f).position -
                            body.position) < 1e-6f);

        const std::vector<glm::vec3> rear_points{
            kMistralTopRearHinge, kMistralTopJoint, {.78f, .95f, -.90f},
            {-.78f, .95f, -.90f}, {.765f, 1.3f, -.48f}};
        const std::vector<glm::vec3> front_points{
            kMistralTopJoint, {.70f, 1.42f, .05f}, {-.70f, 1.42f, .05f},
            {0.f, 1.462f, -.20f}};
        for (float f = 0.f; f <= 1.0001f; f += .0625f) {
            rigid(body, mistral_top_rear_transform(body, f), kMistralTopRearHinge,
                  rear_points, "rear bow");
            // The front bow rides the rear one, so its rigid parent is the
            // rear bow's pose at the same fraction, not the body.
            const Transform rear = mistral_top_rear_transform(body, f);
            rigid(rear, mistral_top_front_transform(body, f), kMistralTopJoint,
                  front_points, "front bow");
            // And the seam stays welded: the joint is one point, not two.
            REQUIRE(glm::length(
                        mistral_top_front_transform(body, f)
                            .transform_point(kMistralTopJoint) -
                        rear.transform_point(kMistralTopJoint)) < 1e-4f);
        }
        // Out-of-range and non-finite fractions clamp rather than fling the
        // canvas off the car.
        REQUIRE(glm::length(mistral_top_front_transform(body, 4.f).position -
                            mistral_top_front_transform(body, 1.f).position) < 1e-5f);
        REQUIRE(glm::length(
                    mistral_top_front_transform(
                        body, std::numeric_limits<float>::quiet_NaN())
                        .position -
                    body.position) < 1e-6f);
    }
    apricot_test::pass("both bows stay rigid and welded at the joint on a fitted, rotated body");
}

// The cooked canvas itself. Skipped rather than failed when the models have not
// been cooked on this machine, the same way the driver-pose suite treats them.
void the_cooked_canvas_folds_where_the_body_is_hollow() {
    const std::string root = "models/vehicles/vesper_mistral/";
    StaticEmesh rear_bow, front_bow;
    if (!std::filesystem::is_regular_file(asset_path(root + "soft_top_rear.emesh")) ||
        !read_static_emesh(asset_path(root + "soft_top_rear.emesh"), rear_bow) ||
        !read_static_emesh(asset_path(root + "soft_top_front.emesh"), front_bow)) {
        std::printf("  --  cooked Mistral canvas absent; skipping the mesh contract\n");
        return;
    }
    // The two bows meet at the joint station and nowhere else overlap in Z.
    REQUIRE_NEAR(front_bow.bounds.min.z, kMistralTopJoint.z, .001f);
    REQUIRE_NEAR(rear_bow.bounds.max.z, kMistralTopJoint.z, .001f);
    // Raised, the canvas reaches the windshield header and the bulkhead, and
    // stays inside the deck's own half-width so it can fold into it.
    REQUIRE_NEAR(front_bow.bounds.max.y, 1.462f, .01f);
    REQUIRE(front_bow.bounds.max.z > 0.f);
    REQUIRE_NEAR(rear_bow.bounds.min.z, -.90f, .001f);
    REQUIRE(std::max(front_bow.bounds.max.x, rear_bow.bounds.max.x) <= .781f);

    Transform body;  // source frame: compare against the cooked numbers directly
    const Transform rear = mistral_top_rear_transform(body, 1.f);
    const Transform front = mistral_top_front_transform(body, 1.f);
    float highest = -1e9f, furthest_forward = -1e9f, furthest_back = 1e9f;
    for (const auto& pair : {std::pair<const StaticEmesh*, const Transform*>{&rear_bow, &rear},
                             {&front_bow, &front}})
        for (const auto& vertex : pair.first->vertices) {
            const glm::vec3 p =
                pair.second->transform_point({vertex.px, vertex.py, vertex.pz});
            highest = std::max(highest, p.y);
            furthest_forward = std::max(furthest_forward, p.z);
            furthest_back = std::min(furthest_back, p.z);
        }
    // Stowed it is behind the bulkhead, below the seat backs, and on the car.
    REQUIRE_MSG(furthest_forward < -.86f,
                "the stowed canvas still reaches into the cockpit", "stow");
    REQUIRE_MSG(highest < 1.10f,
                "the stowed canvas stands proud of the seat backs", "stow");
    REQUIRE_MSG(furthest_back > -2.0f,
                "the stowed canvas hangs off the tail", "stow");
    // Raised, it is a roof: higher than anything the stowed stack reaches.
    REQUIRE(front_bow.bounds.max.y > highest + .3f);
    apricot_test::pass("the cooked canvas folds behind the seats and clears the cockpit");
}

}  // namespace

int main() {
    only_the_mistral_has_a_top();
    the_toggle_is_a_latch_not_a_switch();
    the_fold_is_rigid_on_a_fitted_body();
    the_cooked_canvas_folds_where_the_body_is_hollow();
    return apricot_test::done("mistral_soft_top_tests");
}
