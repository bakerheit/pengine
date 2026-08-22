#pragma once

#include <string>

#include <glad/gl.h>

#include <glm/glm.hpp>

namespace apricot {

// A linked GL program plus uniform setters.
//
// NOT IMPLEMENTED YET — this header is the contract downstream renderer work
// is written against; shader.cpp is a stub.
// TODO(renderer ticket): compile/link with full info-log reporting on failure
// (a silent compile failure renders black, which is the hardest possible thing
// to diagnose), cache uniform locations by name, and add a dev-only reload.
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Compile and link from GLSL source. Returns false and logs the driver's
    // info log on failure. Both stages are #version 330 core.
    bool build(const std::string& vertex_src, const std::string& fragment_src);

    // Same, but reads both stages from files under the asset root.
    bool build_from_files(const std::string& vertex_rel,
                          const std::string& fragment_rel);

    void destroy();

    // Binds through gl_state, never glUseProgram directly.
    void bind() const;

    GLuint id() const { return program_; }
    bool valid() const { return program_ != 0; }

    void set_int(const char* name, int v) const;
    void set_float(const char* name, float v) const;
    void set_vec3(const char* name, const glm::vec3& v) const;
    void set_vec4(const char* name, const glm::vec4& v) const;
    void set_mat4(const char* name, const glm::mat4& v) const;

private:
    GLuint program_ = 0;
};

}  // namespace apricot
