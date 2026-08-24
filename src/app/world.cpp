#include "app/world.h"

#include <utility>

#include "core/log.h"
#include "gfx/primitives.h"
#include "gfx/texture.h"

namespace apricot {
namespace {

// Append `src` into `dst`, shifting its indices. Local rather than promoted
// into gfx/primitives.h because composing boxes into a prop is a modelling
// convenience for THIS file, and primitives.h earns its place by being the
// thing headless suites can exercise.
void append(MeshData& dst, const MeshData& src, glm::vec3 offset,
            glm::vec3 scale) {
    const uint32_t base = static_cast<uint32_t>(dst.vertices.size());
    dst.vertices.reserve(dst.vertices.size() + src.vertices.size());
    for (const MeshVertex& v : src.vertices) {
        MeshVertex out = v;
        out.position = v.position * scale + offset;
        // Non-uniform scale skews a normal; renormalising the componentwise
        // divide is the cheap correct form. Skipping it makes a squat rock
        // light like a tall one, which reads as "the lighting is a bit off".
        out.normal = glm::normalize(v.normal / scale);
        dst.vertices.push_back(out);
        dst.bounds.expand(out.position);
    }
    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (const uint32_t i : src.indices) dst.indices.push_back(base + i);
}

// A tree, from a trunk and two canopy blocks.
//
// THE MODEL IS A STAND-IN AND THE PLACEMENT IS NOT. Where trees go, how dense
// they are, what they stand on and how they cull is terrain/scatter.cpp and is
// real. What a tree LOOKS like is three boxes, because the engine has no model
// loader and no asset on disk, and inventing one is not this ticket. Sized from
// prop_dims(PropKind::Tree) so the drawn thing matches the bounds it is culled
// by; a model larger than its bounds pops out at the edge of the screen.
MeshData make_tree(uint8_t variant) {
    const PropDims& d = prop_dims(PropKind::Tree);
    const float lean = 0.85f + 0.10f * static_cast<float>(variant);
    const float bushy = 0.80f + 0.14f * static_cast<float>(variant % 3u);

    MeshData m;
    const MeshData unit = make_box(glm::vec3{0.5f});

    const float trunk_h = d.height * 0.42f * lean;
    append(m, unit, glm::vec3{0.0f, trunk_h * 0.5f, 0.0f},
           glm::vec3{d.radius * 0.30f, trunk_h, d.radius * 0.30f});

    const float lower_h = d.height * 0.34f;
    append(m, unit, glm::vec3{0.0f, trunk_h + lower_h * 0.5f, 0.0f},
           glm::vec3{d.radius * 1.85f * bushy, lower_h,
                     d.radius * 1.85f * bushy});

    const float upper_h = d.height * 0.30f * lean;
    append(m, unit,
           glm::vec3{0.0f, trunk_h + lower_h + upper_h * 0.45f, 0.0f},
           glm::vec3{d.radius * 1.15f * bushy, upper_h,
                     d.radius * 1.15f * bushy});
    return m;
}

MeshData make_rock(uint8_t variant) {
    const PropDims& d = prop_dims(PropKind::Rock);
    const float squat[3] = {1.0f, 0.72f, 1.35f};
    const float wide[3] = {1.0f, 1.30f, 0.80f};
    const std::size_t i = variant % 3u;

    MeshData m;
    append(m, make_box(glm::vec3{0.5f}),
           glm::vec3{0.0f, d.height * squat[i] * 0.5f, 0.0f},
           glm::vec3{d.radius * 2.0f * wide[i], d.height * squat[i],
                     d.radius * 2.0f * wide[i]});
    return m;
}

}  // namespace

bool World::init(Renderer& renderer, uint64_t seed, const StreamerConfig& cfg) {
    streamer_ = Streamer(seed, cfg);


    // --- terrain material ----------------------------------------------------
    // ONE material for every chunk at every level. That is what lets the batch
    // key vary only by mesh, so the whole visible ring collapses into one draw
    // per chunk mesh rather than one program switch per chunk.
    //
    // It is a single tiled diffuse and not a splat. TerrainVertex carries
    // four-way material_weights and the lit shader does not read them; wiring
    // that up needs a vertex attribute and a fragment change together and is
    // its own ticket. Said out loud here rather than left looking finished.
    Texture ground;
    if (!ground.make_noise(256, 8, 4, glm::vec3{0.19f, 0.28f, 0.13f},
                           glm::vec3{0.45f, 0.52f, 0.28f},
                           seed ^ 0x6C0FFEEull)) {
        AP_ERROR("world: terrain texture generation failed");
        return false;
    }
    proto_.terrain.material = renderer.add_material(std::move(ground));
    proto_.terrain.mesh = kInvalidId;  // per chunk, filled by the streamer
    proto_.terrain.tint = glm::vec4{1.0f};
    // Chunk UVs are world-space in chunk units, so one repeat per 8 m keeps the
    // tiling identical at every level: the coarse rings are the same ground,
    // sampled less often, not a different-looking ground.
    proto_.terrain.uv_scale = glm::vec2{kChunkMetres / 8.0f};

    Texture bark;
    if (!bark.make_noise(128, 6, 3, glm::vec3{0.16f, 0.22f, 0.11f},
                         glm::vec3{0.34f, 0.44f, 0.20f}, seed ^ 0x77EEull)) {
        AP_ERROR("world: foliage texture generation failed");
        return false;
    }
    const MaterialId tree_mat = renderer.add_material(std::move(bark));

    Texture stone;
    if (!stone.make_noise(128, 8, 3, glm::vec3{0.32f, 0.31f, 0.30f},
                          glm::vec3{0.63f, 0.62f, 0.59f}, seed ^ 0x57012Eull)) {
        AP_ERROR("world: stone texture generation failed");
        return false;
    }
    const MaterialId rock_mat = renderer.add_material(std::move(stone));

    // --- prop models ---------------------------------------------------------
    // One mesh per variant, shared by every instance of it in the world. That
    // sharing is the entire reason a hillside of trees is a handful of draws:
    // the batch key is (material, mesh), so ten thousand trees of four variants
    // is four draws.
    for (uint8_t v = 0; v < kTreeVariants; ++v) {
        proto_.tree[v].mesh = renderer.add_mesh(make_tree(v));
        proto_.tree[v].material = tree_mat;
        if (proto_.tree[v].mesh == kInvalidId) {
            AP_ERROR("world: tree variant %u failed to upload", v);
            return false;
        }
    }
    for (uint8_t v = 0; v < kRockVariants; ++v) {
        proto_.rock[v].mesh = renderer.add_mesh(make_rock(v));
        proto_.rock[v].material = rock_mat;
        if (proto_.rock[v].mesh == kInvalidId) {
            AP_ERROR("world: rock variant %u failed to upload", v);
            return false;
        }
    }

    AP_INFO("world: seed 0x%016llX, rings %d/%d/%d/%d chunks "
            "(%.0f/%.0f/%.0f/%.0f m), scatter to level %d",
            static_cast<unsigned long long>(seed), cfg.lod_ring[0],
            cfg.lod_ring[1], cfg.lod_ring[2], cfg.load_radius,
            static_cast<double>(cfg.lod_ring[0]) * kChunkMetres,
            static_cast<double>(cfg.lod_ring[1]) * kChunkMetres,
            static_cast<double>(cfg.lod_ring[2]) * kChunkMetres,
            static_cast<double>(cfg.load_radius) * kChunkMetres,
            cfg.max_scatter_lod);
    return true;
}

void World::update(Scene& scene, Renderer& renderer, glm::vec3 focus,
                   StepMode mode) {
    const StreamerStats st = streamer_.step(scene, proto_, focus, mode);

    stats_.chunks_refitted = st.chunks_refitted;
    stats_.chunks_evicted = st.chunks_evicted;
    stats_.instances_activated = st.instances_activated;
    stats_.budget_exhausted = st.budget_exhausted;
    stats_.chunks_built = 0;
    stats_.quads_built = 0;
    stats_.meshes_freed = 0;

    // --- free first, then upload --------------------------------------------
    // In that order on purpose. Freeing first lets the mesh table hand the same
    // slot straight back to this frame's uploads, so a player driving in a
    // straight line holds a flat number of slots instead of a growing one. The
    // generation tag is what makes reuse this eager safe: a stale handle
    // resolves to nullptr rather than to whatever moved in.
    //
    // Every id here is one the streamer has already removed the scene nodes
    // for -- Streamer::step() ends its eviction with Scene::remove_many()
    // before it returns.
    released_scratch_.clear();
    streamer_.take_released_meshes(released_scratch_);
    for (const MeshId id : released_scratch_) {
        if (renderer.remove_mesh(id)) ++stats_.meshes_freed;
    }

    for (const ChunkRequest& r : streamer_.pending_loads()) {
        const ChunkMesh mesh = build_chunk(streamer_.seed(), r.coord, r.lod);
        const MeshId id = renderer.add_mesh(mesh);
        if (id == kInvalidId) {
            // The upload failed. Do NOT deliver: an invalid handle activated
            // into a scene node is a chunk that draws nothing forever, and the
            // streamer would consider that coordinate resident and never ask
            // again. Dropping the delivery leaves it neither resident nor in
            // flight, so the next plan simply asks for it again.
            AP_ERROR("world: chunk (%d, %d) lod %d failed to upload; it will be "
                     "requested again next step",
                     r.coord.x, r.coord.z, r.lod);
            continue;
        }
        streamer_.deliver(r.coord, r.lod, id, mesh.bounds);
        ++stats_.chunks_built;
        stats_.quads_built += lod_quads(r.lod) * lod_quads(r.lod);
    }

    // deliver() can drop a stale delivery and hand its mesh straight back, so
    // drain once more rather than leaving it until next frame.
    released_scratch_.clear();
    streamer_.take_released_meshes(released_scratch_);
    for (const MeshId id : released_scratch_) {
        if (renderer.remove_mesh(id)) ++stats_.meshes_freed;
    }

    stats_.resident_chunks = streamer_.resident_count();
    streamer_.residency_by_lod(stats_.resident_by_lod);
    stats_.live_meshes = renderer.mesh_count();
    stats_.mesh_bytes = renderer.mesh_bytes();
}

int World::fill(Scene& scene, Renderer& renderer, glm::vec3 focus) {
    // A hard cap, and it is an assertion rather than a convenience. Fill mode
    // plans only prime_radius with no budget, so this converges in a handful of
    // steps; if it does not, something is wrong with the plan and spinning here
    // forever would present as the app hanging on startup with no message.
    constexpr int kMaxFillSteps = 256;

    int steps = 0;
    while (!streamer_.ready(focus) && steps < kMaxFillSteps) {
        update(scene, renderer, focus, StepMode::Fill);
        scene.update();
        ++steps;
    }
    if (!streamer_.ready(focus)) {
        AP_ERROR("world: fill did not converge in %d steps; resuming into a "
                 "world that is not ready",
                 kMaxFillSteps);
    }
    // ready() is re-asked by the caller rather than cached here: a cached flag
    // goes stale the moment the focus moves, and a stale "the world is ready"
    // is exactly the claim that lets a teleport resume into a hole.
    return steps;
}

void World::shutdown(Scene& scene, Renderer& renderer) {
    // Scene::clear() drops every node, which is every reference to every chunk
    // mesh. The meshes still RESIDENT are freed by Renderer::destroy(), which
    // runs next and destroys the whole table; what is drained here is only what
    // the streamer had already handed back and this frame had not collected, so
    // the two do not end the session disagreeing about who owns what.
    scene.clear();
    released_scratch_.clear();
    streamer_.take_released_meshes(released_scratch_);
    for (const MeshId id : released_scratch_) renderer.remove_mesh(id);
    released_scratch_.clear();
}

}  // namespace apricot
