#include "gfx/shader.h"

#include <cctype>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/asset_root.h"
#include "core/log.h"
#include "gfx/gl_state.h"

namespace apricot {
namespace {

// How deep #include may nest before we call it a cycle. Shader includes are a
// handful of shared maths files; anything past this is a loop, and a loop here
// eats all the memory on the machine before it ever reports anything.
constexpr int kMaxIncludeDepth = 8;

// Where one line of the spliced source actually came from.
//
// Without this, a compile error inside lighting.glsl gets reported as a line
// number in lit.frag and points at whatever innocent statement happens to live
// there — which is WORSE than no line number, because you believe it and go
// stare at the wrong file.
//
// `file` is an index into Preprocessed::files, not a pointer: that vector grows
// while the map is being built, and a pointer into it would dangle on the next
// reallocation. An index cannot.
struct SourceLine {
    std::size_t file = 0;
    int line = 0;  // 1-based, within that file
};

struct Preprocessed {
    std::string text;
    std::vector<std::string> lines;  // text split, parallel to `map`
    std::vector<SourceLine> map;     // map[i] is the origin of lines[i]
    std::vector<std::string> files;  // display paths, referenced by SourceLine
    bool ok = false;
};

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Directory part of a path, trailing slash included, or "" if there is none.
std::string dir_of(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string{} : path.substr(0, slash + 1);
}

// Parse `#include "name"` and return `name`. Sets `is_include` when the line
// starts an include directive at all, so the caller can tell "not an include"
// from "a malformed one" and complain about the second.
std::string parse_include(const std::string& line, bool& is_include) {
    is_include = false;
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (line.compare(i, 8, "#include") != 0) return {};
    is_include = true;
    i += 8;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] != '"') return {};
    const std::size_t start = i + 1;
    const std::size_t end = line.find('"', start);
    if (end == std::string::npos) return {};
    return line.substr(start, end - start);
}

// Splice `src` into `out`, resolving #include relative to `dir`.
// `display_path` is the name error messages should use for this text.
bool splice(const std::string& src, const std::string& dir,
            const std::string& display_path, int depth, Preprocessed& out) {
    if (depth > kMaxIncludeDepth) {
        AP_ERROR("shader: #include nested deeper than %d at '%s' — cyclic include?",
                 kMaxIncludeDepth, display_path.c_str());
        return false;
    }

    const std::size_t file_index = out.files.size();
    out.files.push_back(display_path);

    std::istringstream in(src);
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;

        // Strip a trailing CR so a shader authored on Windows does not end
        // every GLSL line with a byte the driver reports as a stray token.
        if (!line.empty() && line.back() == '\r') line.pop_back();

        bool is_include = false;
        const std::string inc = parse_include(line, is_include);
        if (!is_include) {
            out.lines.push_back(line);
            out.map.push_back(SourceLine{file_index, line_no});
            continue;
        }
        if (inc.empty()) {
            AP_ERROR("shader: %s:%d: malformed #include (expected #include \"file\")",
                     display_path.c_str(), line_no);
            return false;
        }

        const std::string inc_path = dir + inc;
        const std::string inc_src = read_file(inc_path);
        if (inc_src.empty()) {
            AP_ERROR("shader: %s:%d: cannot open (or empty) included file '%s'",
                     display_path.c_str(), line_no, inc_path.c_str());
            return false;
        }

        // A #line directive would let the driver number things itself, but
        // drivers disagree about whether the argument names the next line or
        // the current one, and a compiler that reports lines off by one is a
        // compiler nobody trusts. The map is ours and it is exact.
        if (!splice(inc_src, dir_of(inc_path), inc_path, depth + 1, out)) return false;
    }
    return true;
}

Preprocessed preprocess(const std::string& src, const std::string& dir,
                        const std::string& display_path) {
    Preprocessed out;
    out.ok = splice(src, dir, display_path, 0, out);
    if (!out.ok) return out;
    for (const std::string& l : out.lines) {
        out.text += l;
        out.text += '\n';
    }
    return out;
}

