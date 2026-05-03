#include "world/ipl_loader.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/log.h"

namespace pengine {

namespace {

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(std::move(tok));
    return out;
}

bool parse_uv(const std::string& v, glm::vec2& out) {
    return std::sscanf(v.c_str(), "%f,%f", &out.x, &out.y) == 2;
}

}  // namespace

bool load_ipl(const std::filesystem::path& path,
              std::vector<InstanceDef>& out) {
    std::ifstream in(path);
    if (!in) return false;

    out.clear();
    int line_no = 0;
    int loaded  = 0;
    std::string line;
    while (std::getline(in, line)) {
        ++line_no;
        if (auto pos = line.find('#'); pos != std::string::npos) line.erase(pos);
        std::vector<std::string> tok = split_ws(line);
        if (tok.empty()) continue;

        if (tok.size() < 11) {
            PE_WARN("IPL %s:%d: expected >=11 numeric tokens, got %zu",
                    path.string().c_str(), line_no, tok.size());
            continue;
        }

        InstanceDef inst;
        try {
            inst.model_id            = static_cast<uint32_t>(std::stoul(tok[0]));
            inst.transform.position  = {std::stof(tok[1]), std::stof(tok[2]), std::stof(tok[3])};
            // glm::quat ctor is (w, x, y, z); file stores xyzw.
            inst.transform.rotation  = glm::quat(std::stof(tok[7]),
                                                  std::stof(tok[4]),
                                                  std::stof(tok[5]),
                                                  std::stof(tok[6]));
            inst.transform.scale     = {std::stof(tok[8]), std::stof(tok[9]), std::stof(tok[10])};
        } catch (...) {
            PE_WARN("IPL %s:%d: bad numeric token", path.string().c_str(), line_no);
            continue;
        }

        for (std::size_t i = 11; i < tok.size(); ++i) {
            const std::string& kv = tok[i];
            auto eq = kv.find('=');
            if (eq == std::string::npos) continue;
            std::string k = kv.substr(0, eq);
            std::string v = kv.substr(eq + 1);
            if (k == "uv") {
                if (!parse_uv(v, inst.uv_scale_override))
                    PE_WARN("IPL %s:%d: bad uv '%s'",
                            path.string().c_str(), line_no, v.c_str());
            } else if (k == "lod") {
                try { inst.lod_pair = static_cast<uint32_t>(std::stoul(v)); }
                catch (...) {}
            }
        }

        out.push_back(inst);
        ++loaded;
    }

    PE_DEBUG("IPL: loaded %d instances from %s", loaded, path.string().c_str());
    return true;
}

bool save_ipl(const std::filesystem::path& path,
              const std::vector<InstanceDef>& instances) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    // Failure to create the directory isn't fatal here; the open below will
    // fail-and-warn with a clearer message.

    std::ofstream out(path);
    if (!out) {
        PE_WARN("IPL: failed to open '%s' for writing", path.string().c_str());
        return false;
    }

    out << "# Auto-generated cell IPL. model px py pz qx qy qz qw sx sy sz [kv...]\n";
    for (const InstanceDef& i : instances) {
        const auto& p = i.transform.position;
        const auto& q = i.transform.rotation;
        const auto& s = i.transform.scale;
        char line[256];
        std::snprintf(line, sizeof(line),
                      "%u %.4f %.4f %.4f  %.6f %.6f %.6f %.6f  %.4f %.4f %.4f",
                      i.model_id,
                      p.x, p.y, p.z,
                      q.x, q.y, q.z, q.w,
                      s.x, s.y, s.z);
        out << line;
        if (i.uv_scale_override.x > 0.f || i.uv_scale_override.y > 0.f) {
            char uv[64];
            std::snprintf(uv, sizeof(uv), "  uv=%.4f,%.4f",
                          i.uv_scale_override.x, i.uv_scale_override.y);
            out << uv;
        }
        out << '\n';
    }
    return static_cast<bool>(out);
}

std::filesystem::path ipl_path_for_cell(const std::filesystem::path& base,
                                         CellCoord coord) {
    char name[64];
    std::snprintf(name, sizeof(name), "cell_%d_%d.ipl", coord.x, coord.z);
    return base / name;
}

}  // namespace pengine
