#include "render/material.h"

#include <fstream>
#include <sstream>

#include "core/log.h"
#include "render/shader.h"

namespace pengine {

namespace {
std::string trim(const std::string& s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}
} // namespace

bool MaterialDef::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) { PE_ERROR("MaterialDef: cannot open %s", path.c_str()); return false; }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if      (key == "shader")   shader_name   = val;
        else if (key == "diffuse")  diffuse_path  = val;
        else if (key == "normal")   normal_path   = val;
        else if (key == "specular") specular_path = val;
    }
    return true;
}

bool Material::load(const std::string& mat_path, const std::string& assets_root) {
    if (!def.load(mat_path)) return false;

    auto load_tex = [&](const std::string& rel) -> std::shared_ptr<Texture> {
        if (rel.empty()) return nullptr;
        auto t = std::make_shared<Texture>();
        if (!t->load_file(assets_root + "/" + rel)) return nullptr;
        return t;
    };

    diffuse  = load_tex(def.diffuse_path);
    normal   = load_tex(def.normal_path);
    specular = load_tex(def.specular_path);
    return true;
}

void Material::bind(Shader& shader) const {
    if (diffuse)  { diffuse->bind(0);  shader.set("u_diffuse",  0); }
    if (normal)   { normal->bind(1);   shader.set("u_normal",   1); }
    if (specular) { specular->bind(2); shader.set("u_specular", 2); }
}

} // namespace pengine
