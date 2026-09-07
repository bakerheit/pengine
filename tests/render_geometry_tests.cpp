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
#include <fstream>
#include <iterator>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include "core/asset_root.h"
#include "gfx/instance.h"
#include "gfx/lighting.h"
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

void gable_prism_is_closed_and_faces_outward() {
    const auto mesh=make_gable_prism();
    REQUIRE(mesh.indices.size()==24u);
    float volume=0.f;
    for(std::size_t i=0;i<mesh.indices.size();i+=3u) {
        const auto& a=mesh.vertices[mesh.indices[i]];
        const auto& b=mesh.vertices[mesh.indices[i+1u]];
        const auto& c=mesh.vertices[mesh.indices[i+2u]];
        const auto normal=face_normal(a.position,b.position,c.position);
        REQUIRE(glm::dot(normal,a.normal)>.999f);
        REQUIRE(glm::dot(normal,(a.position+b.position+c.position)/3.f)>0.f);
        volume+=glm::dot(a.position,glm::cross(b.position,c.position))/6.f;
        // Every geometric edge is paired with exactly one reversed edge.
        // Checking positions welds the hard-normal face splits for topology.
        for(std::size_t e=0;e<3u;++e) {
            const auto p=mesh.vertices[mesh.indices[i+e]].position;
            const auto q=mesh.vertices[mesh.indices[i+(e+1u)%3u]].position;
            int reverse=0;
            for(std::size_t j=0;j<mesh.indices.size();j+=3u)
                for(std::size_t k=0;k<3u;++k) {
                    const auto r=mesh.vertices[mesh.indices[j+k]].position;
                    const auto s=mesh.vertices[mesh.indices[j+(k+1u)%3u]].position;
                    if(glm::all(glm::equal(p,s)) && glm::all(glm::equal(q,r))) ++reverse;
                }
            REQUIRE(reverse==1);
        }
    }
    REQUIRE_NEAR(volume,.5f,.0001f);
    for(int axis=0;axis<3;++axis) {
        REQUIRE_NEAR(mesh.bounds.min[axis],-.5f,.0001f);
        REQUIRE_NEAR(mesh.bounds.max[axis],.5f,.0001f);
    }
    apricot_test::pass("gable prism: closed eight-triangle volume, exact unit bounds and outward CCW faces");
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

void flat_decals_have_no_headlight_catching_sides() {
    const MeshData decal = make_decal_quad();
    REQUIRE(decal.vertices.size() == 4u);
    REQUIRE(decal.indices.size() == 6u);
    REQUIRE(decal.bounds.valid());
    REQUIRE_NEAR(static_cast<double>(decal.bounds.min.y), 0.5, 1e-6);
    REQUIRE_NEAR(static_cast<double>(decal.bounds.max.y), 0.5, 1e-6);
    for (const MeshVertex& vertex : decal.vertices) {
        REQUIRE_NEAR(static_cast<double>(vertex.position.y), 0.5, 1e-6);
        REQUIRE(vertex.normal == glm::vec3(0.0f, 1.0f, 0.0f));
    }
    for (std::size_t i = 0; i < decal.indices.size(); i += 3u) {
        const glm::vec3 geo =
            face_normal(decal.vertices[decal.indices[i]].position,
                        decal.vertices[decal.indices[i + 1u]].position,
                        decal.vertices[decal.indices[i + 2u]].position);
        REQUIRE(geo.y > 0.99f);
    }
    apricot_test::pass("flat decals expose only an upward paint face");
}

void billboard_art_has_one_forward_facing_plane() {
    const MeshData billboard = make_billboard_quad();
    REQUIRE(billboard.vertices.size() == 4u);
    REQUIRE(billboard.indices.size() == 6u);
    REQUIRE(billboard.bounds.valid());
    REQUIRE_NEAR(static_cast<double>(billboard.bounds.min.z), 0.5, 1e-6);
    REQUIRE_NEAR(static_cast<double>(billboard.bounds.max.z), 0.5, 1e-6);
    for (const MeshVertex& vertex : billboard.vertices) {
        REQUIRE_NEAR(static_cast<double>(vertex.position.z), 0.5, 1e-6);
        REQUIRE(vertex.normal == glm::vec3(0.0f, 0.0f, 1.0f));
    }
    for (std::size_t i = 0; i < billboard.indices.size(); i += 3u) {
        const glm::vec3 geo =
            face_normal(billboard.vertices[billboard.indices[i]].position,
                        billboard.vertices[billboard.indices[i + 1u]].position,
                        billboard.vertices[billboard.indices[i + 2u]].position);
        REQUIRE(geo.z > 0.99f);
    }
    apricot_test::pass("billboard art exposes one forward-facing image plane");
}

void rounded_props_have_curved_normals_and_closed_geometry() {
    const MeshData rounded =
        make_rounded_box(glm::vec3{0.5f}, 0.16f, 8);
    REQUIRE(rounded.vertices.size() == 486u);
    REQUIRE(rounded.indices.size() == 2304u);
    REQUIRE(rounded.bounds.valid());
    REQUIRE_NEAR(static_cast<double>(rounded.bounds.min.x), -0.5, 1e-5);
    REQUIRE_NEAR(static_cast<double>(rounded.bounds.max.y), 0.5, 1e-5);

    bool found_curved_normal = false;
    for (const MeshVertex& vertex : rounded.vertices) {
        const glm::vec3 a = glm::abs(vertex.normal);
        found_curved_normal |= a.x > 0.15f && a.y > 0.15f;
        REQUIRE_NEAR(static_cast<double>(glm::length(vertex.normal)), 1.0, 1e-5);
    }
    REQUIRE_MSG(found_curved_normal,
                "rounded box still has only six cube-face normals", "rounding");

    const MeshData cylinder = make_cylinder(0.5f, 0.5f, 18);
    REQUIRE(cylinder.indices.size() == 216u);
    REQUIRE(cylinder.bounds.valid());
    REQUIRE_NEAR(static_cast<double>(cylinder.bounds.min.y), -0.5, 1e-5);
    REQUIRE_NEAR(static_cast<double>(cylinder.bounds.max.x), 0.5, 1e-5);
    apricot_test::pass("rounded boxes and closed cylinders model world props");
}

void the_disc_faces_up() {
    const MeshData disc = make_disc(1.0f, 18);
    REQUIRE(disc.vertices.size() == 20u);
    REQUIRE(disc.indices.size() == 54u);
    for (std::size_t i = 0; i < disc.indices.size(); i += 3u) {
        const glm::vec3 a = disc.vertices[disc.indices[i]].position;
        const glm::vec3 b = disc.vertices[disc.indices[i + 1u]].position;
        const glm::vec3 c = disc.vertices[disc.indices[i + 2u]].position;
        REQUIRE(glm::cross(b - a, c - a).y > 0.0f);
    }
    apricot_test::pass("fluid-mark discs face upward instead of being culled");
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
    const glm::vec4 damage0{0.1f, 0.2f, 0.3f, 0.4f};
    const glm::vec4 damage1{0.5f, 0.6f, 4.0f, -2.0f};
    const glm::vec4 deform{0.25f, 0.125f, 0.0f, 0.0f};
    const InstanceData inst = make_instance(glm::mat4{1.0f}, tint, uv,
                                            damage0, damage1, deform);
    REQUIRE(inst.tint == tint);
    REQUIRE(inst.uv_scale == uv);
    REQUIRE(inst.body_damage0 == damage0);
    REQUIRE(inst.body_damage1 == damage1);
    REQUIRE(inst.deform_frame == deform);
    REQUIRE(inst.model == glm::mat4{1.0f});
    apricot_test::pass("colour, tiling, and body dents ride the instance");
}

void damaged_body_seams_stay_welded() {
    std::ifstream file(asset_path("shaders/lit_instanced.vert"));
    REQUIRE_MSG(file.good(), "could not read the live body shader", "damage");
    const std::string shader{std::istreambuf_iterator<char>{file},
                             std::istreambuf_iterator<char>{}};
    const std::size_t deformation_begin =
        shader.find("Deep localized damage loosens the panel");
    const std::size_t deformation_end =
        shader.find("Interpolated into the fragment stage");
    REQUIRE(deformation_begin != std::string::npos);
    REQUIRE(deformation_end != std::string::npos);
    REQUIRE(deformation_end > deformation_begin);
    const std::string deformation = shader.substr(
        deformation_begin, deformation_end - deformation_begin);
    REQUIRE_MSG(deformation.find("a_normal") == std::string::npos,
                "normal-based deformation tears duplicated panel seams",
                "damage");

    const std::size_t loose_begin = deformation.find("float front_loose");
    const std::size_t loose_end = deformation.find("v_loose_panel");
    REQUIRE(loose_begin != std::string::npos);
    REQUIRE(loose_end != std::string::npos);
    REQUIRE(loose_end > loose_begin);
    const std::string loose_geometry = deformation.substr(
        loose_begin, loose_end - loose_begin);
    REQUIRE_MSG(loose_geometry.find("local_pos") == std::string::npos,
                "loose-panel styling opened the single-layer body shell",
                "damage");
    apricot_test::pass("damaged panel seams stay welded on single-layer cars");
}

void vehicle_lights_keep_per_lamp_failure_and_emission() {
    std::ifstream lighting(asset_path("shaders/lighting.glsl"));
    std::ifstream fragment(asset_path("shaders/lit.frag"));
    REQUIRE(lighting.good());
    REQUIRE(fragment.good());
    const std::string light_source{std::istreambuf_iterator<char>{lighting},
                                   std::istreambuf_iterator<char>{}};
    const std::string fragment_source{std::istreambuf_iterator<char>{fragment},
                                      std::istreambuf_iterator<char>{}};
    REQUIRE(light_source.find("uniform vec2  u_headlight_intensity") !=
            std::string::npos);
    REQUIRE(light_source.find("u_headlight_intensity[i]") !=
            std::string::npos);
    REQUIRE(fragment_source.find("max(v_tint.a - 1.0, 0.0)") !=
            std::string::npos);
    std::ifstream vertex(asset_path("shaders/lit_instanced.vert"));
    REQUIRE(vertex.good());
    const std::string vertex_source{std::istreambuf_iterator<char>{vertex},
                                    std::istreambuf_iterator<char>{}};
    REQUIRE(vertex_source.find("gl_Position = u_view_proj * world;") != std::string::npos);
    REQUIRE_MSG(vertex_source.find("gl_Position.z") == std::string::npos,
                "surface glow must not be pulled off the body by a depth bias", "lamps");
    apricot_test::pass(
        "headlight beams fail independently and lamp meshes stay emissive");
}

void gas_canopy_owns_six_real_downlights() {
    std::ifstream lighting(asset_path("shaders/lighting.glsl"));
    REQUIRE(lighting.good());
    const std::string source{std::istreambuf_iterator<char>{lighting},
                             std::istreambuf_iterator<char>{}};
    REQUIRE(kCanopyLightCount == 6);
    REQUIRE(source.find("uniform vec3  u_canopy_light_pos[6]") !=
            std::string::npos);
    REQUIRE(source.find("for (int i = 0; i < 6; ++i)") != std::string::npos);
    REQUIRE(source.find("u_canopy_light_intensity") != std::string::npos);
    REQUIRE(source.find("u_canopy_light_outer_cos") != std::string::npos);
    apricot_test::pass("six canopy panels emit bounded downward spotlights");
}

void distance_haze_eases_world_rain_and_sky_together() {
    std::ifstream lighting(asset_path("shaders/lighting.glsl"));
    std::ifstream precip(asset_path("shaders/precip.frag"));
    std::ifstream sky(asset_path("shaders/sky.frag"));
    REQUIRE(lighting.good());
    REQUIRE(precip.good());
    REQUIRE(sky.good());
    const std::string lighting_source{
        std::istreambuf_iterator<char>{lighting},
        std::istreambuf_iterator<char>{}};
    const std::string precip_source{
        std::istreambuf_iterator<char>{precip},
        std::istreambuf_iterator<char>{}};
    const std::string sky_source{std::istreambuf_iterator<char>{sky},
                                 std::istreambuf_iterator<char>{}};
    constexpr const char* kEase = "f = f * f * (3.0 - 2.0 * f)";
    REQUIRE(lighting_source.find(kEase) != std::string::npos);
    REQUIRE(precip_source.find(kEase) != std::string::npos);
    REQUIRE(sky_source.find("horizon_haze") != std::string::npos);
    REQUIRE(sky_source.find("u_fog_color") != std::string::npos);
    apricot_test::pass("world, rain, and sky share one eased distance haze");
}

}  // namespace

int main() {
    every_box_triangle_faces_outward();
    gable_prism_is_closed_and_faces_outward();
    the_plane_faces_up();
    flat_decals_have_no_headlight_catching_sides();
    billboard_art_has_one_forward_facing_plane();
    rounded_props_have_curved_normals_and_closed_geometry();
    the_disc_faces_up();
    the_normal_matrix_survives_non_uniform_scale();
    a_uniform_scale_is_the_easy_case_and_must_still_be_right();
    the_instance_record_carries_what_the_batch_key_refuses_to();
    damaged_body_seams_stay_welded();
    vehicle_lights_keep_per_lamp_failure_and_emission();
    gas_canopy_owns_six_real_downlights();
    distance_haze_eases_world_rain_and_sky_together();
    return apricot_test::done("render_geometry_tests");
}
