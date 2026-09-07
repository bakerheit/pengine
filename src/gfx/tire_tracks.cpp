#include "gfx/tire_tracks.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

#include "core/log.h"
#include "gfx/camera.h"
#include "gfx/gl_state.h"
#include "gfx/sky.h"

namespace apricot {
namespace {

constexpr std::size_t kTrackCapacity = 2048u;
constexpr float kStampSpacingM = 0.32f;
constexpr float kTrailBreakDistanceM = 2.5f;
constexpr float kGroundLiftM = 0.018f;
constexpr float kHalfLengthM = 0.27f;
constexpr uint64_t kStepsPerSecond = 120u;

const void* attribute_offset(std::size_t bytes) {
    return reinterpret_cast<const void*>(bytes);
}

uint64_t track_lifetime(TireTrackSurface surface) {
    switch (surface) {
        case TireTrackSurface::Rubber: return kStepsPerSecond * 18u;
        case TireTrackSurface::Dirt: return kStepsPerSecond * 10u;
        case TireTrackSurface::Snow: return kStepsPerSecond * 42u;
    }
    return kStepsPerSecond * 10u;
}

float track_half_width(TireTrackSurface surface) {
    return surface == TireTrackSurface::Snow ? 0.165f : 0.105f;
}

glm::vec3 track_color(TireTrackSurface surface) {
    switch (surface) {
        case TireTrackSurface::Rubber: return {0.032f, 0.029f, 0.030f};
        case TireTrackSurface::Dirt: return {0.24f, 0.15f, 0.075f};
        // Compressed snow exposes a cold, dense layer below the powder. Keep
        // enough contrast to read in a whiteout without turning it into a
        // black rubber stripe.
        case TireTrackSurface::Snow: return {0.18f, 0.23f, 0.29f};
    }
    return {0.04f, 0.04f, 0.04f};
}

struct SavedGlState {
    GLboolean depth_test = GL_FALSE;
    GLboolean blend = GL_FALSE;
    GLboolean cull = GL_FALSE;
    GLboolean polygon_offset = GL_FALSE;
    GLboolean depth_mask = GL_TRUE;
    GLint blend_src_rgb = GL_ONE;
    GLint blend_dst_rgb = GL_ZERO;
    GLint blend_src_alpha = GL_ONE;
    GLint blend_dst_alpha = GL_ZERO;
    GLint program = 0;
    GLint vao = 0;
    GLint array_buffer = 0;
    GLfloat polygon_factor = 0.0f;
    GLfloat polygon_units = 0.0f;

    void capture() {
        depth_test = glIsEnabled(GL_DEPTH_TEST);
        blend = glIsEnabled(GL_BLEND);
        cull = glIsEnabled(GL_CULL_FACE);
        polygon_offset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &polygon_factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &polygon_units);
    }

    void restore() const {
        glBlendFuncSeparate(static_cast<GLenum>(blend_src_rgb),
                            static_cast<GLenum>(blend_dst_rgb),
                            static_cast<GLenum>(blend_src_alpha),
                            static_cast<GLenum>(blend_dst_alpha));
        depth_test ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
        blend ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
        cull ? glEnable(GL_CULL_FACE) : glDisable(GL_CULL_FACE);
        polygon_offset ? glEnable(GL_POLYGON_OFFSET_FILL)
                       : glDisable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(polygon_factor, polygon_units);
        glDepthMask(depth_mask);
        gl_state::use_program(static_cast<GLuint>(program));
        gl_state::bind_vertex_array(static_cast<GLuint>(vao));
        gl_state::bind_buffer(GL_ARRAY_BUFFER,
                              static_cast<GLuint>(array_buffer));
    }
};

}  // namespace

TireTracks::~TireTracks() { destroy(); }

