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
    //
    // MESHES ARE FREED NOW, AND THE HANDLE CARRIES A GENERATION (PENG-28).
    //
    // This table used to be append-only, and the reason it gave was a good one:
    // a recycled MeshId aliasing a live scene node draws one chunk's geometry
    // where another chunk's should be, which reads as "the world is subtly
    // wrong somewhere" and points at nothing. Streaming removed the option of
    // never freeing — a 2.5 km ring is thousands of chunk meshes and they turn
    // over as the player drives — so the aliasing had to be made
    // unrepresentable instead of merely unlikely.
    //
    // A MeshId is therefore SLOT | GENERATION, not an index:
    //
    //     bits  0-15   slot into meshes_
    //     bits 16-31   generation, bumped on every free
    //
    // A handle held across the free of its slot resolves to nullptr and draws
    // nothing, loudly, instead of drawing somebody else's geometry. That is a
    // bug you can find. The alternative is a bug you cannot.
    //
    // kInvalidId (0xFFFFFFFF) can never collide with a real handle because slot
    // 0xFFFF is never issued — see the cap in add_mesh().
    //
    // MATERIALS ARE STILL APPEND-ONLY, and that is a decision rather than an
    // oversight. Nothing streams materials: every terrain chunk in the world
    // shares one, and the road layers share six, all created at startup. A free
    // path for a table that never grows would be untested code guarding a case
    // that does not occur.
    MeshId add_mesh(const MeshData& data);

    // Terrain's own vertex arrays, uploaded without a copy into MeshData
    // first. ChunkMesh and MeshData are the same three members over the same
    // vertex type; Mesh already overloads on both.
    MeshId add_mesh(const ChunkMesh& data);

    MaterialId add_material(Texture&& diffuse);

    // Free a mesh and let its slot be reissued under a new generation.
    //
    // THE CALLER MUST HAVE REMOVED EVERY SCENE NODE REFERENCING IT FIRST.
    // Streamer::step() does exactly that — eviction ends in
    // Scene::remove_many() before the step returns, and the ids to free arrive
    // through Streamer::released_meshes() afterwards. Getting that order wrong
    // no longer corrupts anything; it just draws nothing and says so.
    //
    // Returns false for a handle that was already freed or never valid.
    bool remove_mesh(MeshId id);

    // Live meshes, and the vertex+index bytes they hold. Reported so the
    // streaming memory figure comes from the resource table itself rather than
    // from a running total somebody has to remember to decrement.
    std::size_t mesh_count() const { return live_meshes_; }
    std::size_t mesh_bytes() const { return mesh_bytes_; }

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

    // One slot of the mesh table. `generation` outlives the Mesh in it: that is
    // the whole mechanism, because a slot that is reused has to be able to tell
    // an old handle apart from a new one.
    struct MeshSlot {
        Mesh mesh;
        std::size_t bytes = 0;
        uint16_t generation = 0;
        bool live = false;
    };

    static constexpr uint16_t kNoSlot = 0xFFFFu;

    static constexpr MeshId pack_mesh_id(uint16_t slot, uint16_t generation) {
        return (static_cast<MeshId>(generation) << 16) |
               static_cast<MeshId>(slot);
    }
    static constexpr uint16_t mesh_slot_of(MeshId id) {
        return static_cast<uint16_t>(id & 0xFFFFu);
    }
    static constexpr uint16_t mesh_generation_of(MeshId id) {
        return static_cast<uint16_t>(id >> 16);
    }

    // nullptr for a stale, freed or malformed handle. The one place the
    // generation is checked, so there is exactly one place to get it wrong.
    Mesh* resolve_mesh(MeshId id);

    MeshId store_mesh(Mesh&& m, std::size_t bytes);

    // Draw one contiguous run as a single instanced call. Returns false if it
    // could not, in which case NOTHING was drawn (rather than something wrong).
    bool draw_run(const Scene& scene, const std::vector<NodeId>& visible,
                  std::size_t first, int count, uint64_t key);

    void begin_frame(const Camera& camera, const SkyEnv& env);

    Shader lit_;
    std::vector<MeshSlot> meshes_;
    std::vector<uint16_t> free_mesh_slots_;
    std::size_t live_meshes_ = 0;
    std::size_t mesh_bytes_ = 0;
    std::vector<Material> materials_;
    MaterialId white_material_ = kInvalidId;

    // Reused between batches so a frame that draws a thousand instances does
    // not allocate a thousand times.
    std::vector<InstanceData> gather_;

    Stats stats_;
};

}  // namespace apricot
