#pragma once

#include <cstdint>
#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "gfx/camera.h"
#include "game/snow_clearance.h"
#include "gfx/instance.h"
#include "gfx/lighting.h"
#include "gfx/mesh.h"
#include "gfx/shader.h"
#include "gfx/sky_env.h"
#include "gfx/snow_shelter_grid.h"
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

    // Copies the exact simulation field; all retained strips reach the shader.
    void set_snow_clearance(const SnowClearanceField& field, float global_depth_m);
    // Static authored roof masks; upload once after the world's covers exist.
    // Capacity failure is explicit: no roofs are silently discarded.
    bool set_snow_shelter(const SnowShelterField& field);

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
    // that does not occur. Append-only means an entry, once added, is never
    // freed and never changes. Paintable materials (below) are the one
    // exception to the second half, and only the second.
    MeshId add_mesh(const MeshData& data);

    // Validated cooked static mesh, read by core/emesh_reader before it reaches
    // the resource table.
    MeshId add_mesh(const StaticEmesh& data);

    // Terrain's own vertex arrays, uploaded without a copy into MeshData
    // first. ChunkMesh and MeshData are the same three members over the same
    // vertex type; Mesh already overloads on both.
    MeshId add_mesh(const ChunkMesh& data);

    // Render-only depth offset for surfaces deliberately laid over another
    // surface, such as a road draped over terrain. This changes raster depth,
    // not the authored mesh or its collision height.
    struct DepthBias {
        float factor;
        float units;

        constexpr DepthBias(float slope_factor = 0.0f,
                            float constant_units = 0.0f)
            : factor(slope_factor), units(constant_units) {}

        constexpr bool enabled() const {
            return factor != 0.0f || units != 0.0f;
        }
    };

    // Enclosed interiors can draw their opaque materials first so their walls
    // reject hidden outdoor fragments before expensive shading. This changes
    // draw order only; glass, alpha and depth-equal overlays retain their passes.
    MaterialId add_material(Texture&& diffuse, bool alpha_blended = false,
                            float specular_scale = 1.0f,
                            DepthBias depth_bias = DepthBias(),
                            bool receives_snow = true, bool early_opaque = false);
    MaterialId add_glass_material();

    // PAINTABLE MATERIALS: THE ONE EXCEPTION TO APPEND-ONLY.
    //
    // Their texels may be rewritten after they are added. They are still never
    // freed. Every other material must stay fixed, because materials are
    // shared by handle: all traffic cars of one paint variant draw through a
    // single MaterialId (TrafficVisual picks it from the model's paint table by
    // hash), so rewriting the texture behind that id would repaint every car of
    // the variant across the city in the same frame, and it would be reported
    // as a traffic bug. update_paintable_material() therefore accepts ONLY ids
    // minted by add_paintable_material(), and refuses the rest with an error
    // and no change.
    //
    // The converse is the caller's half of the bargain: a paintable id must
    // never go into a table that traffic, or any other shared owner, draws
    // from. One car, one paintable id.
    //
    // It starts as `width` x `height` opaque white, so one not yet written
    // draws its tint rather than black, with add_material()'s defaults (which
    // are what vehicle body paint already uses). Because the table never frees,
    // a pool that wants N of these allocates N up front and holds their RGBA
    // storage and mips from then on. kInvalidId for a non-positive size.
    MaterialId add_paintable_material(int width, int height);

    // Replace the texels: RGBA8, rows bottom-up, as decode_rgba_file() gives.
    // Same GL object and same handle, so no scene node needs re-pointing; a
    // size change reallocates the storage behind that same object.
    bool update_paintable_material(MaterialId id, int width, int height,
                                   const std::vector<uint8_t>& rgba);

    // False for kInvalidId, out-of-range ids and every add_material() id.
    bool material_paintable(MaterialId id) const;

    // Draw after opaque characters, so glass also covers occupants correctly.
    void render_glass(const Scene& scene, const std::vector<NodeId>& visible,
                      const Camera& camera, const SkyEnv& env,
                      const HeadlightRig& headlights, const CanopyLightRig& canopy_lights);

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
                        const HeadlightRig& headlights,
                        const CanopyLightRig& canopy_lights,
                        const Options& options);

    const Stats& stats() const { return stats_; }

private:
    struct Material {
        Texture diffuse;
        bool alpha_blended = false;
        bool glass = false;
        bool receives_snow = true;
        bool early_opaque = false;
        // Set only by add_paintable_material(). See the comment there.
        bool paintable = false;
        float specular_scale = 1.0f;
        DepthBias depth_bias;
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

    void begin_frame(const Camera& camera, const SkyEnv& env,
                     const HeadlightRig& headlights,
                     const CanopyLightRig& canopy_lights);

    Shader lit_;
    GLuint snow_clearance_texture_ = 0;
    std::array<glm::vec4, SnowClearanceField::kMaxStrips * 2> snow_clearance_data_{};
    glm::vec4 snow_clearance_bounds_{0.f};
    int snow_clearance_count_ = 0;
    bool snow_clearance_dirty_ = false;
    SnowShelterGrid snow_shelter_grid_;
    std::array<GLuint,3> snow_shelter_textures_{};
    std::array<GLuint,3> snow_shelter_buffers_{};
    Texture vehicle_damage_atlas_;
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
