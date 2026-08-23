// Bounding boxes, frustum planes and transform composition.
//
// These three carry more load than their size suggests. Culling decides what
// is drawn, ray/box decides what the car touches, and composition decides
// where everything IS. All three fail quietly: a wrong composition order looks
// almost right until something is both far from the origin and rotated, a
// frustum plane mixed up with its neighbour only shows at the edge of the
// screen, and a ray test that mishandles the axis-aligned case still works for
// every ray except the ones a grid actually generates.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdio>

#include "core/aabb.h"
#include "core/frustum.h"
#include "core/transform.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// --- helpers -----------------------------------------------------------------

void require_vec_near(const glm::vec3& a, const glm::vec3& b, float eps,
                      const char* what) {
    REQUIRE_MSG(std::fabs(a.x - b.x) <= eps, what, "x");
    REQUIRE_MSG(std::fabs(a.y - b.y) <= eps, what, "y");
    REQUIRE_MSG(std::fabs(a.z - b.z) <= eps, what, "z");
}

bool mat_near(const glm::mat4& a, const glm::mat4& b, float eps) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::fabs(a[c][r] - b[c][r]) > eps) return false;
        }
    }
    return true;
}

// A camera at the origin looking down -Z. Every frustum case below uses this
// one so the numbers stay comparable.
glm::mat4 test_view_proj() {
    const glm::mat4 proj =
        glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 1.0f, 100.0f);
    const glm::mat4 view =
        glm::lookAt(glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, -1.0f},
                    glm::vec3{0.0f, 1.0f, 0.0f});
    return proj * view;
}

AABB box_at(const glm::vec3& c, float half) {
    return AABB{c - glm::vec3{half}, c + glm::vec3{half}};
}

// --- AABB basics -------------------------------------------------------------

void empty_box_stays_empty() {
    AABB fresh;
    REQUIRE_MSG(!fresh.valid(), "a default box must be empty", "valid");

    // Merging an empty box into anything must not drag the bounds out to
    // infinity, which is what an unguarded min/max against the inverted
    // sentinel would do.
    AABB real = box_at(glm::vec3{1.0f, 2.0f, 3.0f}, 1.0f);
    const AABB merged = real.merged(AABB{});
    require_vec_near(merged.min, real.min, 0.0f, "empty merge moved min");
    require_vec_near(merged.max, real.max, 0.0f, "empty merge moved max");

    // The empty set is contained in everything.
    REQUIRE(real.contains(AABB{}));

    apricot_test::pass("an empty box merges and contains harmlessly");
}

void containment_and_merge() {
    const AABB outer{glm::vec3{-2.0f}, glm::vec3{2.0f}};
    const AABB inner{glm::vec3{-1.0f}, glm::vec3{1.0f}};
    const AABB straddling{glm::vec3{1.0f}, glm::vec3{3.0f}};

    REQUIRE(outer.contains(inner));
    REQUIRE(!inner.contains(outer));
    REQUIRE(!outer.contains(straddling));

    // A box contains itself, and its own faces: the surface is part of the
    // solid. Off-by-one here shows up as objects popping at chunk seams.
    REQUIRE(outer.contains(outer));
    REQUIRE(outer.contains(glm::vec3{2.0f, 2.0f, 2.0f}));
    REQUIRE(!outer.contains(glm::vec3{2.0001f, 0.0f, 0.0f}));

    REQUIRE(outer.intersects(straddling));
    REQUIRE(!inner.intersects(AABB{glm::vec3{5.0f}, glm::vec3{6.0f}}));

    const AABB u = inner.merged(straddling);
    require_vec_near(u.min, glm::vec3{-1.0f}, 0.0f, "merge min");
    require_vec_near(u.max, glm::vec3{3.0f}, 0.0f, "merge max");

    require_vec_near(inner.expanded(0.5f).max, glm::vec3{1.5f}, 1e-6f,
                     "expanded max");
    require_vec_near(inner.closest_point(glm::vec3{5.0f, 0.0f, -5.0f}),
                     glm::vec3{1.0f, 0.0f, -1.0f}, 1e-6f, "closest point");
    require_vec_near(inner.closest_point(glm::vec3{0.25f, 0.0f, 0.0f}),
                     glm::vec3{0.25f, 0.0f, 0.0f}, 1e-6f,
                     "closest point inside is the point itself");

    apricot_test::pass("containment, merge, expand and closest point");
}