bool TireTracks::init() {
    if (!shader_.build_from_files("shaders/tire_tracks.vert",
                                  "shaders/tire_tracks.frag")) {
        AP_ERROR("tire tracks: shader failed to build");
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (!vao_ || !vbo_) {
        AP_ERROR("tire tracks: GL refused to create buffers");
        destroy();
        return false;
    }

    gl_state::bind_vertex_array(vao_);
    gl_state::bind_buffer(GL_ARRAY_BUFFER, vbo_);
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(Vertex, color)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(Vertex, alpha)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride,
                          attribute_offset(offsetof(Vertex, surface)));
    gl_state::bind_vertex_array(0);

    stamps_.assign(kTrackCapacity, Stamp{});
    vertices_.reserve(kTrackCapacity * 6u);
    return true;
}

void TireTracks::destroy() {
    shader_.destroy();
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
    stamps_.clear();
    vertices_.clear();
    trails_ = {};
    head_ = 0;
    latest_step_ = 0;
    drawn_quads_ = 0;
}

void TireTracks::add(const TireTrackEmission& emission, glm::vec3 position,
                     glm::vec3 direction, uint64_t step) {
    if (stamps_.empty()) return;
    direction -= emission.normal * glm::dot(direction, emission.normal);
    const float length = glm::length(direction);
    if (!(length > 1e-5f)) direction = emission.direction;
    else direction /= length;

    Stamp& stamp = stamps_[head_];
    stamp.position = position;
    stamp.normal = emission.normal;
    stamp.direction = direction;
    stamp.surface = emission.surface;
    stamp.intensity = emission.intensity;
    stamp.born_step = step;
    stamp.lifetime_steps = track_lifetime(emission.surface);
    stamp.live = true;
    head_ = (head_ + 1u) % stamps_.size();
}

void TireTracks::step(const VehicleState& car, float handbrake,
                      float snow_cover, uint64_t step) {
    latest_step_ = step;
    for (int wheel_index = 0; wheel_index < kWheelCount; ++wheel_index) {
        const TireTrackEmission emission = tire_track_emission(
            car, wheel_index, handbrake, snow_cover);
        Trail& trail = trails_[static_cast<std::size_t>(wheel_index)];
        // Front tyres establish ordinary snow grooves. Rear tyres add a second
        // path only when they actually step out, which avoids stamping the same
        // straight line twice while retaining a four-wheel drift shape.
        const bool rear = wheel_index == kWheelRearLeft ||
                          wheel_index == kWheelRearRight;
        if (!emission.emit ||
            (emission.surface == TireTrackSurface::Snow && rear &&
             !emission.sliding)) {
            trail.active = false;
            continue;
        }

        glm::vec3 position = emission.position;
        // Wheel contact is the authoritative surface. In snow that point is
        // raised by the accumulated physical layer; lowering the decal to the
        // old bare-ground mesh makes the depth buffer bury the groove.
        position += emission.normal * kGroundLiftM;

        glm::vec3 direction = emission.direction;
        bool should_add = true;
        if (trail.active && trail.surface == emission.surface) {
            const glm::vec3 delta = position - trail.position;
            const float distance = glm::length(delta);
            if (distance < kStampSpacingM) {
                should_add = false;
            } else if (distance <= kTrailBreakDistanceM) {
                direction = delta / distance;
            }
        }
        if (should_add) {
            add(emission, position, direction, step);
            trail.position = position;
            trail.surface = emission.surface;
            trail.active = true;
        }
    }
}

std::size_t TireTracks::live_count() const {
    std::size_t count = 0;
    for (const Stamp& stamp : stamps_) {
        if (stamp.live && latest_step_ >= stamp.born_step &&
            latest_step_ - stamp.born_step < stamp.lifetime_steps) {
            ++count;
        }
    }
    return count;
}

std::size_t TireTracks::live_count(TireTrackSurface surface) const {
    std::size_t count = 0;
    for (const Stamp& stamp : stamps_) {
        if (stamp.live && stamp.surface == surface &&
            latest_step_ >= stamp.born_step &&
            latest_step_ - stamp.born_step < stamp.lifetime_steps) {
            ++count;
        }
    }
    return count;
}

