#include "core/emesh_reader.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include "core/log.h"

namespace apricot {
namespace {

bool read_exact(std::FILE* file, void* dst, std::size_t bytes) {
    return bytes == 0u || std::fread(dst, 1u, bytes, file) == bytes;
}

bool finite_vertex(const EmeshVertex& v) {
    const float values[] = {v.px, v.py, v.pz, v.nx, v.ny, v.nz,
                            v.u,  v.v,  v.tx, v.ty, v.tz, v.tw};
    for (const float value : values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

bool finite_vertex(const EmeshSkinnedVertex& v) {
    const float values[] = {v.px, v.py, v.pz, v.nx, v.ny, v.nz,
                            v.u,  v.v,  v.tx, v.ty, v.tz, v.tw,
                            v.bone_weight[0], v.bone_weight[1],
                            v.bone_weight[2], v.bone_weight[3]};
    for (const float value : values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

bool checked_add(uint64_t& total, uint64_t count, uint64_t size) {
    if (count != 0u && size > std::numeric_limits<uint64_t>::max() / count) {
        return false;
    }
    const uint64_t bytes = count * size;
    if (total > std::numeric_limits<uint64_t>::max() - bytes) return false;
    total += bytes;
    return true;
}

}  // namespace

bool read_static_emesh(const std::string& path, StaticEmesh& out) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        AP_ERROR("emesh: cannot open '%s'", path.c_str());
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        AP_ERROR("emesh: cannot size '%s'", path.c_str());
        std::fclose(file);
        return false;
    }
    const long file_size_long = std::ftell(file);
    if (file_size_long < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        AP_ERROR("emesh: cannot seek '%s'", path.c_str());
        std::fclose(file);
        return false;
    }
    const uint64_t file_size = static_cast<uint64_t>(file_size_long);

    EmeshHeader header{};
    if (!read_exact(file, &header, sizeof(header))) {
        AP_ERROR("emesh: truncated header in '%s'", path.c_str());
        std::fclose(file);
        return false;
    }
    if (header.magic != EMESH_MAGIC || header.version != EMESH_VERSION) {
        AP_ERROR("emesh: bad magic/version in '%s'", path.c_str());
        std::fclose(file);
        return false;
    }
    if ((header.flags & EMESH_FLAG_SKINNED) != 0u) {
        AP_ERROR("emesh: '%s' is skinned; static loader refused it", path.c_str());
        std::fclose(file);
        return false;
    }
    if (header.flags != 0u || header.vertex_count == 0u ||
        header.index_count == 0u || header.submesh_count == 0u ||
        header.index_count % 3u != 0u) {
        AP_ERROR("emesh: invalid static counts/flags in '%s'", path.c_str());
        std::fclose(file);
        return false;
    }

    uint64_t expected = sizeof(EmeshHeader);
    const bool size_ok =
        checked_add(expected, header.vertex_count, sizeof(EmeshVertex)) &&
        checked_add(expected, header.index_count, sizeof(uint32_t)) &&
        checked_add(expected, header.submesh_count, sizeof(EmeshSubmesh)) &&
        checked_add(expected, header.string_block_size, sizeof(char));
    if (!size_ok || expected != file_size) {
        AP_ERROR("emesh: '%s' size is %llu bytes; header describes %llu",
                 path.c_str(), static_cast<unsigned long long>(file_size),
                 static_cast<unsigned long long>(expected));
        std::fclose(file);
        return false;
    }

    StaticEmesh loaded;
    loaded.vertices.resize(header.vertex_count);
    loaded.indices.resize(header.index_count);
    std::vector<EmeshSubmesh> submeshes(header.submesh_count);
    std::vector<char> strings(header.string_block_size);
    const bool read_ok =
        read_exact(file, loaded.vertices.data(),
                   loaded.vertices.size() * sizeof(EmeshVertex)) &&
        read_exact(file, loaded.indices.data(),
                   loaded.indices.size() * sizeof(uint32_t)) &&
        read_exact(file, submeshes.data(),
                   submeshes.size() * sizeof(EmeshSubmesh)) &&
        read_exact(file, strings.data(), strings.size());
    std::fclose(file);
    if (!read_ok) {
        AP_ERROR("emesh: truncated payload in '%s'", path.c_str());
        return false;
    }

    for (std::size_t i = 0; i < loaded.vertices.size(); ++i) {
        const EmeshVertex& v = loaded.vertices[i];
        if (!finite_vertex(v)) {
            AP_ERROR("emesh: non-finite vertex %zu in '%s'", i, path.c_str());
            return false;
        }
        loaded.bounds.expand(glm::vec3{v.px, v.py, v.pz});
    }
    for (std::size_t i = 0; i < loaded.indices.size(); ++i) {
        if (loaded.indices[i] >= loaded.vertices.size()) {
            AP_ERROR("emesh: index %zu is out of range in '%s'", i, path.c_str());
            return false;
        }
    }

    for (std::size_t i = 0; i < submeshes.size(); ++i) {
        const EmeshSubmesh& sub = submeshes[i];
        const uint64_t end = static_cast<uint64_t>(sub.index_offset) +
                             static_cast<uint64_t>(sub.index_count);
        if (sub.index_count == 0u || sub.index_count % 3u != 0u ||
            end > loaded.indices.size() ||
            sub.material_name_offset >= strings.size()) {
            AP_ERROR("emesh: invalid submesh %zu in '%s'", i, path.c_str());
            return false;
        }
        const char* name = strings.data() + sub.material_name_offset;
        const std::size_t remaining = strings.size() - sub.material_name_offset;
        if (std::memchr(name, '\0', remaining) == nullptr) {
            AP_ERROR("emesh: unterminated material name in '%s'", path.c_str());
            return false;
        }
    }

    AP_INFO("emesh: loaded '%s' (%u vertices, %u indices, %u submeshes)",
            path.c_str(), header.vertex_count, header.index_count,
            header.submesh_count);
    out = std::move(loaded);
    return true;
}

bool read_skinned_emesh(const std::string& path, SkinnedEmesh& out) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        AP_ERROR("emesh: cannot open '%s'", path.c_str());
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0) {
        AP_ERROR("emesh: cannot size '%s'", path.c_str());
        std::fclose(file);
        return false;
    }
    const long file_size_long = std::ftell(file);
    if (file_size_long < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        AP_ERROR("emesh: cannot seek '%s'", path.c_str());
        std::fclose(file);
        return false;
    }
    const uint64_t file_size = static_cast<uint64_t>(file_size_long);