// Pull 1-based line numbers out of a driver info log. Three formats in the
// wild, and every vendor picked a different one:
//   "ERROR: 0:42: 'foo' : undeclared identifier"   Apple, AMD
//   "0(42) : error C1503: undefined variable"      NVIDIA
//   "0:42(9): error: syntax error"                 Mesa
// All three lead with the source-string index, and we only ever compile one
// source string, so anchoring on a standalone '0' catches the lot.
std::vector<int> parse_error_lines(const std::string& log) {
    std::vector<int> lines;
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i] != '0') continue;
        if (i > 0 && std::isdigit(static_cast<unsigned char>(log[i - 1]))) continue;

        std::size_t j = i + 1;
        if (j >= log.size()) break;
        const char sep = log[j];
        if (sep != '(' && sep != ':') continue;
        ++j;

        int value = 0;
        std::size_t digits = 0;
        while (j < log.size() && std::isdigit(static_cast<unsigned char>(log[j]))) {
            value = value * 10 + (log[j] - '0');
            ++digits;
            ++j;
        }
        if (digits == 0 || j >= log.size()) continue;

        const char close = log[j];
        const bool nvidia = (sep == '(' && close == ')');
        const bool colons = (sep == ':' && (close == ':' || close == '('));
        if (!nvidia && !colons) continue;

        lines.push_back(value);
    }
    return lines;
}

// Quote the source lines the driver complained about, naming the file they
// really came from. This is the entire point of keeping the line map.
void log_offending_lines(const Preprocessed& pp, const std::string& info_log) {
    const std::vector<int> reported = parse_error_lines(info_log);
    bool quoted_anything = false;
    std::vector<int> seen;

    for (const int n : reported) {
        if (n < 1 || static_cast<std::size_t>(n) > pp.lines.size()) continue;

        bool duplicate = false;
        for (const int s : seen) {
            if (s == n) { duplicate = true; break; }
        }
        if (duplicate) continue;
        seen.push_back(n);

        const SourceLine& origin = pp.map[static_cast<std::size_t>(n) - 1];
        AP_ERROR("  >> %s:%d:  %s", pp.files[origin.file].c_str(), origin.line,
                 pp.lines[static_cast<std::size_t>(n) - 1].c_str());
        quoted_anything = true;
    }

    if (!quoted_anything) {
        AP_ERROR("  >> driver reported no line number this source could be mapped "
                 "to; the info log above is all there is");
    }
}

// Read an info log without guessing at a fixed buffer size. A long log from a
// shader with twenty errors truncated at 1024 bytes hides the first error,
// which is the only one that matters.
std::string read_info_log(GLuint object, bool is_program) {
    GLint log_len = 0;
    if (is_program) {
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_len);
    } else {
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_len);
    }
    if (log_len <= 1) return {};

    std::string info(static_cast<std::size_t>(log_len), '\0');
    GLsizei written = 0;
    if (is_program) {
        glGetProgramInfoLog(object, log_len, &written, info.data());
    } else {
        glGetShaderInfoLog(object, log_len, &written, info.data());
    }
    info.resize(static_cast<std::size_t>(written));
    return info;
}

GLuint compile_stage(GLenum type, const Preprocessed& pp, const char* stage_name,
                     const std::string& label) {
    const char* text = pp.text.c_str();
    const GLint length = static_cast<GLint>(pp.text.size());

    const GLuint shader = glCreateShader(type);
    if (shader == 0) {
        AP_ERROR("shader '%s': glCreateShader failed for the %s stage",
                 label.c_str(), stage_name);
        return 0;
    }
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return shader;

    const std::string info = read_info_log(shader, false);
    AP_ERROR("shader '%s': %s stage FAILED TO COMPILE", label.c_str(), stage_name);
    AP_ERROR("  driver said:\n%s", info.c_str());
    log_offending_lines(pp, info);

    glDeleteShader(shader);
    return 0;
}

// Compile both preprocessed stages and link them. Returns 0 on any failure,
// having already logged the driver's message and the offending source. Shared
// by both build paths so neither owns the other's error handling.
GLuint compile_and_link(const Preprocessed& vpp, const Preprocessed& fpp,
                        const std::string& label) {
    const GLuint vs = compile_stage(GL_VERTEX_SHADER, vpp, "vertex", label);
    if (vs == 0) return 0;
    const GLuint fs = compile_stage(GL_FRAGMENT_SHADER, fpp, "fragment", label);
    if (fs == 0) {
        glDeleteShader(vs);
        return 0;
    }

    const GLuint program = glCreateProgram();
    if (program == 0) {
        AP_ERROR("shader '%s': glCreateProgram failed", label.c_str());
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    // Flagged for deletion now; GL keeps each stage alive until the program
    // that owns it is deleted. Nothing else in the engine holds a stage id.
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok) return program;

    const std::string info = read_info_log(program, true);
    AP_ERROR("shader '%s': FAILED TO LINK", label.c_str());
    AP_ERROR("  driver said:\n%s", info.c_str());
    // A link failure is almost always a varying or interface mismatch, so quote
    // suspects from BOTH stages instead of guessing which side is at fault.
    log_offending_lines(vpp, info);
    log_offending_lines(fpp, info);

    glDeleteProgram(program);
    gl_state::on_program_deleted(program);
    return 0;
}

}  // namespace

