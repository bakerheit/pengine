#include "world/model_registry.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/log.h"
#include "render/mesh.h"
#include "render/texture.h"
#include "world/model_def.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

namespace {

std::vector<std::string> split_ws(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) out.push_back(std::move(tok));
    return out;
}

bool parse_vec3(const std::string& s, glm::vec3& out) {
    int n = std::sscanf(s.c_str(), "%f,%f,%f", &out.x, &out.y, &out.z);
    return n == 3;
}

bool parse_vec2(const std::string& s, glm::vec2& out) {
    int n = std::sscanf(s.c_str(), "%f,%f", &out.x, &out.y);
    return n == 2;
}

std::string resolve_asset_path(const std::string& p) {
    if (p.rfind("proc:", 0) == 0) return p;
    if (!p.empty() && p[0] == '/') return p;
    return std::string(ASSETS_DIR) + "/" + p;
}

}  // namespace

bool ModelRegistry::load_ide(const std::filesystem::path& ide_path) {
    std::ifstream in(ide_path);
    if (!in) {
        PE_ERROR("ModelRegistry: failed to open IDE %s", ide_path.string().c_str());
        return false;
    }

    int line_no = 0;
    int loaded  = 0;
    std::string line;
    while (std::getline(in, line)) {
        ++line_no;
        // Strip inline comments.
        if (auto pos = line.find('#'); pos != std::string::npos) line.erase(pos);

        std::vector<std::string> tok = split_ws(line);
        if (tok.empty()) continue;

        if (tok.size() < 7) {
            PE_WARN("ModelRegistry: %s:%d: expected >=7 columns, got %zu",
                    ide_path.string().c_str(), line_no, tok.size());
            continue;
        }

        ModelDef def;
        try {
            def.id = static_cast<uint32_t>(std::stoul(tok[0]));
        } catch (...) {
            PE_WARN("ModelRegistry: %s:%d: bad id '%s'",
                    ide_path.string().c_str(), line_no, tok[0].c_str());
            continue;
        }
        def.name         = tok[1];
        def.mesh_path    = tok[2];
        def.texture_path = tok[3];
        try {
            def.draw_dist = std::stof(tok[4]);
        } catch (...) {
            PE_WARN("ModelRegistry: %s:%d: bad draw_dist '%s'",
                    ide_path.string().c_str(), line_no, tok[4].c_str());
            continue;
        }
        def.flags  = parse_model_flags(tok[5]);
        try {
            def.lod_id = static_cast<uint32_t>(std::stoul(tok[6]));
        } catch (...) {
            PE_WARN("ModelRegistry: %s:%d: bad lod_id '%s'",
                    ide_path.string().c_str(), line_no, tok[6].c_str());
            continue;
        }

        // Optional trailing key=value tokens.
        for (std::size_t i = 7; i < tok.size(); ++i) {
            const std::string& kv = tok[i];
            auto eq = kv.find('=');
            if (eq == std::string::npos) {
                PE_WARN("ModelRegistry: %s:%d: malformed kv '%s'",
                        ide_path.string().c_str(), line_no, kv.c_str());
                continue;
            }
            std::string k = kv.substr(0, eq);
            std::string v = kv.substr(eq + 1);
            if (k == "tint") {
                if (!parse_vec3(v, def.tint))
                    PE_WARN("ModelRegistry: %s:%d: bad tint '%s'",
                            ide_path.string().c_str(), line_no, v.c_str());
            } else if (k == "uv") {
                if (!parse_vec2(v, def.uv_scale))
                    PE_WARN("ModelRegistry: %s:%d: bad uv '%s'",
                            ide_path.string().c_str(), line_no, v.c_str());
            } else {
                PE_WARN("ModelRegistry: %s:%d: unknown key '%s'",
                        ide_path.string().c_str(), line_no, k.c_str());
            }
        }

        if (defs_.count(def.id)) {
            PE_WARN("ModelRegistry: %s:%d: duplicate id %u (overwriting)",
                    ide_path.string().c_str(), line_no, def.id);
        }
        defs_[def.id] = std::move(def);
        ++loaded;
    }

    PE_INFO("ModelRegistry: loaded %d defs from %s",
            loaded, ide_path.string().c_str());
    return loaded > 0;
}

