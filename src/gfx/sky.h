#pragma once

#include <glad/gl.h>

#include <glm/glm.hpp>

#include "gfx/camera.h"
#include "gfx/shader.h"
#include "gfx/sky_env.h"

namespace apricot {

// Push a SkyEnv into an ALREADY BOUND shader.
//
// The one place the lighting uniform block gets written. It sets every uniform
// assets/shaders/lighting.glsl declares, every time, including the ones that
// happen to be zero — a "skip it if it's the default" optimisation here means
// the value left over from the previous program's frame leaks into this one,
// and that bug looks like the fog following you between passes.
void apply_lighting(const Shader& shader, const SkyEnv& env,
                    const glm::vec3& camera_position);

// The procedural sky pass. Owns its shader and an empty VAO — the geometry is
// three vertices generated from gl_VertexID, so there is no vertex buffer to
// own and nothing to upload.
class Sky {
public:
    Sky() = default;
    ~Sky();

    Sky(const Sky&) = delete;
    Sky& operator=(const Sky&) = delete;

    // Loads assets/shaders/sky.{vert,frag} and creates the VAO. Returns false
    // (having logged why) if the shader did not build.
    bool init();
    void destroy();

    bool valid() const { return shader_.valid() && vao_ != 0; }

    // Draw the sky. Call FIRST, before any opaque geometry: it writes no depth,
    // so anything drawn afterwards covers it, and drawing it last would mean
    // paying for every sky pixel the world already hid.
    //
    // `anim_time` drives cloud drift and star twinkle in seconds. Feed it sim
    // time, not wall time — App owns the only clock, and a sky animated off
    // wall time makes a replay's sky diverge from the run it recorded.
    void render(const Camera& camera, const SkyEnv& env, float anim_time);

private:
    Shader shader_;
    GLuint vao_ = 0;
};

}  // namespace apricot