void transformed_box_encloses_its_corners() {
    const AABB box{glm::vec3{-1.0f, -2.0f, -3.0f}, glm::vec3{4.0f, 5.0f, 6.0f}};

    Transform t;
    t.position = glm::vec3{7.0f, -3.0f, 2.0f};
    t.set_euler_deg(21.0f, -47.0f, 13.0f);
    t.scale = glm::vec3{1.7f, 0.6f, 2.3f};

    const glm::mat4 m = t.matrix();
    const AABB out = box.transformed(m);

    // Arvo's method is exact for the ENCLOSING box, so every transformed
    // corner must land inside it. Checking against the corners rather than
    // against a second implementation is what makes this a real test.
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner{(i & 1) ? box.max.x : box.min.x,
                               (i & 2) ? box.max.y : box.min.y,
                               (i & 4) ? box.max.z : box.min.z};
        const glm::vec3 moved = glm::vec3{m * glm::vec4{corner, 1.0f}};
        REQUIRE_MSG(out.contains(moved + glm::vec3{0.0f}),
                    "a transformed corner escaped the transformed box",
                    "corner containment");
    }

    apricot_test::pass("transformed() encloses all eight moved corners");
}

// --- AABB ray ----------------------------------------------------------------

void ray_hits_and_misses() {
    const AABB box{glm::vec3{-1.0f}, glm::vec3{1.0f}};

    // Straight in from -X.
    AABB::RayHit h = box.intersect_ray(glm::vec3{-5.0f, 0.0f, 0.0f},
                                       glm::vec3{1.0f, 0.0f, 0.0f});
    REQUIRE(h.hit);
    REQUIRE_NEAR(h.t_near, 4.0, 1e-5);
    REQUIRE_NEAR(h.t_far, 6.0, 1e-5);
    REQUIRE(!h.inside);

    // Same direction, well above the box.
    REQUIRE(!box.intersect_ray(glm::vec3{-5.0f, 5.0f, 0.0f},
                               glm::vec3{1.0f, 0.0f, 0.0f}).hit);

    // Behind the ray. The default range starts at 0, so a box the ray is
    // travelling AWAY from must miss rather than report a negative hit.
    REQUIRE(!box.intersect_ray(glm::vec3{5.0f, 0.0f, 0.0f},
                               glm::vec3{1.0f, 0.0f, 0.0f}).hit);

    // Stopping short. A hit-scan with a range must not report the wall behind
    // its own maximum distance.
    REQUIRE(!box.intersect_ray(glm::vec3{-5.0f, 0.0f, 0.0f},
                               glm::vec3{1.0f, 0.0f, 0.0f}, 0.0f, 2.0f).hit);

    // Starting inside.
    h = box.intersect_ray(glm::vec3{0.0f}, glm::vec3{1.0f, 0.0f, 0.0f});
    REQUIRE(h.hit);
    REQUIRE(h.inside);
    REQUIRE_NEAR(h.t_near, 0.0, 1e-6);
    REQUIRE_NEAR(h.t_far, 1.0, 1e-5);

    // Diagonal, through two corners.
    h = box.intersect_ray(glm::vec3{-3.0f, -3.0f, -3.0f},
                          glm::normalize(glm::vec3{1.0f, 1.0f, 1.0f}));
    REQUIRE_MSG(h.hit, "a corner-to-corner diagonal missed", "diagonal");

    // Diagonal that passes beside the box.
    REQUIRE(!box.intersect_ray(glm::vec3{-3.0f, 4.0f, -3.0f},
                               glm::normalize(glm::vec3{1.0f, 1.0f, 1.0f})).hit);

    // An empty box is hit by nothing.
    REQUIRE(!AABB{}.intersect_ray(glm::vec3{0.0f},
                                  glm::vec3{1.0f, 0.0f, 0.0f}).hit);

    apricot_test::pass("ray hits, misses, range limits and inside starts");
}