    EmeshHeader header{};
    if (!read_exact(file, &header, sizeof(header)) ||
        header.magic != EMESH_MAGIC || header.version != EMESH_VERSION) {
        AP_ERROR("emesh: bad or truncated header in '%s'", path.c_str());
        std::fclose(file);
        return false;
    }
    if (header.flags != EMESH_FLAG_SKINNED || header.vertex_count == 0u ||
        header.index_count == 0u || header.submesh_count == 0u ||
        header.index_count % 3u != 0u) {
        AP_ERROR("emesh: invalid skinned counts/flags in '%s'", path.c_str());
        std::fclose(file);
        return false;
    }

    uint64_t expected = sizeof(EmeshHeader);
    const bool size_ok =
        checked_add(expected, header.vertex_count,
                    sizeof(EmeshSkinnedVertex)) &&
        checked_add(expected, header.index_count, sizeof(uint32_t)) &&
        checked_add(expected, header.submesh_count, sizeof(EmeshSubmesh)) &&
        checked_add(expected, header.string_block_size, sizeof(char));
    if (!size_ok || expected != file_size) {
        AP_ERROR("emesh: '%s' size is %llu bytes; header describes %llu",
                 path.c_str(), static_cast<unsigned long long>(file_size),
                 static_cast<unsigned long long>(expected));
        std::fclose(file);
        return false;
    }

    SkinnedEmesh loaded;
    loaded.vertices.resize(header.vertex_count);
    loaded.indices.resize(header.index_count);
    std::vector<EmeshSubmesh> submeshes(header.submesh_count);
    std::vector<char> strings(header.string_block_size);
    const bool read_ok =
        read_exact(file, loaded.vertices.data(),
                   loaded.vertices.size() * sizeof(EmeshSkinnedVertex)) &&
        read_exact(file, loaded.indices.data(),
                   loaded.indices.size() * sizeof(uint32_t)) &&
        read_exact(file, submeshes.data(),
                   submeshes.size() * sizeof(EmeshSubmesh)) &&
        read_exact(file, strings.data(), strings.size());
    std::fclose(file);
    if (!read_ok) {
        AP_ERROR("emesh: truncated payload in '%s'", path.c_str());
        return false;
    }

    for (std::size_t i = 0; i < loaded.vertices.size(); ++i) {
        const EmeshSkinnedVertex& v = loaded.vertices[i];
        if (!finite_vertex(v)) {
            AP_ERROR("emesh: non-finite skinned vertex %zu in '%s'", i,
                     path.c_str());
            return false;
        }
        float weight_sum = 0.0f;
        for (int influence = 0; influence < 4; ++influence) {
            if (v.bone_weight[influence] < 0.0f) {
                AP_ERROR("emesh: negative bone weight at vertex %zu in '%s'",
                         i, path.c_str());
                return false;
            }
            weight_sum += v.bone_weight[influence];
        }
        if (std::fabs(weight_sum - 1.0f) > 0.002f) {
            AP_ERROR("emesh: bone weights sum to %.5f at vertex %zu in '%s'",
                     static_cast<double>(weight_sum), i, path.c_str());
            return false;
        }
        loaded.bounds.expand(glm::vec3{v.px, v.py, v.pz});
    }
    for (std::size_t i = 0; i < loaded.indices.size(); ++i) {
        if (loaded.indices[i] >= loaded.vertices.size()) {
            AP_ERROR("emesh: index %zu is out of range in '%s'", i,
                     path.c_str());
            return false;
        }
    }
    for (std::size_t i = 0; i < submeshes.size(); ++i) {
        const EmeshSubmesh& sub = submeshes[i];
        const uint64_t end = static_cast<uint64_t>(sub.index_offset) +
                             static_cast<uint64_t>(sub.index_count);
        if (sub.index_count == 0u || sub.index_count % 3u != 0u ||
            end > loaded.indices.size() ||
            sub.material_name_offset >= strings.size()) {
            AP_ERROR("emesh: invalid submesh %zu in '%s'", i, path.c_str());
            return false;
        }
        const char* name = strings.data() + sub.material_name_offset;
        const std::size_t remaining = strings.size() - sub.material_name_offset;
        if (std::memchr(name, '\0', remaining) == nullptr) {
            AP_ERROR("emesh: unterminated material name in '%s'", path.c_str());
            return false;
        }
    }

    AP_INFO("emesh: loaded rigged '%s' (%u vertices, %u indices, %u submeshes)",
            path.c_str(), header.vertex_count, header.index_count,
            header.submesh_count);
    out = std::move(loaded);
    return true;
}

}  // namespace apricot