Shader::~Shader() { destroy(); }

bool Shader::build(const std::string& vertex_src, const std::string& fragment_src,
                   const std::string& label) {
    // Inline sources have no directory, so #include from one can only reach the
    // asset root. That is intentional: a shader built from a string literal has
    // no "next to me" to be relative to.
    const std::string dir = asset_path("shaders/");

    const Preprocessed vpp = preprocess(vertex_src, dir, label + " [vert]");
    if (!vpp.ok) return false;
    const Preprocessed fpp = preprocess(fragment_src, dir, label + " [frag]");
    if (!fpp.ok) return false;

    const GLuint program = compile_and_link(vpp, fpp, label);
    if (program == 0) return false;

    // The old program is replaced only once the new one has linked. Rebuilding
    // over a live Shader and failing must leave the previous, working program
    // intact — a failed reload that blanks the screen is a failed reload that
    // costs you the thing you were looking at.
    destroy();
    program_ = program;
    label_ = label;
    AP_DEBUG("shader '%s' linked (program %u)", label_.c_str(), program_);
    return true;
}

bool Shader::build_from_files(const std::string& vertex_rel,
                              const std::string& fragment_rel) {
    const std::string vpath = asset_path(vertex_rel);
    const std::string fpath = asset_path(fragment_rel);

    const std::string vsrc = read_file(vpath);
    if (vsrc.empty()) {
        AP_ERROR("shader: vertex source '%s' is missing or empty", vpath.c_str());
        return false;
    }
    const std::string fsrc = read_file(fpath);
    if (fsrc.empty()) {
        AP_ERROR("shader: fragment source '%s' is missing or empty", fpath.c_str());
        return false;
    }

    // Preprocess against the REAL paths, so #include resolves next to the file
    // that asked for it and every error message names a file on disk.
    const Preprocessed vpp = preprocess(vsrc, dir_of(vpath), vpath);
    if (!vpp.ok) return false;
    const Preprocessed fpp = preprocess(fsrc, dir_of(fpath), fpath);
    if (!fpp.ok) return false;

    const std::string label = vertex_rel + " + " + fragment_rel;
    const GLuint program = compile_and_link(vpp, fpp, label);
    if (program == 0) return false;

    destroy();
    program_ = program;
    label_ = label;
    AP_INFO("shader '%s' linked (program %u)", label_.c_str(), program_);
    return true;
}

void Shader::destroy() {
    uniform_cache_.clear();
    label_.clear();
    if (!program_) return;
    glDeleteProgram(program_);
    // Paired with the delete, always. See the warning in gl_state.h.
    gl_state::on_program_deleted(program_);
    program_ = 0;
}

void Shader::bind() const { gl_state::use_program(program_); }

GLint Shader::location(const char* name) const {
    if (!program_) return -1;
    const auto it = uniform_cache_.find(name);
    if (it != uniform_cache_.end()) return it->second;
    const GLint loc = glGetUniformLocation(program_, name);
    // -1 is cached on purpose. The driver has already said this name is not in
    // the program; asking again every frame is a string lookup inside the
    // driver for a guaranteed no.
    uniform_cache_.emplace(name, loc);
    return loc;
}

void Shader::set_int(const char* name, int v) const {
    const GLint l = location(name);
    if (l >= 0) glUniform1i(l, v);
}
void Shader::set_float(const char* name, float v) const {
    const GLint l = location(name);
    if (l >= 0) glUniform1f(l, v);
}
void Shader::set_vec2(const char* name, const glm::vec2& v) const {
    const GLint l = location(name);
    if (l >= 0) glUniform2fv(l, 1, &v[0]);
}
void Shader::set_vec3(const char* name, const glm::vec3& v) const {
    const GLint l = location(name);
    if (l >= 0) glUniform3fv(l, 1, &v[0]);
}
void Shader::set_vec4(const char* name, const glm::vec4& v) const {
    const GLint l = location(name);
    if (l >= 0) glUniform4fv(l, 1, &v[0]);
}
void Shader::set_mat3(const char* name, const glm::mat3& v) const {
    const GLint l = location(name);
    if (l >= 0) glUniformMatrix3fv(l, 1, GL_FALSE, &v[0][0]);
}
void Shader::set_mat4(const char* name, const glm::mat4& v) const {
    const GLint l = location(name);
    if (l >= 0) glUniformMatrix4fv(l, 1, GL_FALSE, &v[0][0]);
}

}  // namespace apricot
