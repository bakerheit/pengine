#include "gfx/precip.h"

#include <algorithm>
#include <cstddef>

#include "core/log.h"
#include "core/rng.h"
#include "gfx/gl_state.h"
#include "gfx/sky.h"

namespace apricot {
namespace {

const void* attrib_offset(std::size_t bytes) {
    return reinterpret_cast<const void*>(bytes);
}

const SnowTuning& active_snow_tuning(PrecipitationType type,
                                     const SnowTuning& snow,
                                     const SnowTuning& blizzard) {
    return type == PrecipitationType::Blizzard ? blizzard : snow;
}

struct SavedGlState {
    GLboolean blend = GL_FALSE;
    GLboolean cull = GL_FALSE;
    GLboolean depth_mask = GL_TRUE;
    GLint blend_src_rgb = GL_ONE;
    GLint blend_dst_rgb = GL_ZERO;
    GLint blend_src_alpha = GL_ONE;
    GLint blend_dst_alpha = GL_ZERO;
    GLint program = 0;
    GLint vao = 0;
    GLint array_buffer = 0;

    void capture() {
        blend = glIsEnabled(GL_BLEND);
        cull = glIsEnabled(GL_CULL_FACE);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
    }

    void restore() const {
        glBlendFuncSeparate(static_cast<GLenum>(blend_src_rgb),
                            static_cast<GLenum>(blend_dst_rgb),
                            static_cast<GLenum>(blend_src_alpha),
                            static_cast<GLenum>(blend_dst_alpha));
        blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        cull ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        glDepthMask(depth_mask);
        gl_state::use_program(static_cast<GLuint>(program));
        gl_state::bind_vertex_array(static_cast<GLuint>(vao));
        gl_state::bind_buffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer));
    }
};

}  // namespace

Precipitation::~Precipitation() { destroy(); }

