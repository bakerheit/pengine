#include "gfx/renderer.h"
#include "gfx/snow_clearance_visual.h"

#include <algorithm>
#include <utility>

#include "core/log.h"
#include "core/asset_root.h"
#include "gfx/gl_state.h"
#include "gfx/sky.h"

namespace apricot {
namespace {

// Texture unit the material diffuse lives on for the whole lit pass.
constexpr GLuint kDiffuseUnit = 0;
constexpr GLuint kVehicleDamageUnit = 1;
constexpr GLuint kSnowClearanceUnit = 2;
constexpr std::array<GLuint,3> kSnowShelterUnits{3,7,8};

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
    if (!vehicle_damage_atlas_.load_file(
            asset_path("textures/effects/vehicle-damage-atlas.png"))) {
        AP_ERROR("renderer: vehicle damage atlas failed to load");
        destroy();
        return false;
    }
    glGenTextures(1, &snow_clearance_texture_);
    gl_state::bind_texture(kSnowClearanceUnit, snow_clearance_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F,
                 static_cast<GLsizei>(SnowClearanceField::kMaxStrips * 2), 1,
                 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

void Renderer::destroy() {
    lit_.destroy();
    if (snow_clearance_texture_) {
        glDeleteTextures(1, &snow_clearance_texture_);
        gl_state::on_texture_deleted(snow_clearance_texture_);
        snow_clearance_texture_ = 0;
    }
    snow_clearance_count_ = 0;
    snow_clearance_dirty_ = false;
    for (auto& texture:snow_shelter_textures_) if(texture) {
        glDeleteTextures(1,&texture);
        gl_state::on_texture_deleted(texture);
        texture=0;
    }
    for (auto& buffer:snow_shelter_buffers_) if(buffer) {
        glDeleteBuffers(1,&buffer);
        gl_state::on_buffer_deleted(buffer);
        buffer=0;
    }
    snow_shelter_grid_={};
    vehicle_damage_atlas_.destroy();
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

MeshId Renderer::add_mesh(const StaticEmesh& data) {
    Mesh m;
    if (!m.upload(data)) {
        AP_ERROR("renderer: cooked mesh upload failed; returning an invalid handle");
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

MaterialId Renderer::add_material(Texture&& diffuse, bool alpha_blended,
                                  float specular_scale, DepthBias depth_bias,
                                  bool receives_snow, bool early_opaque) {
    Material m;
    m.diffuse = std::move(diffuse);
    m.alpha_blended = alpha_blended;
    m.receives_snow = receives_snow;
    m.early_opaque = early_opaque && !alpha_blended;
    m.specular_scale = std::clamp(specular_scale, 0.0f, 1.0f);
    m.depth_bias = depth_bias;
    materials_.push_back(std::move(m));
    return static_cast<MaterialId>(materials_.size() - 1u);
}

MaterialId Renderer::add_glass_material() {
    Texture white;
    if (!white.make_white()) return kInvalidId;
    const auto id=add_material(std::move(white),true);
    materials_[id].glass=true;
    return id;
}

void Renderer::set_snow_clearance(const SnowClearanceField& field,
                                   float global_depth_m) {
    snow_clearance_count_ = 0;
    AABB bounds;
    for (const auto& strip : field.strips()) {
        if (snow_clearance_count_ >= static_cast<int>(SnowClearanceField::kMaxStrips)) break;
        const std::size_t index=static_cast<std::size_t>(snow_clearance_count_)*2;
        snow_clearance_data_[index]={strip.a,strip.half_width};
        snow_clearance_data_[index+1]={strip.b,
            snow_clearance_visual_cover(strip.residual_depth, global_depth_m)};
        const glm::vec3 radius{strip.half_width,0.f,strip.half_width};
        bounds.expand(strip.a-radius); bounds.expand(strip.a+radius);
        bounds.expand(strip.b-radius); bounds.expand(strip.b+radius);
        ++snow_clearance_count_;
    }
    if (bounds.valid())
        snow_clearance_bounds_={bounds.min.x,bounds.min.z,bounds.max.x,bounds.max.z};
    snow_clearance_dirty_ = true;
}

bool Renderer::set_snow_shelter(const SnowShelterField& field) {
    GLint max_texels=0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&max_texels);
    SnowShelterGrid grid;
    if(max_texels<=0 || !grid.build(field,static_cast<std::size_t>(max_texels))) {
        AP_ERROR("snow shelter: complete roof field exceeds texture capacity; refusing partial shelter");
        return false;
    }
    const auto upload=[&](std::size_t slot,GLenum format,std::size_t bytes,const void* data) {
        if(!snow_shelter_textures_[slot]) glGenTextures(1,&snow_shelter_textures_[slot]);
        if(!snow_shelter_buffers_[slot]) glGenBuffers(1,&snow_shelter_buffers_[slot]);
        gl_state::bind_texture(kSnowShelterUnits[slot],snow_shelter_textures_[slot],GL_TEXTURE_BUFFER);
        gl_state::bind_buffer(GL_TEXTURE_BUFFER,snow_shelter_buffers_[slot]);
        const glm::vec4 zero{0.f};
        if(bytes==0) {bytes=sizeof(zero);data=&zero;}
        glBufferData(GL_TEXTURE_BUFFER,static_cast<GLsizeiptr>(bytes),data,GL_STATIC_DRAW);
        glTexBuffer(GL_TEXTURE_BUFFER,format,snow_shelter_buffers_[slot]);
    };
    upload(0,GL_RGBA32F,grid.roofs.size()*sizeof(glm::vec4),grid.roofs.data());
    upload(1,GL_RG32UI,grid.cells.size()*sizeof(glm::uvec2),grid.cells.data());
    upload(2,GL_R32UI,grid.indices.size()*sizeof(uint32_t),grid.indices.data());
    AP_INFO("snow shelter: %zu roofs indexed in %dx%d cells (%.0fm); %zu references",
        grid.roofs.size()/4u,grid.columns,grid.rows,grid.cell_size,grid.indices.size());
    snow_shelter_grid_=std::move(grid);
    return true;
}

void Renderer::begin_frame(const Camera& camera, const SkyEnv& env,
                           const HeadlightRig& headlights,
                           const CanopyLightRig& canopy_lights) {
    lit_.bind();
    lit_.set_mat4("u_view_proj", camera.view_projection());
    lit_.set_int("u_diffuse", static_cast<int>(kDiffuseUnit));
    lit_.set_int("u_vehicle_damage", static_cast<int>(kVehicleDamageUnit));
    vehicle_damage_atlas_.bind(kVehicleDamageUnit);
    gl_state::bind_texture(kSnowClearanceUnit, snow_clearance_texture_);
    if (snow_clearance_dirty_ && snow_clearance_count_ > 0) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, snow_clearance_count_*2, 1,
                        GL_RGBA, GL_FLOAT, snow_clearance_data_.data());
    }
    snow_clearance_dirty_=false;
    lit_.set_int("u_snow_clearance",static_cast<int>(kSnowClearanceUnit));
    lit_.set_int("u_snow_clearance_count",snow_clearance_count_);
    lit_.set_vec4("u_snow_clearance_bounds",snow_clearance_bounds_);
    lit_.set_float("u_snow_clearance_height_tolerance",SnowClearanceField::kHeightTolerance);
    for(std::size_t slot=0;slot<snow_shelter_textures_.size();++slot)
        gl_state::bind_texture(kSnowShelterUnits[slot],snow_shelter_textures_[slot],GL_TEXTURE_BUFFER);
    lit_.set_int("u_snow_shelter_roofs",static_cast<int>(kSnowShelterUnits[0]));
    lit_.set_int("u_snow_shelter_cells",static_cast<int>(kSnowShelterUnits[1]));
    lit_.set_int("u_snow_shelter_indices",static_cast<int>(kSnowShelterUnits[2]));
    lit_.set_int("u_snow_shelter_columns",snow_shelter_grid_.columns);
    lit_.set_int("u_snow_shelter_rows",snow_shelter_grid_.rows);
    lit_.set_vec2("u_snow_shelter_origin",snow_shelter_grid_.origin);
    lit_.set_float("u_snow_shelter_cell_size",snow_shelter_grid_.cell_size);
    // One call, one env, every lit shader. See gfx/sky.h.
    apply_lighting(lit_, env, camera.position, headlights, canopy_lights);
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
                                        n->renderable.uv_scale,
                                        n->renderable.body_damage0,
                                        n->renderable.body_damage1,
                                        n->renderable.deform_frame));
    }
    if (gather_.empty()) return false;

