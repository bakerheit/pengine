#include "gfx/shader.h"

#include "core/log.h"
#include "gfx/gl_state.h"

namespace apricot {

// ============================================================================
//  STUB. Every function here is deliberately inert. It exists so the module
//  has a translation unit, links, and presents the header's contract to
//  callers being written in parallel.
//  TODO(renderer ticket): implement for real. Do not "fix" these one at a
//  time in passing — a half-built Shader that reports valid() is worse than
//  one that reports nothing.
// ============================================================================

Shader::~Shader() { destroy(); }

bool Shader::build(const std::string&, const std::string&) {
    AP_ERROR("Shader::build is not implemented yet");
    return false;
}

bool Shader::build_from_files(const std::string&, const std::string&) {
    AP_ERROR("Shader::build_from_files is not implemented yet");
    return false;
}

void Shader::destroy() {
    if (!program_) return;
    glDeleteProgram(program_);
    // Paired with the delete, always. See the warning in gl_state.h.
    gl_state::on_program_deleted(program_);
    program_ = 0;
}

void Shader::bind() const { gl_state::use_program(program_); }

void Shader::set_int(const char*, int) const {}
void Shader::set_float(const char*, float) const {}
void Shader::set_vec3(const char*, const glm::vec3&) const {}
void Shader::set_vec4(const char*, const glm::vec4&) const {}
void Shader::set_mat4(const char*, const glm::mat4&) const {}

}  // namespace apricot
