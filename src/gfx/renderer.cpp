#include "gfx/renderer.h"

#include <algorithm>
#include <utility>

#include "core/log.h"
#include "gfx/gl_state.h"
#include "gfx/sky.h"

namespace apricot {
namespace {

// Texture unit the material diffuse lives on for the whole lit pass.
constexpr GLuint kDiffuseUnit = 0;

// Unpack the batch key scene/draw_batch.h builds. Kept next to its only
// consumer so a change to batch_key() breaks here loudly rather than producing
// plausible-looking wrong lookups.
MeshId key_mesh(uint64_t key) { return static_cast<MeshId>(key & 0xFFFFFFFFull); }
MaterialId key_material(uint64_t key) {
    return static_cast<MaterialId>(key >> 32);
}

}  // namespace

Renderer::~Renderer() { destroy(); }

bool Renderer::init() {
    if (!lit_.build_from_files("shaders/lit_instanced.vert", "shaders/lit.frag")) {
        AP_ERROR("renderer: the lit shader failed to build; no world will draw");
        return false;
    }

    Texture white;
    if (!white.make_white()) {
        AP_ERROR("renderer: could not create the fallback white texture");
        lit_.destroy();
        return false;
    }
    white_material_ = add_material(std::move(white));
    return true;
}

void Renderer::destroy() {
    lit_.destroy();
    meshes_.clear();      // each Mesh destructor pairs its own gl_state hooks
    free_mesh_slots_.clear();
    live_meshes_ = 0;
    mesh_bytes_ = 0;
    materials_.clear();   // ditto for each Texture
    gather_.clear();
    white_material_ = kInvalidId;
    stats_ = Stats{};
}

Mesh* Renderer::resolve_mesh(MeshId id) {
    if (id == kInvalidId) return nullptr;
    const uint16_t slot = mesh_slot_of(id);
    if (slot >= meshes_.size()) return nullptr;
    MeshSlot& s = meshes_[slot];
    // The generation check IS the safety. Without it a handle held across a
    // free draws whatever chunk took the slot over, at that chunk's world
    // coordinates, and the symptom is a patch of ground repeated somewhere it
    // has no business being.
    if (!s.live || s.generation != mesh_generation_of(id)) return nullptr;
    return &s.mesh;
}

MeshId Renderer::store_mesh(Mesh&& m, std::size_t bytes) {
    uint16_t slot;
    if (!free_mesh_slots_.empty()) {
        slot = free_mesh_slots_.back();
        free_mesh_slots_.pop_back();
        meshes_[slot].mesh = std::move(m);
    } else {
        // kNoSlot must never be a real slot, or a full table would mint a
        // handle equal to kInvalidId and every consumer's "is this valid" check
        // would start lying.
        if (meshes_.size() >= kNoSlot) {
            AP_ERROR("renderer: the mesh table is full (%u slots); refusing to "
                     "issue a handle that would collide with kInvalidId",
                     static_cast<unsigned>(kNoSlot));
            return kInvalidId;
        }
        slot = static_cast<uint16_t>(meshes_.size());
        meshes_.emplace_back();
        meshes_[slot].mesh = std::move(m);
    }

    MeshSlot& s = meshes_[slot];
    s.bytes = bytes;
    s.live = true;
    ++live_meshes_;
    mesh_bytes_ += bytes;
    return pack_mesh_id(slot, s.generation);
}

MeshId Renderer::add_mesh(const MeshData& data) {
    Mesh m;
    if (!m.upload(data)) {
        AP_ERROR("renderer: mesh upload failed; returning an invalid handle");
        return kInvalidId;
    }
    return store_mesh(std::move(m),
                      data.vertices.size() * sizeof(MeshVertex) +
                          data.indices.size() * sizeof(uint32_t));
}

MeshId Renderer::add_mesh(const ChunkMesh& data) {
    Mesh m;
    if (!m.upload(data)) {
        AP_ERROR("renderer: chunk mesh upload failed for (%d, %d) lod %d",
                 data.coord.x, data.coord.z, data.lod);
        return kInvalidId;
    }
    return store_mesh(std::move(m), data.gpu_bytes());
}

bool Renderer::remove_mesh(MeshId id) {
    if (id == kInvalidId) return false;
    const uint16_t slot = mesh_slot_of(id);
    if (slot >= meshes_.size()) return false;
    MeshSlot& s = meshes_[slot];
    if (!s.live || s.generation != mesh_generation_of(id)) return false;

    // Mesh::destroy() pairs every glDelete* with its gl_state::on_*_deleted()
    // hook already, and it is the reason this function is three lines rather
    // than a place the bind-cache invariant gets re-litigated. See
    // src/gfx/README.md.
    s.mesh.destroy();
    s.live = false;
    mesh_bytes_ -= s.bytes;
    s.bytes = 0;
    --live_meshes_;

    // Wrapping the generation is fine and reusing the slot immediately is
    // fine; what is not fine is reusing it at the SAME generation, which would
    // make a stale handle valid again after 65536 turnovers of one slot. At a
    // chunk every few frames that is hours away, and "hours away" is precisely
    // the bug that never gets found, so the slot is retired instead.
    if (s.generation == 0xFFFFu) {
        AP_WARN("renderer: mesh slot %u has turned over 65536 times; retiring "
                "it rather than letting a stale handle become valid again",
                static_cast<unsigned>(slot));
        return true;
    }
    ++s.generation;
    free_mesh_slots_.push_back(slot);
    return true;
}

MaterialId Renderer::add_material(Texture&& diffuse) {
    Material m;
    m.diffuse = std::move(diffuse);
    materials_.push_back(std::move(m));
    return static_cast<MaterialId>(materials_.size() - 1u);
}

void Renderer::begin_frame(const Camera& camera, const SkyEnv& env) {
    lit_.bind();
    lit_.set_mat4("u_view_proj", camera.view_projection());
    lit_.set_int("u_diffuse", static_cast<int>(kDiffuseUnit));
    // One call, one env, every lit shader. See gfx/sky.h.
    apply_lighting(lit_, env, camera.position);
}

bool Renderer::draw_run(const Scene& scene, const std::vector<NodeId>& visible,
                        std::size_t first, int count, uint64_t key) {
    if (count <= 0) return false;

    const MeshId mesh_id = key_mesh(key);
    const MaterialId mat_id = key_material(key);
    if (mat_id >= materials_.size()) {
        AP_ERROR("renderer: batch key names material %u but the table holds %zu",
                 mat_id, materials_.size());
        return false;
    }

    // A node still holding a handle to a freed mesh lands here. Drawing nothing
    // is the correct answer and saying so is the important half: silence would
    // present as a hole in the world with no clue attached, which is the exact
    // failure the generation tag exists to convert into a message.
    Mesh* found = resolve_mesh(mesh_id);
    if (!found) {
        AP_ERROR("renderer: batch key names mesh slot %u generation %u, which "
                 "is not live; a scene node outlived the mesh it draws",
                 static_cast<unsigned>(mesh_slot_of(mesh_id)),
                 static_cast<unsigned>(mesh_generation_of(mesh_id)));
        return false;
    }

    Mesh& mesh = *found;
    if (!mesh.valid()) return false;

    gather_.clear();
    gather_.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const SceneNode* n = scene.get(visible[first + static_cast<std::size_t>(i)]);
        if (!n) continue;
        gather_.push_back(make_instance(n->world, n->renderable.tint,
                                        n->renderable.uv_scale));
    }
    if (gather_.empty()) return false;

