#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gfx/camera.h"
#include "gfx/instance.h"
#include "gfx/mesh.h"
#include "gfx/shader.h"
#include "gfx/sky_env.h"
#include "gfx/texture.h"
#include "scene/draw_batch.h"
#include "scene/scene.h"

namespace apricot {

// The forward lit pass.
//
// It owns the mesh and material tables the sim's opaque MeshId / MaterialId
// handles index into, and it EXECUTES the batch plan that scene/draw_batch.h
// produces — it does not make one. That split is the reason the batching can be
// tested headlessly against a real culled scene while this class stays a thing
// you have to have a window to run.
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init();
    void destroy();
    bool valid() const { return lit_.valid(); }

    // --- resource tables ----------------------------------------------------
    // Handles are indices, handed to the sim as opaque integers. They are
    // stable for the lifetime of the Renderer; nothing is ever removed, because
    // a recycled MeshId aliasing live scene nodes is a whole class of bug this
    // engine does not need yet.
    MeshId add_mesh(const MeshData& data);
    MaterialId add_material(Texture&& diffuse);

    // A material with no texture of its own: samples flat white, so a node
    // renders as its per-instance tint. Created by init(); use it rather than
    // leaving a material's texture unset, because an unbound sampler reads
    // black and a black object looks exactly like an unlit one.
    MaterialId white_material() const { return white_material_; }

    struct Options {
        // false routes every visible node through a one-instance draw instead
        // of the batch plan. Same shader, same geometry, same lighting — the
        // ONLY difference is how many draw calls it takes, which is what makes
        // the debug overlay's toggle a measurement rather than a demo.
        bool instancing = true;
    };

    struct Stats {
        int visible_nodes = 0;
        int batches = 0;            // entries in the plan
        int instanced_batches = 0;  // of those, ones that collapsed
        int draw_calls = 0;
        int instances = 0;          // total instances submitted
        int largest_run = 0;        // biggest collapsed run this frame
        unsigned int skipped_binds = 0;
    };

    // Draw `visible` (which MUST be the batch-key-sorted list Scene::cull
    // produced; feeding an unsorted list is not an error, it just finds no runs
    // and quietly does no batching).
    const Stats& render(const Scene& scene, const std::vector<NodeId>& visible,
                        const Camera& camera, const SkyEnv& env,
                        const Options& options);

    const Stats& stats() const { return stats_; }

private:
    struct Material {
        Texture diffuse;
    };

    // Draw one contiguous run as a single instanced call. Returns false if it
    // could not, in which case NOTHING was drawn (rather than something wrong).
    bool draw_run(const Scene& scene, const std::vector<NodeId>& visible,
                  std::size_t first, int count, uint64_t key);

    void begin_frame(const Camera& camera, const SkyEnv& env);

    Shader lit_;
    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;
    MaterialId white_material_ = kInvalidId;

    // Reused between batches so a frame that draws a thousand instances does
    // not allocate a thousand times.
    std::vector<InstanceData> gather_;

    Stats stats_;
};

}  // namespace apricot