bool TireTracks::recent_bounds(glm::vec3& minimum, glm::vec3& maximum,
                               std::size_t max_stamps) const {
    if (stamps_.empty() || max_stamps == 0u) return false;
    bool found = false;
    const std::size_t count = std::min(max_stamps, stamps_.size());
    for (std::size_t offset = 1u; offset <= count; ++offset) {
        const std::size_t index =
            (head_ + stamps_.size() - offset) % stamps_.size();
        const Stamp& stamp = stamps_[index];
        if (!stamp.live || latest_step_ < stamp.born_step ||
            latest_step_ - stamp.born_step >= stamp.lifetime_steps) {
            continue;
        }
        if (!found) {
            minimum = maximum = stamp.position;
            found = true;
        } else {
            minimum = glm::min(minimum, stamp.position);
            maximum = glm::max(maximum, stamp.position);
        }
    }
    return found;
}

void TireTracks::render(const Camera& camera, const SkyEnv& env,
                        const HeadlightRig& headlights,
                        const CanopyLightRig& canopy_lights) {
    drawn_quads_ = 0;
    if (!valid()) return;
    vertices_.clear();

    for (Stamp& stamp : stamps_) {
        if (!stamp.live || latest_step_ < stamp.born_step) continue;
        const uint64_t age_steps = latest_step_ - stamp.born_step;
        if (age_steps >= stamp.lifetime_steps) {
            stamp.live = false;
            continue;
        }
        if (stamp.surface == TireTrackSurface::Snow && env.snow_cover < 0.05f) {
            continue;
        }

        const glm::vec3 camera_delta = stamp.position - camera.position;
        if (glm::dot(camera_delta, camera_delta) > 180.0f * 180.0f) continue;

        glm::vec3 forward = stamp.direction -
            stamp.normal * glm::dot(stamp.direction, stamp.normal);
        const float forward_length = glm::length(forward);
        if (!(forward_length > 1e-5f)) continue;
        forward /= forward_length;
        glm::vec3 right = glm::cross(stamp.normal, forward);
        const float right_length = glm::length(right);
        if (!(right_length > 1e-5f)) continue;
        right /= right_length;

        const float half_width = track_half_width(stamp.surface);
        const glm::vec3 along = forward * kHalfLengthM;
        const glm::vec3 across = right * half_width;
        const float life = static_cast<float>(age_steps) /
                           static_cast<float>(stamp.lifetime_steps);
        const float alpha =
            (stamp.surface == TireTrackSurface::Snow
                 ? 0.58f + stamp.intensity * 0.34f
                 : 0.22f + stamp.intensity * 0.58f) *
            (1.0f - life);
        const glm::vec3 color = track_color(stamp.surface);
        const float surface = static_cast<float>(stamp.surface);

        const Vertex bl{stamp.position - along - across, stamp.normal,
                        {-1.0f, -1.0f}, color, alpha, surface};
        const Vertex br{stamp.position - along + across, stamp.normal,
                        {1.0f, -1.0f}, color, alpha, surface};
        const Vertex tr{stamp.position + along + across, stamp.normal,
                        {1.0f, 1.0f}, color, alpha, surface};
        const Vertex tl{stamp.position + along - across, stamp.normal,
                        {-1.0f, 1.0f}, color, alpha, surface};
        vertices_.push_back(bl);
        vertices_.push_back(br);
        vertices_.push_back(tr);
        vertices_.push_back(bl);
        vertices_.push_back(tr);
        vertices_.push_back(tl);
    }
    if (vertices_.empty()) return;

    SavedGlState saved;
    saved.capture();
    gl_state::bind_vertex_array(vao_);
    gl_state::bind_buffer(GL_ARRAY_BUFFER, vbo_);
    const std::size_t bytes = vertices_.size() * sizeof(Vertex);
    if (bytes > vbo_capacity_bytes_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes),
                     vertices_.data(), GL_STREAM_DRAW);
        vbo_capacity_bytes_ = bytes;
    } else {
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vbo_capacity_bytes_), nullptr,
                     GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes),
                        vertices_.data());
    }

    shader_.bind();
    shader_.set_mat4("u_view_proj", camera.view_projection());
    apply_lighting(shader_, env, camera.position, headlights, canopy_lights);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glDrawArrays(GL_TRIANGLES, 0,
                 static_cast<GLsizei>(vertices_.size()));
    drawn_quads_ = static_cast<int>(vertices_.size() / 6u);
    saved.restore();
}

}  // namespace apricot