bool ModelRegistry::resolve_assets() {
    bool ok = true;
    for (auto& [id, def] : defs_) {
        def.mesh    = fetch_mesh(def.mesh_path);
        def.texture = fetch_texture(def.texture_path);
        if (!def.mesh) {
            PE_ERROR("ModelRegistry: id %u (%s) failed to resolve mesh '%s'",
                     id, def.name.c_str(), def.mesh_path.c_str());
            ok = false;
            continue;
        }
        if (!def.texture) {
            PE_WARN("ModelRegistry: id %u (%s) has no texture; will use bound default",
                    id, def.name.c_str());
        }
        def.local_bounds.min = def.mesh->bounds_min();
        def.local_bounds.max = def.mesh->bounds_max();
    }
    return ok;
}

const ModelDef* ModelRegistry::get(uint32_t id) const {
    auto it = defs_.find(id);
    return it == defs_.end() ? nullptr : &it->second;
}

const Mesh* ModelRegistry::fetch_mesh(const std::string& path) {
    if (auto it = meshes_.find(path); it != meshes_.end()) return it->second.get();

    auto mesh = std::make_unique<Mesh>();

    if (path == "proc:cube") {
        std::vector<Vertex>   verts;
        std::vector<uint32_t> idxs;
        make_cube(verts, idxs, 0.5f);
        mesh->upload(verts, idxs);
    } else if (path.rfind("proc:", 0) == 0) {
        PE_ERROR("ModelRegistry: unknown procedural mesh '%s'", path.c_str());
        return nullptr;
    } else {
        if (!load_static_emesh(resolve_asset_path(path), *mesh)) {
            PE_ERROR("ModelRegistry: load_static_emesh failed for '%s'", path.c_str());
            return nullptr;
        }
    }

    const Mesh* raw = mesh.get();
    meshes_.emplace(path, std::move(mesh));
    return raw;
}

const Texture* ModelRegistry::fetch_texture(const std::string& path) {
    if (path.empty()) return nullptr;
    if (auto it = textures_.find(path); it != textures_.end()) return it->second.get();

    auto tex = std::make_unique<Texture>();

    if      (path == "proc:checker")  tex->load_checkerboard(128);
    else if (path == "proc:asphalt")  tex->load_asphalt();
    else if (path == "proc:grass")    tex->load_grass();
    else if (path == "proc:facade")   tex->load_facade();
    else if (path == "proc:sidewalk") tex->load_sidewalk();
    else if (path.rfind("proc:", 0) == 0) {
        PE_ERROR("ModelRegistry: unknown procedural texture '%s'", path.c_str());
        return nullptr;
    } else {
        if (!tex->load_file(resolve_asset_path(path))) {
            PE_ERROR("ModelRegistry: texture load failed for '%s'", path.c_str());
            return nullptr;
        }
    }

    const Texture* raw = tex.get();
    textures_.emplace(path, std::move(tex));
    return raw;
}

// ---------------------------------------------------------------------------
// Flag parsing
// ---------------------------------------------------------------------------

namespace {
struct FlagEntry { const char* name; uint32_t bit; };
constexpr FlagEntry kFlagTable[] = {
    {"NONE",   ModelFlag::None},
    {"BLDG",   ModelFlag::Building},
    {"ROAD",   ModelFlag::Road},
    {"WALK",   ModelFlag::Walk},
    {"GROUND", ModelFlag::Ground},
    {"TREE",   ModelFlag::Tree},
    {"LOD",    ModelFlag::Lod},
    {"PROP",   ModelFlag::Prop},
};
}  // namespace

uint32_t parse_model_flags(const std::string& s) {
    uint32_t bits = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t j = s.find('|', i);
        std::string tok = s.substr(i, j == std::string::npos ? std::string::npos : j - i);
        bool found = false;
        for (const auto& e : kFlagTable) {
            if (tok == e.name) { bits |= e.bit; found = true; break; }
        }
        if (!found && !tok.empty())
            PE_WARN("ModelRegistry: unknown flag '%s'", tok.c_str());
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return bits;
}

const char* format_model_flags(uint32_t flags) {
    thread_local char buf[128];
    if (flags == 0) { std::strncpy(buf, "NONE", sizeof(buf)); return buf; }
    buf[0] = '\0';
    bool first = true;
    for (const auto& e : kFlagTable) {
        if (e.bit == 0) continue;
        if (flags & e.bit) {
            if (!first) std::strncat(buf, "|", sizeof(buf) - std::strlen(buf) - 1);
            std::strncat(buf, e.name, sizeof(buf) - std::strlen(buf) - 1);
            first = false;
        }
    }
    return buf;
}

}  // namespace pengine
