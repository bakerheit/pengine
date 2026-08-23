// Procedural geometry and the per-instance normal matrix.
//
// Both things under test fail SILENTLY and both are invisible in a screenshot
// until you already suspect them:
//
//   * Winding. A box wound clockwise is perfectly valid geometry that simply
//     is not there once GL_CULL_FACE goes on. It will look fine in whatever
//     test scene had culling off.
//   * The normal matrix. mat3(model) is right for uniform scale and WRONG for
//     everything else, and the error reads as "the lighting looks a bit off"
//     rather than as a bug, so it survives review indefinitely.
//
// Neither can be checked by eye, so they get checked here, against the real
// generators the renderer uploads.

#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

#include "gfx/instance.h"
#include "gfx/primitives.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// Geometric normal of triangle (a, b, c) under GL's counter-clockwise front
// face convention.
glm::vec3 face_normal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    return glm::normalize(glm::cross(b - a, c - a));
}

void every_box_triangle_faces_outward() {
    const glm::vec3 half{1.5f, 0.5f, 3.0f};
    const MeshData box = make_box(half);

    REQUIRE_MSG(box.vertices.size() == 24,
                "24 vertices: four per face, so normals stay hard", "box");
    REQUIRE_MSG(box.indices.size() == 36, "12 triangles", "box");

    for (std::size_t i = 0; i + 2 < box.indices.size(); i += 3) {
        const MeshVertex& v0 = box.vertices[box.indices[i]];
        const MeshVertex& v1 = box.vertices[box.indices[i + 1]];
        const MeshVertex& v2 = box.vertices[box.indices[i + 2]];

        const glm::vec3 geo = face_normal(v0.position, v1.position, v2.position);

        // The winding's own normal must agree with the stored vertex normal.
        // Disagreement means the triangle is wound backwards and will vanish
        // the moment back-face culling is switched on.
        REQUIRE_MSG(glm::dot(geo, v0.normal) > 0.99f,
                    "triangle is wound clockwise (it will be culled away)",
                    "winding");

        // And that normal must point away from the centre, not into it.
        const glm::vec3 centroid =
            (v0.position + v1.position + v2.position) / 3.0f;
        REQUIRE_MSG(glm::dot(geo, glm::normalize(centroid)) > 0.0f,
                    "face normal points inward", "winding");
    }

    for (const MeshVertex& v : box.vertices) {
        REQUIRE_MSG(std::fabs(glm::length(v.normal) - 1.0f) < 1e-5f,
                    "vertex normals must be unit length", "box");
    }

    REQUIRE_NEAR(static_cast<double>(box.bounds.min.x), -1.5, 1e-5);
    REQUIRE_NEAR(static_cast<double>(box.bounds.max.z), 3.0, 1e-5);
    REQUIRE(box.bounds.valid());
    apricot_test::pass("box: 12 triangles, all wound outward, bounds exact");
}

void the_plane_faces_up() {
    const MeshData plane = make_plane(10.0f, 4);

    REQUIRE(plane.vertices.size() == 25u);   // 5x5 lattice
    REQUIRE(plane.indices.size() == 96u);    // 16 quads, 2 triangles, 3 indices

    for (std::size_t i = 0; i + 2 < plane.indices.size(); i += 3) {
        const glm::vec3 geo =
            face_normal(plane.vertices[plane.indices[i]].position,
                        plane.vertices[plane.indices[i + 1]].position,
                        plane.vertices[plane.indices[i + 2]].position);
        REQUIRE_MSG(geo.y > 0.99f,
                    "ground triangle is wound downward; it would be culled from "
                    "above, which is the only place anyone looks at it",
                    "plane");
    }

    REQUIRE_NEAR(static_cast<double>(plane.bounds.min.x), -10.0, 1e-5);
    REQUIRE_NEAR(static_cast<double>(plane.bounds.max.z), 10.0, 1e-5);

    // A degenerate subdivision must still produce a drawable plane rather than
    // an empty buffer that silently draws nothing.
    const MeshData one = make_plane(1.0f, 0);
    REQUIRE(one.indices.size() == 6u);
    apricot_test::pass("plane faces +Y everywhere, and a 0-cell plane is legal");
}