    materials_[mat_id].diffuse.bind(kDiffuseUnit);

    const GLsizei n = static_cast<GLsizei>(gather_.size());
    if (!mesh.upload_instances(gather_.data(), n)) return false;
    mesh.draw_instanced(n);

    ++stats_.draw_calls;
    stats_.instances += static_cast<int>(n);
    return true;
}

const Renderer::Stats& Renderer::render(const Scene& scene,
                                        const std::vector<NodeId>& visible,
                                        const Camera& camera, const SkyEnv& env,
                                        const Options& options) {
    stats_ = Stats{};
    stats_.visible_nodes = static_cast<int>(visible.size());
    if (!valid() || visible.empty()) return stats_;

    gl_state::reset_counters();
    begin_frame(camera, env);

    // Opaque geometry: depth on, back faces culled. Set here rather than once
    // at startup because the sky, the rain and the HUD each turn pieces of it
    // off, and a pass that assumes it inherited the right state is a pass that
    // breaks the day somebody reorders the frame.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);

    if (!options.instancing) {
        // The A/B path. Every visible node gets its own draw of exactly one
        // instance, through the same shader and the same buffers, so the only
        // thing that changes is the draw count.
        for (std::size_t i = 0; i < visible.size(); ++i) {
            const SceneNode* n = scene.get(visible[i]);
            if (!n || !batchable(n->renderable)) continue;
            draw_run(scene, visible, i, 1, batch_key(n->renderable));
            ++stats_.batches;
        }
        stats_.skipped_binds = gl_state::skipped_binds();
        return stats_;
    }

    const std::vector<DrawBatch> plan = plan_draw_batches(scene, visible);
    stats_.batches = static_cast<int>(plan.size());

    for (const DrawBatch& b : plan) {
        if (b.instanced) {
            if (draw_run(scene, visible, b.first, b.count, b.key)) {
                ++stats_.instanced_batches;
                stats_.largest_run = std::max(stats_.largest_run, b.count);
            }
            continue;
        }
        // A plain stretch is whatever did not make a long enough run. Walk it
        // one node at a time — below kMinInstancedRun the program switch and
        // the instance upload cost more than the draw calls they save.
        for (int i = 0; i < b.count; ++i) {
            const std::size_t idx = b.first + static_cast<std::size_t>(i);
            const SceneNode* n = scene.get(visible[idx]);
            if (!n || !batchable(n->renderable)) continue;
            draw_run(scene, visible, idx, 1, batch_key(n->renderable));
        }
    }

    stats_.skipped_binds = gl_state::skipped_binds();
    return stats_;
}

}  // namespace apricot