    const Material& material = materials_[mat_id];
    material.diffuse.bind(kDiffuseUnit);
    lit_.set_int("u_glass", material.glass ? 1 : 0);
    lit_.set_int("u_receives_snow", material.receives_snow ? 1 : 0);
    lit_.set_float("u_material_specular_scale", material.specular_scale);

    const GLsizei n = static_cast<GLsizei>(gather_.size());
    if (!mesh.upload_instances(gather_.data(), n)) return false;

    // Distant, grazing-angle roads can share a depth-buffer value with the
    // terrain even though their authored vertices are physically separated.
    // Polygon offset resolves that raster ambiguity without lifting collision
    // or making vehicles ride above the visible asphalt.
    if (material.depth_bias.enabled()) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(material.depth_bias.factor, material.depth_bias.units);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    mesh.draw_instanced(n);

    ++stats_.draw_calls;
    stats_.instances += static_cast<int>(n);
    return true;
}

const Renderer::Stats& Renderer::render(const Scene& scene,
                                        const std::vector<NodeId>& visible,
                                        const Camera& camera, const SkyEnv& env,
                                        const HeadlightRig& headlights,
                                        const CanopyLightRig& canopy_lights,
                                        const Options& options) {
    stats_ = Stats{};
    stats_.visible_nodes = static_cast<int>(visible.size());
    if (!valid() || visible.empty()) return stats_;

    gl_state::reset_counters();
    begin_frame(camera, env, headlights, canopy_lights);

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
    glDisable(GL_POLYGON_OFFSET_FILL);

    const auto node_is_alpha_blended = [&](const SceneNode* node) {
        return node && node->renderable.material < materials_.size() &&
               materials_[node->renderable.material].alpha_blended;
    };
    const auto node_is_glass = [&](const SceneNode* node) {
        return node && node->renderable.material<materials_.size() &&
            materials_[node->renderable.material].glass;
    };

    enum class OpaquePhase { All, Early, Regular };
    const auto matches_phase = [&](const SceneNode* node, OpaquePhase phase) {
        if (phase == OpaquePhase::All) return true;
        const bool early = node && node->renderable.material < materials_.size() &&
            materials_[node->renderable.material].early_opaque;
        return early == (phase == OpaquePhase::Early);
    };

    const auto begin_alpha_pass = [] {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    };
    const auto end_alpha_pass = [] {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    };

    const SurfaceDrawPasses passes = partition_surface_draws(scene, visible);
    const auto begin_surface_pass = [] {
        glDepthFunc(GL_EQUAL);
        glDepthMask(GL_FALSE);
    };
    const auto end_surface_pass = [] {
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
    };

    if (!options.instancing) {
        // The A/B path. Every visible node gets its own draw of exactly one
        // instance, through the same shader and the same buffers, so the only
        // thing that changes is the draw count.
        const auto draw_unbatched_pass = [&](const std::vector<NodeId>& nodes,
                                             bool alpha_blended, OpaquePhase phase = OpaquePhase::All) {
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                const SceneNode* n = scene.get(nodes[i]);
                if (!n || node_is_glass(n) || !batchable(n->renderable) ||
                    node_is_alpha_blended(n) != alpha_blended ||
                    !matches_phase(n, phase)) {
                    continue;
                }
                draw_run(scene, nodes, i, 1, batch_key(n->renderable));
                ++stats_.batches;
            }
        };
        draw_unbatched_pass(passes.geometry, false, OpaquePhase::Early);
        draw_unbatched_pass(passes.geometry, false, OpaquePhase::Regular);
        begin_surface_pass();
        draw_unbatched_pass(passes.overlays, false);
        end_surface_pass();
        begin_alpha_pass();
        draw_unbatched_pass(passes.geometry, true);
        end_alpha_pass();
        glDisable(GL_POLYGON_OFFSET_FILL);
        stats_.skipped_binds = gl_state::skipped_binds();
        return stats_;
    }

    const std::vector<DrawBatch> plan = plan_draw_batches(scene, passes.geometry);
    const std::vector<DrawBatch> surface_plan = plan_draw_batches(scene, passes.overlays);
    stats_.batches = static_cast<int>(plan.size() + surface_plan.size());

    const auto draw_batched_pass = [&](const std::vector<NodeId>& nodes,
                                      const std::vector<DrawBatch>& batches,
                                      bool alpha_blended, OpaquePhase phase = OpaquePhase::All) {
        for (const DrawBatch& b : batches) {
            if (b.instanced) {
                const SceneNode* first = scene.get(nodes[b.first]);
                if (node_is_glass(first) || node_is_alpha_blended(first) != alpha_blended ||
                    !matches_phase(first, phase)) continue;
                if (draw_run(scene, nodes, b.first, b.count, b.key)) {
                    ++stats_.instanced_batches;
                    stats_.largest_run = std::max(stats_.largest_run, b.count);
                }
                continue;
            }
            // A plain stretch is whatever did not make a long enough run.
            for (int i = 0; i < b.count; ++i) {
                const std::size_t idx = b.first + static_cast<std::size_t>(i);
                const SceneNode* n = scene.get(nodes[idx]);
                if (!n || node_is_glass(n) || !batchable(n->renderable) ||
                    node_is_alpha_blended(n) != alpha_blended ||
                    !matches_phase(n, phase)) {
                    continue;
                }
                draw_run(scene, nodes, idx, 1, batch_key(n->renderable));
            }
        }
    };

    // Same batches and draw count, with enclosed opaque interiors ahead of
    // the outdoor world. Their depth then rejects hidden city shading.
    draw_batched_pass(passes.geometry, plan, false, OpaquePhase::Early);
    draw_batched_pass(passes.geometry, plan, false, OpaquePhase::Regular);
    begin_surface_pass();
    draw_batched_pass(passes.overlays, surface_plan, false);
    end_surface_pass();

    // Transparent world decals still test against the opaque world, but never
    // write depth. Fluid marks share a mesh and material, so they retain one
    // instanced draw while texture alpha supplies their broken wet edges.
    begin_alpha_pass();
    draw_batched_pass(passes.geometry, plan, true);
    end_alpha_pass();

    glDisable(GL_POLYGON_OFFSET_FILL);
    stats_.skipped_binds = gl_state::skipped_binds();
    return stats_;
}

void Renderer::render_glass(const Scene& scene, const std::vector<NodeId>& visible,
        const Camera& camera, const SkyEnv& env, const HeadlightRig& headlights,
        const CanopyLightRig& canopy_lights) {
    std::vector<NodeId> panes;
    for (auto id:visible) {
        const auto* node=scene.get(id);
        if (node && node->renderable.material<materials_.size() &&
            materials_[node->renderable.material].glass) panes.push_back(id);
    }
    if (panes.empty()) return;
    std::stable_sort(panes.begin(),panes.end(),[&](NodeId a,NodeId b) {
        const auto da=scene.get(a)->world_bounds.center()-camera.position;
        const auto db=scene.get(b)->world_bounds.center()-camera.position;
        return glm::dot(da,da)>glm::dot(db,db);
    });
    begin_frame(camera,env,headlights,canopy_lights);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (std::size_t i=0;i<panes.size();++i)
        draw_run(scene,panes,i,1,batch_key(scene.get(panes[i])->renderable));
    glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

}  // namespace apricot
