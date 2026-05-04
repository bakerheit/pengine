#include "render/particles.h"

#include <algorithm>
#include <cmath>

#include "render/gl_state.h"

namespace pengine {

namespace {

constexpr float GRAVITY      = -16.f;  // m/s^2 — matches the punchy car-physics gravity
constexpr float AIR_DRAG_INV_TAU = 1.5f; // 1/s exponential drag on velocity

inline float frand(std::mt19937& rng, float lo, float hi) {
    std::uniform_real_distribution<float> d(lo, hi);
    return d(rng);
}

} // namespace

bool Particles::init(const std::string& assets_root) {
    if (!shader_.load(assets_root + "/shaders/particles.vert",
                      assets_root + "/shaders/particles.frag")) return false;

    glGenVertexArrays(1, &vao_);
    glGenBuffers     (1, &vbo_);

    gl_state::bind_vao(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    // Layout matches GpuVertex: vec3 pos | vec4 color | float size.
    GLsizei stride = sizeof(GpuVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(GpuVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(GpuVertex, color)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(GpuVertex, size)));

    gl_state::bind_vao(0);
    return true;
}

void Particles::shutdown() {
    if (vbo_) { glDeleteBuffers     (1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    shader_.destroy();
    particles_.clear();
    upload_buf_.clear();
}

void Particles::update(float dt) {
    float drag = std::exp(-AIR_DRAG_INV_TAU * dt);
    for (auto& p : particles_) {
        p.vel.y += GRAVITY * dt;
        p.vel   *= drag;
        p.pos   += p.vel * dt;
        p.age   += dt;
    }
    // Compact: drop expired particles.
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                        [](const Particle& p) { return p.age >= p.max_age; }),
        particles_.end());
}

void Particles::emit_sparks(const glm::vec3& pos,
                             const glm::vec3& base_vel,
                             int count) {
    if (count <= 0) return;
    particles_.reserve(particles_.size() + static_cast<std::size_t>(count));

    // Reflect tangential velocity backward and upward — sparks fly opposite
    // to the corner's slide direction, kicked off the ground.
    glm::vec3 tan = base_vel;
    tan.y = 0.f;
    float tan_speed = glm::length(tan);
    glm::vec3 tan_dir = (tan_speed > 0.1f) ? -tan / tan_speed
                                            : glm::vec3{0.f, 0.f, 1.f};
    float launch_speed = std::min(8.f, 2.f + tan_speed * 0.35f);

    for (int i = 0; i < count; ++i) {
        Particle p;
        p.pos = pos;

        // Cone around (tan_dir + up). Random yaw spread + random pitch.
        float yaw   = frand(rng_, -1.0f, 1.0f); // ~rad spread
        float pitch = frand(rng_,  0.2f, 1.2f); // mostly up
        glm::vec3 right = glm::normalize(glm::cross(
            glm::vec3{0.f, 1.f, 0.f},
            std::abs(tan_dir.y) > 0.9f ? glm::vec3{1.f, 0.f, 0.f} : tan_dir));
        glm::vec3 dir = std::cos(pitch) * (std::cos(yaw) * tan_dir
                                            + std::sin(yaw) * right)
                       + std::sin(pitch) * glm::vec3{0.f, 1.f, 0.f};
        p.vel = dir * frand(rng_, launch_speed * 0.6f, launch_speed * 1.4f);

        p.age     = 0.f;
        p.max_age = frand(rng_, 0.35f, 0.7f);
        // Hot sparks: bright yellow-white at spawn fading to deep red.
        p.color_start = {1.0f, 0.95f, 0.55f};
        p.color_end   = {1.0f, 0.25f, 0.05f};
        p.size_px     = frand(rng_, 90.f, 160.f); // base px at 1 m

        particles_.push_back(p);
    }
}

void Particles::render(const glm::mat4& view_proj,
                        const glm::vec3& cam_pos,
                        float viewport_height_px) {
    if (particles_.empty()) return;

    upload_buf_.clear();
    upload_buf_.reserve(particles_.size());
    for (const auto& p : particles_) {
        float life     = (p.max_age > 0.f) ? p.age / p.max_age : 1.f;
        life           = std::min(1.f, std::max(0.f, life));
        glm::vec3 rgb  = p.color_start + (p.color_end - p.color_start) * life;
        // Alpha ramps in fast then trails out so sparks pop at spawn and
        // dim toward death without clipping to 0 abruptly.
        float alpha    = (1.f - life) * (1.f - life);
        upload_buf_.push_back({p.pos, glm::vec4{rgb, alpha}, p.size_px});
    }

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    std::size_t bytes = upload_buf_.size() * sizeof(GpuVertex);
    if (bytes > vbo_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes),
                     upload_buf_.data(), GL_DYNAMIC_DRAW);
        vbo_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(bytes), upload_buf_.data());
    }

    GLboolean depth_was_on = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blend_was_on = glIsEnabled(GL_BLEND);
    GLboolean dmask_was_on;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &dmask_was_on);
    GLint blend_src, blend_dst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);   // additive
    glDepthMask(GL_FALSE);          // sparks don't occlude themselves
    glEnable(GL_PROGRAM_POINT_SIZE);

    shader_.use();
    shader_.set("u_view_proj",  view_proj);
    shader_.set("u_cam_pos",    cam_pos);
    // Scale point size so a particle that says "100 px at 1 m" actually
    // renders at ~ that pixel size. The vertex shader divides by distance.
    shader_.set("u_size_scale", viewport_height_px * 0.001f);

    gl_state::bind_vao(vao_);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(upload_buf_.size()));
    gl_state::bind_vao(0);

    glDisable(GL_PROGRAM_POINT_SIZE);
    glDepthMask(dmask_was_on ? GL_TRUE : GL_FALSE);
    if (!blend_was_on) glDisable(GL_BLEND);
    else               glBlendFunc(static_cast<GLenum>(blend_src),
                                    static_cast<GLenum>(blend_dst));
    if (!depth_was_on) glDisable(GL_DEPTH_TEST);
}

} // namespace pengine
