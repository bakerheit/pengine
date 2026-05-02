#include "render/skeleton.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "core/skeleton_format.h"

namespace pengine {

bool Skeleton::load(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { PE_ERROR("Skeleton: cannot open %s", path.c_str()); return false; }

    EskelHeader hdr{};
    if (std::fread(&hdr, sizeof(hdr), 1, f) != 1) { std::fclose(f); return false; }
    if (hdr.magic != ESKEL_MAGIC || hdr.version != ESKEL_VERSION) {
        PE_ERROR("Skeleton: bad magic/version in %s", path.c_str());
        std::fclose(f); return false;
    }

    std::vector<EskelBone> raw(hdr.bone_count);
    std::vector<char>      strings(hdr.string_block_size);
    bool ok =
        std::fread(raw.data(),     sizeof(EskelBone), raw.size(),     f) == raw.size() &&
        std::fread(strings.data(), 1, strings.size(), f) == strings.size();
    std::fclose(f);
    if (!ok) { PE_ERROR("Skeleton: truncated %s", path.c_str()); return false; }

    bones_.resize(hdr.bone_count);
    for (uint32_t bi = 0; bi < hdr.bone_count; ++bi) {
        Bone& b = bones_[bi];
        b.parent = raw[bi].parent;
        if (raw[bi].name_offset < strings.size())
            b.name = strings.data() + raw[bi].name_offset;
        std::memcpy(&b.inv_bind[0][0],   raw[bi].inv_bind,   16 * sizeof(float));
        std::memcpy(&b.bind_local[0][0], raw[bi].bind_local, 16 * sizeof(float));
    }
    PE_INFO("Skeleton loaded: %s  (%u bones)", path.c_str(), hdr.bone_count);
    for (uint32_t bi = 0; bi < hdr.bone_count; ++bi) {
        const Bone& b = bones_[bi];
        glm::vec3 t{b.bind_local[3]};
        PE_INFO("  bone[%u] '%s' parent=%d bind_t=(%.2f,%.2f,%.2f)",
                bi, b.name.c_str(), b.parent, t.x, t.y, t.z);
    }
    return true;
}

int Skeleton::find_bone(const std::string& nm) const {
    for (size_t i = 0; i < bones_.size(); ++i)
        if (bones_[i].name == nm) return static_cast<int>(i);
    return -1;
}

void Skeleton::compute_skin_matrices(const std::vector<glm::mat4>& local_poses,
                                      std::vector<glm::mat4>& out_skin) const {
    const size_t n = bones_.size();
    std::vector<glm::mat4> world(n);
    std::vector<bool>      done(n, false);

    // Tree topology: at most one pass per depth level. n^2 worst case is fine
    // for ~28 bones.
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (size_t b = 0; b < n; ++b) {
            if (done[b]) continue;
            int p = bones_[b].parent;
            if (p < 0) {
                world[b] = local_poses[b];
                done[b]  = true;
                progressed = true;
            } else if (done[static_cast<size_t>(p)]) {
                world[b] = world[static_cast<size_t>(p)] * local_poses[b];
                done[b]  = true;
                progressed = true;
            }
        }
    }

    out_skin.resize(n);
    for (size_t b = 0; b < n; ++b)
        out_skin[b] = world[b] * bones_[b].inv_bind;
}

} // namespace pengine