// THE AXIS-ALIGNED CASES.
//
// A ray running exactly along a face has a zero direction component AND an
// origin exactly on a slab bound. Under the textbook `1/dir` form that is
// 0 * inf == NaN, and whether the NaN then escapes depends on the argument
// order of std::min/std::max (see kRayParallelEpsilon in core/aabb.h — this
// was measured, not guessed). When it does escape, the result is not a clean
// miss: it is a HIT whose t is NaN, which the caller multiplies into a
// position. So these cases assert the t values are FINITE, not merely that
// `hit` came back true — checking `hit` alone would sail straight past it.
//
// It only goes wrong for axis-aligned rays, which is to say for every ray a
// grid march, a downward ground probe or an axis-aligned sweep produces.
void ray_edge_on_cases() {
    const AABB box{glm::vec3{-1.0f}, glm::vec3{1.0f}};
    const glm::vec3 dir{1.0f, 0.0f, 0.0f};

    auto require_clean_hit = [](const AABB::RayHit& hit, const char* which) {
        REQUIRE_MSG(hit.hit, "an edge-on ray reported a miss", which);
        REQUIRE_MSG(std::isfinite(hit.t_near), "t_near came back NaN or inf",
                    which);
        REQUIRE_MSG(std::isfinite(hit.t_far), "t_far came back NaN or inf",
                    which);
        REQUIRE_MSG(hit.t_near <= hit.t_far, "the hit interval is inverted",
                    which);
    };

    // Grazing exactly along the +Y face. The face is part of the box: HIT.
    AABB::RayHit h = box.intersect_ray(glm::vec3{-5.0f, 1.0f, 0.0f}, dir);
    require_clean_hit(h, "top face");
    REQUIRE_NEAR(h.t_near, 4.0, 1e-5);
    REQUIRE_NEAR(h.t_far, 6.0, 1e-5);

    // And along the -Y face, where the NaN lands on the OTHER slab bound.
    require_clean_hit(box.intersect_ray(glm::vec3{-5.0f, -1.0f, 0.0f}, dir),
                      "bottom face");

    // Exactly along an EDGE: on the boundary in two axes at once.
    require_clean_hit(box.intersect_ray(glm::vec3{-5.0f, 1.0f, 1.0f}, dir),
                      "top-front edge");
    require_clean_hit(box.intersect_ray(glm::vec3{-5.0f, -1.0f, -1.0f}, dir),
                      "bottom-back edge");

    // A hair outside the same face must still miss. The inclusive test must
    // not have become a sloppy one.
    REQUIRE_MSG(!box.intersect_ray(glm::vec3{-5.0f, 1.001f, 0.0f}, dir).hit,
                "a ray just past the face reported a hit", "edge-on epsilon");

    // Parallel on every axis at once: a zero-length direction. Inside hits,
    // outside misses, and neither divides by zero.
    REQUIRE(box.intersect_ray(glm::vec3{0.0f}, glm::vec3{0.0f}).hit);
    REQUIRE(!box.intersect_ray(glm::vec3{9.0f, 0.0f, 0.0f}, glm::vec3{0.0f}).hit);

    // A direction small enough that its reciprocal would overflow. Treated as
    // parallel rather than producing infinities.
    REQUIRE(box.intersect_ray(glm::vec3{-5.0f, 0.0f, 0.0f},
                              glm::vec3{1.0f, 1e-30f, 0.0f}).hit);

    apricot_test::pass("edge-on rays hit, and near-parallel ones do not blow up");
}

