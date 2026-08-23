#pragma once

#include <string>
#include <unordered_map>

#include <glad/gl.h>

#include <glm/glm.hpp>

namespace apricot {

// A linked GL program plus uniform setters.
//
// Two things this class exists to do properly, because the alternative is a
// black screen and a lost afternoon:
//
//  1. FAILURE IS LOUD. A failed compile or link logs the driver's info log AND
//     the offending source line, quoted, with the file and line number it came
//     from ORIGINALLY — not an offset into the post-#include blob. valid()
//     stays false afterwards, so a half-built program never gets bound.
//
//  2. #include WORKS. Sources may `#include "other.glsl"` relative to the
//     including file. The loader splices them and keeps a line map, which is
//     the only reason point 1 can tell the truth.
//
// Uniform locations are cached by name. -1 (the uniform was optimised out, or
// was never there) is cached too — re-asking the driver every frame for a name
// it has already said no to is pure waste.
class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Compile and link from GLSL source. Returns false and logs the driver's
    // info log on failure. Both stages are #version 330 core. `label` names the
    // program in log messages; give it something a bug report can search for.
    bool build(const std::string& vertex_src, const std::string& fragment_src,
               const std::string& label = "<inline>");

    // Same, but reads both stages from files under the asset root. Paths are
    // relative to the asset root, e.g. "shaders/lit.frag".
    bool build_from_files(const std::string& vertex_rel,
                          const std::string& fragment_rel);

    void destroy();

    // Binds through gl_state, never glUseProgram directly.
    void bind() const;

    GLuint id() const { return program_; }
    bool valid() const { return program_ != 0; }

    // The label this program was built with. Empty before a successful build.
    const std::string& label() const { return label_; }

    // Every setter binds nothing: the program must already be bound. They are
    // silent no-ops for a name the program does not have, which is deliberate —
    // a shared include means callers legitimately set uniforms a given variant
    // dropped, and warning on that would train everyone to ignore warnings.
    void set_int(const char* name, int v) const;
    void set_float(const char* name, float v) const;
    void set_vec2(const char* name, const glm::vec2& v) const;
    void set_vec3(const char* name, const glm::vec3& v) const;
    void set_vec4(const char* name, const glm::vec4& v) const;
    void set_mat3(const char* name, const glm::mat3& v) const;
    void set_mat4(const char* name, const glm::mat4& v) const;

private:
    GLint location(const char* name) const;

    GLuint program_ = 0;
    std::string label_;

    // Mutable so a logically-const setter can populate the cache on first use.
    mutable std::unordered_map<std::string, GLint> uniform_cache_;
};

}  // namespace apricot
