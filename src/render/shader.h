#pragma once

#include <string>
#include <ctime>
#include <glad/gl.h>
#include <glm/glm.hpp>

namespace pengine {

class Shader {
public:
    Shader() = default;
    ~Shader() { destroy(); }

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool load(std::string vert_path, std::string frag_path);
    void destroy();

    void use() const;

    // In Debug builds, re-checks file mtimes every 60 frames and recompiles
    // if either source changed. No-op in Release.
    void hot_reload();

    GLuint id() const { return program_; }

    void set(const char* name, int v)             const;
    void set(const char* name, float v)           const;
    void set(const char* name, const glm::vec2& v) const;
    void set(const char* name, const glm::vec3& v) const;
    void set(const char* name, const glm::mat3& v) const;
    void set(const char* name, const glm::mat4& v) const;
    void set_mat4_array(const char* name, const glm::mat4* data, int count) const;

private:
    bool compile_and_link();

    GLuint      program_    = 0;
    std::string vert_path_;
    std::string frag_path_;

#ifndef NDEBUG
    std::time_t vert_mtime_   = 0;
    std::time_t frag_mtime_   = 0;
    int         frame_check_  = 0;
#endif
};

} // namespace pengine