// --- frustum -----------------------------------------------------------------

// Rejection on ALL SIX planes, each one on its own.
//
// A test that only checks "far away is culled" passes with two planes swapped,
// or with one plane simply wrong, because the others cover for it. So each
// case here pushes a point out through exactly ONE plane and asserts that that
// plane — by name — is the one that rejects it, while the other five still
// report the point as inside.
void every_frustum_plane_rejects_on_its_own() {
    const Frustum f = Frustum::from_view_proj(test_view_proj());

    // Comfortably inside all six.
    const glm::vec3 middle{0.0f, 0.0f, -20.0f};
    for (int i = 0; i < Frustum::kPlaneCount; ++i) {
        REQUIRE_MSG(
            f.distance_to(static_cast<Frustum::PlaneIndex>(i), middle) > 0.0f,
            "the middle of the frustum was outside a plane", "setup");
    }
    REQUIRE_MSG(!f.cull(box_at(middle, 0.5f)), "a centred box was culled",
                "setup");
    REQUIRE(f.contains(middle));

    const char* names[Frustum::kPlaneCount] = {"left", "right", "bottom",
                                               "top",  "near",  "far"};

    // How far past its own plane to put the test point. SMALL on purpose. The
    // four side planes all meet at the camera, so a generous step out through
    // the NEAR plane sails straight through the apex and lands outside five
    // planes at once — which tests nothing, because any one of them could be
    // doing the rejecting. The box and sphere below are smaller than this
    // margin so they sit wholly outside rather than straddling.
    constexpr float kMargin = 0.25f;
    constexpr float kProbeRadius = 0.05f;

    for (int i = 0; i < Frustum::kPlaneCount; ++i) {
        const auto plane = static_cast<Frustum::PlaneIndex>(i);
        const glm::vec3 n{f.planes[plane]};

        // Drop perpendicularly onto this plane, then step just past it. The
        // move is perpendicular to THIS plane, so it disturbs the others as
        // little as geometry allows.
        const float d = f.distance_to(plane, middle);
        const glm::vec3 outside = middle - n * (d + kMargin);

        REQUIRE_MSG(f.distance_to(plane, outside) < 0.0f,
                    "the point did not end up outside its own plane", names[i]);

        for (int j = 0; j < Frustum::kPlaneCount; ++j) {
            if (j == i) continue;
            REQUIRE_MSG(
                f.distance_to(static_cast<Frustum::PlaneIndex>(j), outside) >
                    0.0f,
                "the point left more than one plane; the case is not isolated",
                names[i]);
        }

        REQUIRE_MSG(!f.contains(outside), "contains() accepted an outside point",
                    names[i]);
        REQUIRE_MSG(f.cull(box_at(outside, kProbeRadius)),
                    "a box outside this plane was not culled", names[i]);
        REQUIRE_MSG(f.cull_sphere(outside, kProbeRadius),
                    "a sphere outside this plane was not culled", names[i]);

        // A sphere big enough to reach back inside must NOT be culled: the
        // test is on the volume, not on the centre.
        REQUIRE_MSG(!f.cull_sphere(outside, 4.0f),
                    "a sphere straddling the plane was culled", names[i]);
    }

    apricot_test::pass("each of the six planes rejects on its own");
}