bool Precipitation::init(uint64_t seed) {
    seed_ = seed;

    if (!shader_.build_from_files("shaders/precip.vert", "shaders/precip.frag")) {
        AP_ERROR("precip: shader failed to build; precipitation is disabled");
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (!vao_ || !vbo_) {
        AP_ERROR("precip: GL refused to create the streak buffers");
        destroy();
        return false;
    }

    gl_state::bind_vertex_array(vao_);
    gl_state::bind_buffer(GL_ARRAY_BUFFER, vbo_);

    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(StreakVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(StreakVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(StreakVertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          attrib_offset(offsetof(StreakVertex, alpha)));

    gl_state::bind_vertex_array(0);

    // Reserve the full field once. Growing the vectors during a downpour would
    // allocate inside the frame that is already the heaviest one.
    drops_.reserve(static_cast<std::size_t>(tuning_.drop_count));
    drop_alpha_.reserve(static_cast<std::size_t>(tuning_.drop_count));
    particle_size_.reserve(static_cast<std::size_t>(tuning_.drop_count));
    return true;
}

void Precipitation::set_type(PrecipitationType type) {
    if (type_ == type) return;
    type_ = type;
    drops_.clear();
    drop_alpha_.clear();
    particle_size_.clear();
    verts_.clear();
    elapsed_ = 0.0f;
    intensity_ = 0.0f;
    live_drops_ = 0;
    drawn_quads_ = 0;
}

void Precipitation::destroy() {
    shader_.destroy();
    // Every delete pairs with its gl_state hook. See the warning in gl_state.h.
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        gl_state::on_buffer_deleted(vbo_);
        vbo_ = 0;
    }
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        gl_state::on_vertex_array_deleted(vao_);
        vao_ = 0;
    }
    vbo_capacity_bytes_ = 0;
    drops_.clear();
    drop_alpha_.clear();
    particle_size_.clear();
    verts_.clear();
    elapsed_ = 0.0f;
    intensity_ = 0.0f;
    live_drops_ = 0;
    drawn_quads_ = 0;
}

void Precipitation::update(const Camera& camera, float intensity, float dt) {
    intensity_ = std::clamp(intensity, 0.0f, 1.0f);

    const SnowTuning& snow = active_snow_tuning(type_, snow_tuning_, blizzard_tuning_);
    const int want = type_ == PrecipitationType::Rain
        ? rain_drop_count(tuning_, intensity_)
        : snow_flake_count(snow, intensity_);
    live_drops_ = want;

    if (want <= 0) {
        // Dry. Keep the allocation, do no work, and let render() draw nothing.
        return;
    }

    // Grow or shrink the field to the intensity. New drops are seeded from
    // hash_coord by INDEX, so drop 900 lands in the same place whether the
    // storm ramped up gradually or started at full strength.
    const std::size_t target = static_cast<std::size_t>(want);
    while (drops_.size() < target) {
        const int index = static_cast<int>(drops_.size());
        drops_.push_back(type_ == PrecipitationType::Rain
            ? rain_seed_position(tuning_, camera.position, seed_, index)
            : snow_seed_position(snow, camera.position, seed_, index, type_));
        const uint32_t channel = type_ == PrecipitationType::Rain
            ? 0x7A14u : snow_channel(type_);
        Rng r = rng_at(seed_, index, 2, channel);
        // Per-drop alpha jitter, so the field reads as depth rather than as one
        // flat sheet of identical streaks.
        drop_alpha_.push_back(r.range(0.45f, 1.0f));
        particle_size_.push_back(r.range(0.78f, 1.22f));
    }
    if (drops_.size() > target) {
        drops_.resize(target);
        drop_alpha_.resize(target);
        particle_size_.resize(target);
    }

    const float step = std::max(dt, 0.0f);
    for (std::size_t i = 0; i < drops_.size(); ++i) {
        drops_[i] = type_ == PrecipitationType::Rain
            ? rain_advance(drops_[i], tuning_, camera.position, step)
            : snow_advance(drops_[i], snow, camera.position, seed_,
                           static_cast<int>(i), elapsed_, step, type_);
    }
    elapsed_ += step;
}

void Precipitation::render(const Camera& camera, const SkyEnv& env,
                           const HeadlightRig& headlights,
                           const CanopyLightRig& canopy_lights) {
    drawn_quads_ = 0;
    if (!valid() || live_drops_ <= 0 || intensity_ <= 0.0f) return;

    const bool rain = type_ == PrecipitationType::Rain;
    const glm::vec3 fall = rain_fall_dir(tuning_);
    const glm::vec3 view = camera.forward();

    // Streak thickness runs perpendicular to BOTH the fall direction and the
    // view direction, which is what makes the quad face the camera. When the
    // camera looks straight along the fall direction the cross product
    // degenerates; fall back to the camera's right axis so a drop seen
    // end-on becomes a dot instead of a NaN.
    glm::vec3 side = glm::cross(fall, view);
    const float side_len = glm::length(side);
    side = side_len > 1e-4f ? side / side_len : camera.right();

    const glm::vec3 along = fall * tuning_.streak_len;
    const glm::vec3 half = side * tuning_.half_width;
    const glm::vec3 cam_right = camera.right();
    const glm::vec3 cam_up = camera.up();
    const SnowTuning& snow = active_snow_tuning(type_, snow_tuning_, blizzard_tuning_);

    verts_.clear();
    verts_.reserve(drops_.size() * 6u);

    for (std::size_t i = 0; i < drops_.size(); ++i) {
        const glm::vec3 head = drops_[i];
        const float a = drop_alpha_[i];

        StreakVertex h0;
        StreakVertex h1;
        StreakVertex t1;
        StreakVertex t0;
        if (rain) {
            const glm::vec3 tail = head - along;
            h0 = {head - half, {-1.0f, 0.0f}, a};
            h1 = {head + half, {1.0f, 0.0f}, a};
            t1 = {tail + half, {1.0f, 1.0f}, a};
            t0 = {tail - half, {-1.0f, 1.0f}, a};
        } else {
            const float radius = snow.half_size * particle_size_[i];
            const glm::vec3 right = cam_right * radius;
            const glm::vec3 up = cam_up * radius;
            h0 = {head - right + up, {-1.0f, 1.0f}, a};
            h1 = {head + right + up, {1.0f, 1.0f}, a};
            t1 = {head + right - up, {1.0f, -1.0f}, a};
            t0 = {head - right - up, {-1.0f, -1.0f}, a};
        }

        verts_.push_back(h0);
        verts_.push_back(h1);
        verts_.push_back(t1);
        verts_.push_back(h0);
        verts_.push_back(t1);
        verts_.push_back(t0);
    }
    if (verts_.empty()) return;

    SavedGlState saved;
    saved.capture();

    gl_state::bind_vertex_array(vao_);
    gl_state::bind_buffer(GL_ARRAY_BUFFER, vbo_);

    const std::size_t bytes = verts_.size() * sizeof(StreakVertex);
    if (bytes > vbo_capacity_bytes_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), verts_.data(),
                     GL_STREAM_DRAW);
        vbo_capacity_bytes_ = bytes;
    } else {
        // Orphan then fill, same reasoning as Mesh::upload_instances: the
        // previous frame's draw may still be reading this store.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vbo_capacity_bytes_),
                     nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                        verts_.data());
    }

    shader_.bind();
    shader_.set_mat4("u_view_proj", camera.view_projection());
    shader_.set_int("u_type", static_cast<int>(type_));
    shader_.set_vec3("u_color", rain ? tuning_.color : snow.color);
    shader_.set_float("u_opacity", (rain ? tuning_.opacity : snow.opacity) * intensity_);
    apply_lighting(shader_, env, camera.position, headlights, canopy_lights);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Depth TEST on so rain behind a hill is hidden, depth WRITE off so drops
    // do not occlude each other and turn the field into a wall of grey.
    glDepthMask(GL_FALSE);
    // The streak quads are built facing the camera, so their winding depends on
    // which side of the fall direction the camera is on. With culling on, half
    // the rain vanishes and it looks like a density bug.
    glDisable(GL_CULL_FACE);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    drawn_quads_ = static_cast<int>(verts_.size() / 6u);

    saved.restore();
}

}  // namespace apricot
