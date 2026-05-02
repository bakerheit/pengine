#include "render/shader.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "core/log.h"
#include "render/gl_state.h"

namespace pengine {

namespace {

std::time_t mtime(const std::string& path) {
    struct ::stat st;
    return ::stat(path.c_str(), &st) == 0 ? static_cast<std::time_t>(st.st_mtime) : 0;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) { PE_ERROR("Cannot open shader: %s", path.c_str()); return {}; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

GLuint compile(GLenum type, const std::string& src, const std::string& path) {
    const char* c = src.c_str();
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        PE_ERROR("Shader compile error [%s]:\n%s", path.c_str(), log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

} // namespace

bool Shader::load(std::string vert_path, std::string frag_path) {
    vert_path_ = std::move(vert_path);
    frag_path_ = std::move(frag_path);
    return compile_and_link();
}

void Shader::destroy() {
    if (program_) { glDeleteProgram(program_); program_ = 0; }
}

bool Shader::compile_and_link() {
    std::string vsrc = read_file(vert_path_);
    std::string fsrc = read_file(frag_path_);
    if (vsrc.empty() || fsrc.empty()) return false;

    GLuint vs = compile(GL_VERTEX_SHADER,   vsrc, vert_path_);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsrc, frag_path_);
    if (!vs || !fs) { glDeleteShader(vs); glDeleteShader(fs); return false; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        PE_ERROR("Shader link error: %s", log);
        glDeleteProgram(prog);
        return false;
    }

    if (program_) glDeleteProgram(program_);
    program_ = prog;

#ifndef NDEBUG
    vert_mtime_ = mtime(vert_path_);
    frag_mtime_ = mtime(frag_path_);
#endif

    PE_DEBUG("Shader loaded: %s + %s", vert_path_.c_str(), frag_path_.c_str());
    return true;
}

void Shader::use() const {
    gl_state::use_program(program_);
}

void Shader::hot_reload() {
#ifndef NDEBUG
    if (++frame_check_ < 60) return;
    frame_check_ = 0;
    std::time_t vt = mtime(vert_path_);
    std::time_t ft = mtime(frag_path_);
    if (vt != vert_mtime_ || ft != frag_mtime_) {
        PE_INFO("Shader changed, reloading...");
        compile_and_link();
    }
#endif
}

void Shader::set(const char* name, int v) const {
    glUniform1i(glGetUniformLocation(program_, name), v);
}
void Shader::set(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(program_, name), v);
}
void Shader::set(const char* name, const glm::vec2& v) const {
    glUniform2fv(glGetUniformLocation(program_, name), 1, &v[0]);
}
void Shader::set(const char* name, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(program_, name), 1, &v[0]);
}
void Shader::set(const char* name, const glm::mat3& v) const {
    glUniformMatrix3fv(glGetUniformLocation(program_, name), 1, GL_FALSE, &v[0][0]);
}
void Shader::set(const char* name, const glm::mat4& v) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE, &v[0][0]);
}
void Shader::set_mat4_array(const char* name, const glm::mat4* data, int count) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name),
                        count, GL_FALSE, &data[0][0][0]);
}

} // namespace pengine