void frustum_edge_conditions() {
    const Frustum f = Frustum::from_view_proj(test_view_proj());

    // An inverted box has no volume and must be culled, not drawn forever.
    REQUIRE_MSG(f.cull(AABB{}), "an empty box was reported visible",
                "invalid bounds");

    // A box so large it swallows the camera is visible from the inside.
    REQUIRE(!f.cull(AABB{glm::vec3{-500.0f}, glm::vec3{500.0f}}));

    // Straddling the near plane counts as visible: the positive-vertex test is
    // conservative on purpose, because drawing one extra box is free and
    // dropping a visible one is a hole in the world.
    REQUIRE(!f.cull(AABB{glm::vec3{-1.0f, -1.0f, -2.0f},
                         glm::vec3{1.0f, 1.0f, 2.0f}}));

    // intersects() is exactly the complement of cull().
    const AABB somewhere = box_at(glm::vec3{0.0f, 0.0f, -30.0f}, 1.0f);
    REQUIRE(f.intersects(somewhere) == !f.cull(somewhere));

    // Normalised planes mean plane distances are real metres. The near plane
    // sits 1 m out, so a point 20 m down -Z is 19 m past it.
    REQUIRE_NEAR(f.distance_to(Frustum::kNear, glm::vec3{0.0f, 0.0f, -20.0f}),
                 19.0, 1e-3);

    apricot_test::pass("empty, enclosing and straddling boxes behave");
}

// --- transform ---------------------------------------------------------------

void matrix_is_translate_rotate_scale() {
    Transform t;
    t.position = glm::vec3{3.0f, -4.0f, 5.0f};
    t.set_euler_deg(10.0f, 25.0f, -40.0f);
    t.scale = glm::vec3{2.0f, 0.5f, 1.25f};

    const glm::mat4 expected =
        glm::translate(glm::mat4{1.0f}, t.position) *
        glm::mat4_cast(t.rotation) *
        glm::scale(glm::mat4{1.0f}, t.scale);

    REQUIRE_MSG(mat_near(t.matrix(), expected, 1e-5f),
                "matrix() is not T * R * S", "composition order");

    // The spelled-out point transform must agree with the matrix it stands for.
    const glm::vec3 p{1.0f, 2.0f, -3.0f};
    require_vec_near(t.transform_point(p),
                     glm::vec3{t.matrix() * glm::vec4{p, 1.0f}}, 1e-4f,
                     "transform_point disagrees with matrix()");

    // A direction ignores the translation.
    require_vec_near(t.transform_direction(p),
                     glm::vec3{t.matrix() * glm::vec4{p, 0.0f}}, 1e-4f,
                     "transform_direction picked up the translation");

    apricot_test::pass("matrix() is T * R * S and the helpers agree with it");
}

// THE ORDER TEST.
//
// A reversed composition is right at the origin, slightly wrong with a small
// rotation, and only obviously wrong once something is both FAR from the
// origin and ROTATED. So the pair below is deliberately both: reverse the
// arguments and this test fails immediately instead of eventually.
void composition_order_is_parent_times_child() {
    Transform parent;
    parent.position = glm::vec3{10.0f, 0.0f, 0.0f};
    parent.set_euler_deg(0.0f, 90.0f, 0.0f);

    Transform child;
    child.position = glm::vec3{0.0f, 0.0f, -2.0f};
    child.set_euler_deg(0.0f, 45.0f, 0.0f);

    const Transform world = combine(parent, child);

    // 1. Against the matrix product it claims to stand for.
    REQUIRE_MSG(mat_near(world.matrix(), parent.matrix() * child.matrix(), 1e-4f),
                "combine(parent, child) != parent.matrix() * child.matrix()",
                "order");

    // 2. And NOT against the reverse, so the case cannot pass by symmetry. If
    //    this ever starts failing, the pair above stopped being asymmetric and
    //    the test above stopped proving anything.
    REQUIRE_MSG(!mat_near(world.matrix(), child.matrix() * parent.matrix(), 1e-4f),
                "the test pair is symmetric and proves nothing", "order");

    // 3. Hand-computed, so a systematic error in glm and in the helper cannot
    //    agree with each other. A 90-degree yaw sends the child's local -Z
    //    offset onto world -X, putting it at x = 10 - 2 = 8.
    require_vec_near(world.position, glm::vec3{8.0f, 0.0f, 0.0f}, 1e-4f,
                     "the child did not land where a 90-degree yaw puts it");

    // 4. Rotations add: 90 + 45 = 135 degrees of yaw.
    Transform expected_rot;
    expected_rot.set_euler_deg(0.0f, 135.0f, 0.0f);
    require_vec_near(world.forward(), expected_rot.forward(), 1e-4f,
                     "composed rotation is not parent * child");

    // 5. Composing then transforming == transforming twice, in that order.
    const glm::vec3 p{0.5f, 1.5f, -0.25f};
    require_vec_near(world.transform_point(p),
                     parent.transform_point(child.transform_point(p)), 1e-4f,
                     "combine is not equivalent to applying both in order");

    // 6. operator* is the same thing spelled differently.
    REQUIRE(mat_near((parent * child).matrix(), world.matrix(), 0.0f));

    apricot_test::pass("composition is parent * child, four ways");
}

