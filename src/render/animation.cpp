#include "render/animation.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "core/anim_format.h"
#include "core/log.h"
#include "render/skeleton.h"

namespace pengine {

namespace {

// Find the upper-bound key whose time > t. Linear scan is fine: typical
// channels have <200 keys and we only sample once per frame.
template <typename Key>
std::size_t upper_bound_key(const std::vector<Key>& keys, float t) {
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (keys[i].t > t) return i;
    return keys.size();
}

glm::vec3 sample_vec3(const std::vector<Animation::PosKey>& keys, float t,
                       const glm::vec3& fallback) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().t) return keys.front().v;
    if (t >= keys.back().t)  return keys.back().v;
    std::size_t hi = upper_bound_key(keys, t);
    std::size_t lo = hi - 1;
    float span = keys[hi].t - keys[lo].t;
    float u    = span > 1e-6f ? (t - keys[lo].t) / span : 0.f;
    return keys[lo].v + (keys[hi].v - keys[lo].v) * u;
}

glm::vec3 sample_vec3(const std::vector<Animation::SclKey>& keys, float t,
                       const glm::vec3& fallback) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().t) return keys.front().v;
    if (t >= keys.back().t)  return keys.back().v;
    std::size_t hi = upper_bound_key(keys, t);
    std::size_t lo = hi - 1;
    float span = keys[hi].t - keys[lo].t;
    float u    = span > 1e-6f ? (t - keys[lo].t) / span : 0.f;
    return keys[lo].v + (keys[hi].v - keys[lo].v) * u;
}

glm::quat sample_quat(const std::vector<Animation::RotKey>& keys, float t,
                       const glm::quat& fallback) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().t) return keys.front().q;
    if (t >= keys.back().t)  return keys.back().q;
    std::size_t hi = upper_bound_key(keys, t);
    std::size_t lo = hi - 1;
    float span = keys[hi].t - keys[lo].t;
    float u    = span > 1e-6f ? (t - keys[lo].t) / span : 0.f;
    return glm::slerp(keys[lo].q, keys[hi].q, u);
}

} // namespace

bool Animation::load(const std::string& path, const Skeleton& skel) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { PE_ERROR("Animation: cannot open %s", path.c_str()); return false; }

    EanimHeader hdr{};
    if (std::fread(&hdr, sizeof(hdr), 1, f) != 1) { std::fclose(f); return false; }
    if (hdr.magic != EANIM_MAGIC || hdr.version != EANIM_VERSION) {
        PE_ERROR("Animation: bad magic/version in %s", path.c_str());
        std::fclose(f); return false;
    }
    duration_ = hdr.duration;

    std::vector<EanimChannel> raw_chans(hdr.channel_count);
    if (std::fread(raw_chans.data(), sizeof(EanimChannel),
                    raw_chans.size(), f) != raw_chans.size()) {
        std::fclose(f); return false;
    }

    // Read keyframes for each channel, in declaration order.
    channels_.resize(hdr.channel_count);
    for (uint32_t ci = 0; ci < hdr.channel_count; ++ci) {
        Channel& c = channels_[ci];
        const EanimChannel& rc = raw_chans[ci];

        c.pos.resize(rc.pos_key_count);
        c.rot.resize(rc.rot_key_count);
        c.scl.resize(rc.scale_key_count);

        for (uint32_t k = 0; k < rc.pos_key_count; ++k) {
            EanimVec3Key vk;
            if (std::fread(&vk, sizeof(vk), 1, f) != 1) { std::fclose(f); return false; }
            c.pos[k] = {vk.time, glm::vec3{vk.x, vk.y, vk.z}};
        }
        for (uint32_t k = 0; k < rc.rot_key_count; ++k) {
            EanimQuatKey qk;
            if (std::fread(&qk, sizeof(qk), 1, f) != 1) { std::fclose(f); return false; }
            c.rot[k] = {qk.time, glm::quat{qk.w, qk.x, qk.y, qk.z}};
        }
        for (uint32_t k = 0; k < rc.scale_key_count; ++k) {
            EanimVec3Key vk;
            if (std::fread(&vk, sizeof(vk), 1, f) != 1) { std::fclose(f); return false; }
            c.scl[k] = {vk.time, glm::vec3{vk.x, vk.y, vk.z}};
        }
    }

    std::vector<char> strings(hdr.string_block_size);
    if (hdr.string_block_size > 0) {
        if (std::fread(strings.data(), 1, strings.size(), f) != strings.size()) {
            std::fclose(f); return false;
        }
    }
    std::fclose(f);

    // Resolve channel bone names against the skeleton.
    int unresolved = 0;
    for (uint32_t ci = 0; ci < hdr.channel_count; ++ci) {
        Channel& c = channels_[ci];
        if (raw_chans[ci].bone_name_offset < strings.size())
            c.bone_name = strings.data() + raw_chans[ci].bone_name_offset;
        c.bone_idx = skel.find_bone(c.bone_name);
        if (c.bone_idx < 0) ++unresolved;
    }
    PE_INFO("Animation loaded: %s  (%u channels, %.2fs, %d unresolved)",
            path.c_str(), hdr.channel_count, duration_, unresolved);
    return true;
}

void Animation::sample(float t, const Skeleton& skel,
                        std::vector<glm::mat4>& out_local) const {
    const int n = skel.bone_count();
    out_local.resize(static_cast<std::size_t>(n));

    // Default everyone to bind pose.
    for (int b = 0; b < n; ++b)
        out_local[static_cast<std::size_t>(b)] = skel.bone(b).bind_local;

    // Wrap t into [0, duration] (loop).
    if (duration_ > 0.f) {
        t = std::fmod(t, duration_);
        if (t < 0.f) t += duration_;
    }

    for (const Channel& c : channels_) {
        if (c.bone_idx < 0) continue;
        glm::vec3 tr = sample_vec3(c.pos, t, glm::vec3{0.f});
        glm::quat rt = sample_quat(c.rot, t, glm::quat{1.f, 0.f, 0.f, 0.f});
        glm::vec3 sc = sample_vec3(c.scl, t, glm::vec3{1.f});
        glm::mat4 m = glm::translate(glm::mat4{1.f}, tr)
                    * glm::mat4_cast(rt)
                    * glm::scale(glm::mat4{1.f}, sc);
        out_local[static_cast<std::size_t>(c.bone_idx)] = m;
    }
}

} // namespace pengine
