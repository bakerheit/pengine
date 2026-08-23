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
    materials_.clear();   // ditto for each Texture
    gather_.clear();
    white_material_ = kInvalidId;
    stats_ = Stats{};
}

MeshId Renderer::add_mesh(const MeshData& data) {
    Mesh m;
    if (!m.upload(data)) {
        AP_ERROR("renderer: mesh upload failed; returning an invalid handle");
        return kInvalidId;
    }
    meshes_.push_back(std::move(m));
    return static_cast<MeshId>(meshes_.size() - 1u);
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
    if (mesh_id >= meshes_.size() || mat_id >= materials_.size()) {
        AP_ERROR("renderer: batch key names mesh %u / material %u but the "
                 "tables hold %zu / %zu",
                 mesh_id, mat_id, meshes_.size(), materials_.size());
        return false;
    }

    Mesh& mesh = meshes_[mesh_id];
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
