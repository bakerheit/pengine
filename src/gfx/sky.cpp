#include "gfx/sky.h"

#include "core/log.h"
#include "gfx/gl_state.h"

namespace apricot {

void apply_lighting(const Shader& shader, const SkyEnv& env,
                    const glm::vec3& camera_position) {
    shader.set_vec3("u_light_dir", env.light_dir);
    shader.set_vec3("u_light_color", env.light_color);
    shader.set_vec3("u_ambient", env.ambient);
    shader.set_vec3("u_cam_pos", camera_position);
    shader.set_float("u_specular_strength", env.specular_strength);

    // Fog is set unconditionally, including when it is off. The shader's
    // no-op test is `fog_end <= fog_start`, and leaving the previous frame's
    // band in place is exactly how "the fog is still there after the storm
    // cleared" happens.
    shader.set_vec3("u_fog_color", env.fog_color);
    shader.set_float("u_fog_start", env.fog_start);
    shader.set_float("u_fog_end", env.fog_end);
    shader.set_float("u_fog_density", env.fog_density);
}

Sky::~Sky() { destroy(); }

bool Sky::init() {
    if (!shader_.build_from_files("shaders/sky.vert", "shaders/sky.frag")) {
        AP_ERROR("sky: shader failed to build; there will be no sky pass");
        return false;
    }
    if (vao_ == 0) {
        // An empty VAO with no attributes. GL 3.3 core still requires SOME VAO
        // to be bound for a draw to be legal, even when the vertex shader reads
        // nothing but gl_VertexID.
        glGenVertexArrays(1, &vao_);
        if (vao_ == 0) {
            AP_ERROR("sky: GL refused to create a VAO");
            shader_.destroy();
            return false;
        }
    }
    return true;
}

void Sky::destroy() {
    shader_.destroy();
    if (vao_) {
        glDeleteVertexArrays(1, &vao_);
        // Paired with the delete, always. See the warning in gl_state.h.
        gl_state::on_vertex_array_deleted(vao_);
        vao_ = 0;
    }
}

void Sky::render(const Camera& camera, const SkyEnv& env, float anim_time) {
    if (!valid()) return;

    // Rotation-only inverse view-projection. Stripping the translation is what
    // makes the sky infinitely far away; feed it the full inverse and the
    // horizon slides around as the car drives, which reads as the world being
    // inside a small painted box.
    const glm::mat4 view_rot = glm::mat4(glm::mat3(camera.view()));
    const glm::mat4 inv = glm::inverse(camera.projection() * view_rot);

    shader_.bind();
    shader_.set_mat4("u_inv_view_rot_proj", inv);
    shader_.set_vec3("u_sun_dir", env.sun_dir);
    shader_.set_vec3("u_sun_color", env.sun_color);
    shader_.set_vec3("u_sky_top", env.sky_top);
    shader_.set_vec3("u_sky_bottom", env.sky_bottom);
    shader_.set_vec3("u_cloud_color", env.cloud_color);
    shader_.set_float("u_time", anim_time);
    shader_.set_float("u_star_intensity", env.star_intensity);
    shader_.set_float("u_cloud_cover", env.cloud_cover);

    // No depth test and no depth write: the sky is a background, and letting it
    // write depth would make it occlude the world it is behind.
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    // The fullscreen triangle's third corner winds whichever way it winds; with
    // culling on, half the time you get a black screen and no error.
    glDisable(GL_CULL_FACE);

    gl_state::bind_vertex_array(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

}  // namespace apricot
