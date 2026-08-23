#include "app/demo_scene.h"

#include <cmath>
#include <utility>

#include "core/log.h"
#include "core/rng.h"
#include "gfx/primitives.h"
#include "gfx/texture.h"

namespace apricot {
namespace {

// Ground grid resolution. 128 cells over the whole field is coarse for terrain
// and completely adequate for a placeholder: it exists so lighting and fog have
// geometry to interpolate across, not to look like a landscape.
constexpr int kGroundCells = 128;

// Build the placeholder ground: a plane whose vertices are lifted onto the
// collider's height field, with normals taken from the same function. Deriving
// both from height_at() is what makes the surface the player sees the surface
// the car touches.
MeshData build_ground(const TerrainCollider& collider, float half) {
    MeshData m = make_plane(half, kGroundCells);
    m.bounds = AABB{};
    for (MeshVertex& v : m.vertices) {
        v.position.y = collider.height(v.position.x, v.position.z);
        v.normal = collider.normal(v.position.x, v.position.z);
        m.bounds.expand(v.position);
    }
    return m;
}

}  // namespace

bool build_demo_scene(Scene& scene, Renderer& renderer,
                      const TerrainCollider& collider, uint64_t seed,
                      int box_count, float field_radius, DemoScene& out) {
    out = DemoScene{};

    // --- geometry -----------------------------------------------------------
    const MeshData ground_data = build_ground(collider, field_radius);
    out.ground_mesh = renderer.add_mesh(ground_data);
    if (out.ground_mesh == kInvalidId) {
        AP_ERROR("demo scene: ground mesh upload failed");
        return false;
    }

    const MeshData box_data = make_box(glm::vec3{0.5f});
    out.box_mesh = renderer.add_mesh(box_data);
    if (out.box_mesh == kInvalidId) {
        AP_ERROR("demo scene: box mesh upload failed");
        return false;
    }

    // --- materials ----------------------------------------------------------
    // All procedural. The engine loads no image files, so a missing texture is
    // not a failure mode that exists here.
    Texture grass;
    if (!grass.make_noise(256, 8, 4, glm::vec3{0.20f, 0.31f, 0.14f},
                          glm::vec3{0.42f, 0.55f, 0.24f}, seed ^ 0x6C0FFEEull)) {
        AP_ERROR("demo scene: ground texture generation failed");
        return false;
    }
    out.ground_material = renderer.add_material(std::move(grass));

    Texture stone;
    if (!stone.make_noise(128, 8, 3, glm::vec3{0.34f, 0.33f, 0.31f},
                          glm::vec3{0.62f, 0.61f, 0.58f}, seed ^ 0x57012Eull)) {
        AP_ERROR("demo scene: stone texture generation failed");
        return false;
    }
    out.box_materials.push_back(renderer.add_material(std::move(stone)));

    Texture checker;
    if (!checker.make_checker(128, 8, glm::vec3{0.86f, 0.84f, 0.78f},
                              glm::vec3{0.30f, 0.28f, 0.26f})) {
        AP_ERROR("demo scene: checker texture generation failed");
        return false;
    }
    out.box_materials.push_back(renderer.add_material(std::move(checker)));

    Texture panel;
    if (!panel.make_gradient(128, glm::vec3{0.30f, 0.22f, 0.18f},
                             glm::vec3{0.72f, 0.60f, 0.46f})) {
        AP_ERROR("demo scene: gradient texture generation failed");
        return false;
    }
    out.box_materials.push_back(renderer.add_material(std::move(panel)));

    Texture car_tex;
    if (!car_tex.make_checker(64, 2, glm::vec3{0.85f, 0.16f, 0.12f},
                              glm::vec3{0.95f, 0.90f, 0.85f})) {
        AP_ERROR("demo scene: car texture generation failed");
        return false;
    }
    out.car_material = renderer.add_material(std::move(car_tex));

    // --- nodes --------------------------------------------------------------
    {
        Renderable r;
        r.mesh = out.ground_mesh;
        r.material = out.ground_material;
        r.tint = glm::vec4{1.0f};
        // One texture tile every ~8 m. Left at 1.0 the noise stretches across
        // the whole field and reads as a flat colour.
        const float tiles = field_radius * 2.0f / 8.0f;
        r.uv_scale = glm::vec2{tiles, tiles};
        out.ground_node = scene.create(r, Transform{}, ground_data.bounds);
    }

    out.box_nodes.reserve(static_cast<std::size_t>(box_count > 0 ? box_count : 0));
    for (int i = 0; i < box_count; ++i) {
        // Keyed by index through hash_coord, never a sequential stream: the
        // field must come out identical however it was built.
        Rng r = rng_at(seed, i, 0, 0xB0);

        // Uniform over the disc needs sqrt on the radius; without it every box
        // crowds the middle and the field looks like a pile.
        const float angle = r.range(0.0f, 6.28318530718f);
        const float radius = field_radius * 0.94f * std::sqrt(r.next_float());
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;

        Transform t;
        // Non-uniform on purpose. This is exactly the case a mat3(model) normal
        // matrix gets wrong, so the demo exercises the inverse-transpose rather
        // than flattering it.
        t.scale = glm::vec3{r.range(1.2f, 4.5f), r.range(1.5f, 9.0f),
                            r.range(1.2f, 4.5f)};
        t.position = glm::vec3{x, collider.height(x, z) + t.scale.y * 0.5f, z};
        t.set_euler_deg(0.0f, r.range(0.0f, 360.0f), 0.0f);

        Renderable rend;
        rend.mesh = out.box_mesh;
        rend.material = out.box_materials[static_cast<std::size_t>(i) %
                                          out.box_materials.size()];
        // tint and uv_scale differ per box and are DELIBERATELY not in the
        // batch key, so all of these still collapse into one draw per material.
        rend.tint = glm::vec4{r.range(0.55f, 1.0f), r.range(0.55f, 1.0f),
                              r.range(0.55f, 1.0f), 1.0f};
        rend.uv_scale = glm::vec2{r.range(1.0f, 3.0f), r.range(1.0f, 3.0f)};

        out.box_nodes.push_back(scene.create(rend, t, box_data.bounds));
    }

    {
        Renderable r;
        r.mesh = out.box_mesh;
        r.material = out.car_material;
        r.tint = glm::vec4{1.0f};
        r.uv_scale = glm::vec2{1.0f};
        Transform t;
        t.scale = out.car_half * 2.0f;
        out.car_node = scene.create(r, t, box_data.bounds);
    }

    scene.update();

    AP_INFO("demo scene: %zu nodes (%d boxes over %.0f m, %zu materials)",
            scene.size(), box_count, static_cast<double>(field_radius),
            out.box_materials.size() + 2u);
    return true;
}

}  // namespace apricot