void inverse_round_trips() {
    Transform t;
    t.position = glm::vec3{-6.0f, 12.0f, 3.5f};
    t.set_euler_deg(33.0f, -71.0f, 19.0f);
    t.scale = glm::vec3{2.5f};  // uniform: inverse() is exact here

    const Transform inv = t.inverse();

    const glm::vec3 p{1.0f, -2.0f, 3.0f};
    require_vec_near(inv.transform_point(t.transform_point(p)), p, 1e-3f,
                     "inverse did not undo the transform");

    REQUIRE_MSG(mat_near(combine(t, inv).matrix(), glm::mat4{1.0f}, 1e-4f),
                "t composed with its inverse is not the identity", "inverse");

    // relative_to is the other half of the same idea: it recovers the child.
    Transform child;
    child.position = glm::vec3{0.0f, 1.0f, -4.0f};
    child.set_euler_deg(0.0f, 30.0f, 0.0f);
    const Transform world = combine(t, child);
    const Transform recovered = relative_to(world, t);

    require_vec_near(recovered.position, child.position, 1e-3f,
                     "relative_to did not recover the child position");
    require_vec_near(recovered.forward(), child.forward(), 1e-3f,
                     "relative_to did not recover the child rotation");

    apricot_test::pass("inverse and relative_to round-trip under uniform scale");
}

// Pins the documented LIMIT of TRS composition rather than pretending it does
// not exist. A non-uniform parent scale combined with a rotated child produces
// shear, and a position/rotation/scale triple has nowhere to put shear. If
// this ever starts passing, combine() has quietly become exact and the comment
// in transform.h is now wrong.
void non_uniform_scale_cannot_represent_shear() {
    Transform parent;
    parent.scale = glm::vec3{3.0f, 1.0f, 1.0f};  // deliberately non-uniform

    Transform child;
    child.set_euler_deg(0.0f, 45.0f, 0.0f);  // rotated, so the shear appears

    REQUIRE_MSG(
        !mat_near(combine(parent, child).matrix(),
                  parent.matrix() * child.matrix(), 1e-3f),
        "combine() is exact under shear; transform.h's caveat is now wrong",
        "documented limit");

    // With the child unrotated there is no shear, so it IS exact.
    Transform straight;
    straight.position = glm::vec3{1.0f, 2.0f, 3.0f};
    REQUIRE_MSG(mat_near(combine(parent, straight).matrix(),
                         parent.matrix() * straight.matrix(), 1e-4f),
                "non-uniform scale broke even the shear-free case",
                "documented limit");

    apricot_test::pass("the non-uniform-scale caveat is real and bounded");
}

}  // namespace

int main() {
    std::printf("math_tests\n");
    empty_box_stays_empty();
    containment_and_merge();
    transformed_box_encloses_its_corners();
    ray_hits_and_misses();
    ray_edge_on_cases();
    every_frustum_plane_rejects_on_its_own();
    frustum_edge_conditions();
    matrix_is_translate_rotate_scale();
    composition_order_is_parent_times_child();
    inverse_round_trips();
    non_uniform_scale_cannot_represent_shear();
    return apricot_test::done("math_tests");
}