void the_normal_matrix_survives_non_uniform_scale() {
    // The case mat3(model) gets wrong: stretch one axis and the surface tangent
    // and its normal stop being perpendicular under the naive transform.
    const glm::mat4 model =
        glm::scale(glm::translate(glm::mat4{1.0f}, glm::vec3{4.0f, 0.0f, -2.0f}),
                   glm::vec3{5.0f, 1.0f, 0.5f});

    const InstanceData inst = make_instance(model, glm::vec4{1.0f}, glm::vec2{1.0f});
    const glm::mat3 normal_matrix{glm::vec3(inst.normal_c0),
                                  glm::vec3(inst.normal_c1),
                                  glm::vec3(inst.normal_c2)};

    // A 45-degree face: normal and in-plane tangent, both unit, perpendicular.
    const glm::vec3 n = glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f});
    const glm::vec3 tangent = glm::normalize(glm::vec3{-1.0f, 1.0f, 0.0f});
    REQUIRE_NEAR(static_cast<double>(glm::dot(n, tangent)), 0.0, 1e-6);

    // THE property. A correctly transformed normal stays perpendicular to the
    // correspondingly transformed surface; that is the definition, and it is
    // what the inverse-transpose is for.
    const glm::vec3 n_correct = glm::normalize(normal_matrix * n);
    const glm::vec3 t_world = glm::mat3(model) * tangent;
    REQUIRE_NEAR(static_cast<double>(glm::dot(n_correct, glm::normalize(t_world))),
                 0.0, 1e-5);

    // And the naive shortcut demonstrably fails it, so this test is proving
    // something rather than restating the implementation.
    const glm::vec3 n_naive = glm::normalize(glm::mat3(model) * n);
    REQUIRE_MSG(std::fabs(glm::dot(n_naive, glm::normalize(t_world))) > 0.1f,
                "mat3(model) should visibly skew this normal; if it does not, "
                "the test case is no longer non-uniform enough to prove anything",
                "normal-matrix");

    apricot_test::pass("inverse-transpose keeps normals perpendicular; "
                       "mat3(model) does not");
}

void a_uniform_scale_is_the_easy_case_and_must_still_be_right() {
    const glm::mat4 model = glm::scale(glm::mat4{1.0f}, glm::vec3{3.0f});
    const InstanceData inst = make_instance(model, glm::vec4{1.0f}, glm::vec2{1.0f});
    const glm::mat3 normal_matrix{glm::vec3(inst.normal_c0),
                                  glm::vec3(inst.normal_c1),
                                  glm::vec3(inst.normal_c2)};

    for (const glm::vec3 n : {glm::vec3{1, 0, 0}, glm::vec3{0, 1, 0},
                              glm::normalize(glm::vec3{1, 2, 3})}) {
        const glm::vec3 out = glm::normalize(normal_matrix * n);
        REQUIRE_MSG(glm::dot(out, n) > 0.9999f,
                    "uniform scale must not rotate a normal", "uniform");
    }
    apricot_test::pass("uniform scale leaves normals exactly where they were");
}

void the_instance_record_carries_what_the_batch_key_refuses_to() {
    // scene/draw_batch.h deliberately keeps tint and uv_scale OUT of the batch
    // key so nodes differing only in those still collapse into one draw. That
    // only works if they actually ride the instance record, so check they
    // survive the trip unmodified.
    const glm::vec4 tint{0.25f, 0.5f, 0.75f, 0.9f};
    const glm::vec2 uv{3.0f, 7.0f};
    const InstanceData inst = make_instance(glm::mat4{1.0f}, tint, uv);
    REQUIRE(inst.tint == tint);
    REQUIRE(inst.uv_scale == uv);
    REQUIRE(inst.model == glm::mat4{1.0f});
    apricot_test::pass("tint and uv_scale ride the instance, not a uniform");
}

}  // namespace

int main() {
    every_box_triangle_faces_outward();
    the_plane_faces_up();
    the_normal_matrix_survives_non_uniform_scale();
    a_uniform_scale_is_the_easy_case_and_must_still_be_right();
    the_instance_record_carries_what_the_batch_key_refuses_to();
    return apricot_test::done("render_geometry_tests");
}
