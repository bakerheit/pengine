#include "core/skeletal_animation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include "core/anim_format.h"
#include "core/log.h"
#include "core/skeleton_format.h"

namespace apricot {
namespace {

bool read_exact(std::FILE* file, void* destination, std::size_t bytes) {
    return bytes == 0u || std::fread(destination, 1u, bytes, file) == bytes;
}

template <typename Key>
std::size_t upper_key(const std::vector<Key>& keys, float time) {
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].time > time) return i;
    }
    return keys.size();
}

template <typename Key>
glm::vec3 sample_vector(const std::vector<Key>& keys, float time,
                        const glm::vec3& fallback) {
    if (keys.empty()) return fallback;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    const std::size_t hi = upper_key(keys, time);
    const std::size_t lo = hi - 1u;
    const float span = keys[hi].time - keys[lo].time;
    const float alpha = span > 1e-6f ? (time - keys[lo].time) / span : 0.0f;
    return glm::mix(keys[lo].value, keys[hi].value, alpha);
}

glm::quat sample_rotation(const std::vector<Animation::RotKey>& keys,
                          float time, const glm::quat& fallback) {
    if (keys.empty()) return fallback;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    const std::size_t hi = upper_key(keys, time);
    const std::size_t lo = hi - 1u;
    const float span = keys[hi].time - keys[lo].time;
    const float alpha = span > 1e-6f ? (time - keys[lo].time) / span : 0.0f;
    return glm::slerp(keys[lo].value, keys[hi].value, alpha);
}

glm::vec3 rotate_by_quaternion(glm::vec4 q, const glm::vec3& value) {
    return value + 2.0f * glm::cross(
        glm::vec3{q}, glm::cross(glm::vec3{q}, value) + q.w * value);
}

glm::vec3 dqs_position(const EmeshSkinnedVertex& vertex,
                       const std::vector<glm::vec4>& real,
                       const std::vector<glm::vec4>& dual) {
    const glm::vec4 reference = real[vertex.bone_idx[0]];
    glm::vec4 blended_real = vertex.bone_weight[0] * reference;
    glm::vec4 blended_dual =
        vertex.bone_weight[0] * dual[vertex.bone_idx[0]];
    for (int influence = 1; influence < 4; ++influence) {
        const std::size_t bone = vertex.bone_idx[influence];
        const glm::vec4 candidate = real[bone];
        const float signed_weight = glm::dot(reference, candidate) < 0.0f
            ? -vertex.bone_weight[influence]
            : vertex.bone_weight[influence];
        blended_real += signed_weight * candidate;
        blended_dual += signed_weight * dual[bone];
    }
    float magnitude = glm::length(blended_real);
    if (magnitude < 1e-8f) {
        blended_real = {0.0f, 0.0f, 0.0f, 1.0f};
        blended_dual = glm::vec4{0.0f};
        magnitude = 1.0f;
    }
    blended_real /= magnitude;
    blended_dual /= magnitude;
    const glm::vec3 translation = 2.0f *
        (blended_real.w * glm::vec3{blended_dual} -
         blended_dual.w * glm::vec3{blended_real} +
         glm::cross(glm::vec3{blended_real}, glm::vec3{blended_dual}));
    return rotate_by_quaternion(
               blended_real, {vertex.px, vertex.py, vertex.pz}) +
           translation;
}

}  // namespace

bool Skeleton::load(const std::string& path) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        AP_ERROR("skeleton: cannot open '%s'", path.c_str());
        return false;
    }
    EskelHeader header{};
    if (!read_exact(file, &header, sizeof(header)) ||
        header.magic != ESKEL_MAGIC || header.version != ESKEL_VERSION ||
        header.bone_count == 0u || header.bone_count > kMaxSkinBones ||
        header.string_block_size == 0u) {
        AP_ERROR("skeleton: invalid header in '%s'", path.c_str());
        std::fclose(file);
        return false;
    }

    std::vector<EskelBone> raw(header.bone_count);
    std::vector<char> strings(header.string_block_size);
    const bool ok = read_exact(file, raw.data(), raw.size() * sizeof(EskelBone)) &&
                    read_exact(file, strings.data(), strings.size());
    const int trailing = std::fgetc(file);
    std::fclose(file);
    if (!ok || trailing != EOF) {
        AP_ERROR("skeleton: truncated or oversized payload in '%s'", path.c_str());
        return false;
    }

    std::vector<Bone> loaded(header.bone_count);
    for (std::size_t i = 0; i < loaded.size(); ++i) {
        const EskelBone& source = raw[i];
        if (source.parent >= static_cast<int32_t>(loaded.size()) ||
            source.parent == static_cast<int32_t>(i) || source.parent < -1 ||
            source.name_offset >= strings.size() ||
            std::memchr(strings.data() + source.name_offset, '\0',
                        strings.size() - source.name_offset) == nullptr) {
            AP_ERROR("skeleton: invalid bone %zu in '%s'", i, path.c_str());
            return false;
        }
        Bone& destination = loaded[i];
        destination.parent = source.parent;
        destination.name = strings.data() + source.name_offset;
        std::memcpy(&destination.inverse_bind[0][0], source.inv_bind,
                    16u * sizeof(float));
        std::memcpy(&destination.bind_local[0][0], source.bind_local,
                    16u * sizeof(float));
        for (int element = 0; element < 16; ++element) {
            if (!std::isfinite(source.inv_bind[element]) ||
                !std::isfinite(source.bind_local[element])) {
                AP_ERROR("skeleton: non-finite matrix on bone %zu in '%s'", i,
                         path.c_str());
                return false;
            }
        }
    }
    bones_ = std::move(loaded);
    AP_INFO("skeleton: loaded '%s' (%u bones)", path.c_str(), header.bone_count);
    return true;
}

int Skeleton::find_bone(const std::string& name) const {
    for (std::size_t i = 0; i < bones_.size(); ++i) {
        if (bones_[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

bool Skeleton::accepts(const SkinnedEmesh& mesh) const {
    if (bones_.empty()) return false;
    for (const EmeshSkinnedVertex& vertex : mesh.vertices) {
        for (int influence = 0; influence < 4; ++influence) {
            if (vertex.bone_weight[influence] > 0.0f &&
                vertex.bone_idx[influence] >= bones_.size()) {
                return false;
            }
        }
    }
    return true;
}

void Skeleton::compute_skin_matrices(
    const std::vector<glm::mat4>& local_poses,
    std::vector<glm::mat4>& out_skin) const {
    const std::size_t count = bones_.size();
    if (local_poses.size() != count) {
        out_skin.clear();
        return;
    }
    std::vector<glm::mat4> world(count);
    std::vector<bool> complete(count, false);
    std::size_t completed = 0;
    bool progressed = true;
    while (completed < count && progressed) {
        progressed = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (complete[i]) continue;
            const int32_t parent = bones_[i].parent;
            if (parent < 0) {
                world[i] = local_poses[i];
            } else if (complete[static_cast<std::size_t>(parent)]) {
                world[i] = world[static_cast<std::size_t>(parent)] *
                           local_poses[i];
            } else {
                continue;
            }
            complete[i] = true;
            ++completed;
            progressed = true;
        }
    }
    if (completed != count) {
        AP_ERROR("skeleton: hierarchy contains a parent cycle");
        out_skin.clear();
        return;
    }
    out_skin.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        out_skin[i] = world[i] * bones_[i].inverse_bind;
    }
}

bool Animation::load(const std::string& path, const Skeleton& skeleton) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        AP_ERROR("animation: cannot open '%s'", path.c_str());
        return false;
    }
    EanimHeader header{};
    if (!read_exact(file, &header, sizeof(header)) ||
        header.magic != EANIM_MAGIC || header.version != EANIM_VERSION ||
        header.channel_count == 0u || header.string_block_size == 0u ||
        !std::isfinite(header.duration) || header.duration <= 0.0f) {
        AP_ERROR("animation: invalid header in '%s'", path.c_str());
        std::fclose(file);
        return false;
    }

    std::vector<EanimChannel> raw(header.channel_count);
    if (!read_exact(file, raw.data(), raw.size() * sizeof(EanimChannel))) {
        AP_ERROR("animation: truncated channels in '%s'", path.c_str());
        std::fclose(file);
        return false;
    }
    std::vector<Channel> loaded(header.channel_count);
    for (std::size_t i = 0; i < loaded.size(); ++i) {
        const EanimChannel& source = raw[i];
        Channel& destination = loaded[i];
        destination.position.resize(source.pos_key_count);
        destination.rotation.resize(source.rot_key_count);
        destination.scale.resize(source.scale_key_count);
        for (PosKey& key : destination.position) {
            EanimVec3Key input{};
            if (!read_exact(file, &input, sizeof(input))) {
                std::fclose(file);
                AP_ERROR("animation: truncated position keys in '%s'", path.c_str());
                return false;
            }
            key = {input.time, {input.x, input.y, input.z}};
        }
        for (RotKey& key : destination.rotation) {
            EanimQuatKey input{};
            if (!read_exact(file, &input, sizeof(input))) {
                std::fclose(file);
                AP_ERROR("animation: truncated rotation keys in '%s'", path.c_str());
                return false;
            }
            key = {input.time, glm::normalize(
                glm::quat{input.w, input.x, input.y, input.z})};
        }
        for (ScaleKey& key : destination.scale) {
            EanimVec3Key input{};
            if (!read_exact(file, &input, sizeof(input))) {
                std::fclose(file);
                AP_ERROR("animation: truncated scale keys in '%s'", path.c_str());
                return false;
            }
            key = {input.time, {input.x, input.y, input.z}};
        }
    }
    std::vector<char> strings(header.string_block_size);
    const bool strings_ok = read_exact(file, strings.data(), strings.size());
    const int trailing = std::fgetc(file);
    std::fclose(file);
    if (!strings_ok || trailing != EOF) {
        AP_ERROR("animation: truncated or oversized strings in '%s'", path.c_str());
        return false;
    }

    int unresolved = 0;
    for (std::size_t i = 0; i < loaded.size(); ++i) {
        if (raw[i].bone_name_offset >= strings.size() ||
            std::memchr(strings.data() + raw[i].bone_name_offset, '\0',
                        strings.size() - raw[i].bone_name_offset) == nullptr) {
            AP_ERROR("animation: invalid channel name in '%s'", path.c_str());
            return false;
        }
        loaded[i].bone_name = strings.data() + raw[i].bone_name_offset;
        loaded[i].bone_index = skeleton.find_bone(loaded[i].bone_name);
        if (loaded[i].bone_index < 0) ++unresolved;
    }
    channels_ = std::move(loaded);
    duration_ = header.duration;
    unresolved_channels_ = unresolved;
    AP_INFO("animation: loaded '%s' (%u channels, %.3f s, %d unresolved)",
            path.c_str(), header.channel_count,
            static_cast<double>(duration_), unresolved);
    return true;
}

void Animation::sample(float time, const Skeleton& skeleton,
                       std::vector<glm::mat4>& out_local) const {
    const int count = skeleton.bone_count();
    out_local.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        out_local[static_cast<std::size_t>(i)] = skeleton.bone(i).bind_local;
    }
    if (duration_ > 0.0f) {
        time = std::fmod(time, duration_);
        if (time < 0.0f) time += duration_;
    }
    for (const Channel& channel : channels_) {
        if (channel.bone_index < 0) continue;
        const glm::vec3 translation = sample_vector(
            channel.position, time, glm::vec3{0.0f});
        const glm::quat rotation = sample_rotation(
            channel.rotation, time, glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
        const glm::vec3 scale = sample_vector(
            channel.scale, time, glm::vec3{1.0f});
        out_local[static_cast<std::size_t>(channel.bone_index)] =
            glm::translate(glm::mat4{1.0f}, translation) *
            glm::mat4_cast(rotation) * glm::scale(glm::mat4{1.0f}, scale);
    }
}

void strip_root_motion_xz(const Skeleton& skeleton,
                          std::vector<glm::mat4>& local_poses) {
    for (int i = 0; i < skeleton.bone_count(); ++i) {
        if (skeleton.bone(i).parent >= 0) continue;
        glm::vec4& translation = local_poses[static_cast<std::size_t>(i)][3];
        const glm::vec3 bind_translation{skeleton.bone(i).bind_local[3]};
        translation.x = bind_translation.x;
        translation.z = bind_translation.z;
        translation.w = 1.0f;
    }
}

void skin_matrices_to_dual_quaternions(
    const std::vector<glm::mat4>& skin,
    std::vector<glm::vec4>& out_real,
    std::vector<glm::vec4>& out_dual) {
    out_real.resize(skin.size());
    out_dual.resize(skin.size());
    for (std::size_t i = 0; i < skin.size(); ++i) {
        const glm::quat rotation = glm::normalize(
            glm::quat_cast(glm::mat3(skin[i])));
        const glm::vec3 translation{skin[i][3]};
        const glm::quat dual = 0.5f *
            (glm::quat{0.0f, translation.x, translation.y, translation.z} *
             rotation);
        out_real[i] = {rotation.x, rotation.y, rotation.z, rotation.w};
        out_dual[i] = {dual.x, dual.y, dual.z, dual.w};
    }
}

glm::vec3 skinned_vertex_position(
    const EmeshSkinnedVertex& vertex,
    const std::vector<glm::vec4>& real,
    const std::vector<glm::vec4>& dual) {
    return dqs_position(vertex, real, dual);
}

float locomotion_plant_offset(const SkinnedEmesh& mesh,
                              const Skeleton& skeleton,
                              const Animation& animation,
                              float model_scale, int phase_samples) {
    if (mesh.vertices.empty() || animation.duration() <= 0.0f ||
        phase_samples <= 0) {
        return 0.0f;
    }
    std::vector<glm::mat4> local;
    std::vector<glm::mat4> skin;
    std::vector<glm::vec4> real;
    std::vector<glm::vec4> dual;
    float animated_min = std::numeric_limits<float>::max();
    for (int sample = 0; sample < phase_samples; ++sample) {
        const float time = animation.duration() *
            static_cast<float>(sample) / static_cast<float>(phase_samples);
        animation.sample(time, skeleton, local);
        strip_root_motion_xz(skeleton, local);
        skeleton.compute_skin_matrices(local, skin);
        skin_matrices_to_dual_quaternions(skin, real, dual);
        for (const EmeshSkinnedVertex& vertex : mesh.vertices) {
            animated_min = std::min(animated_min,
                                    dqs_position(vertex, real, dual).y);
        }
    }
    if (!std::isfinite(animated_min)) return 0.0f;
    return std::max(0.0f, (mesh.bounds.min.y - animated_min) * model_scale);
}

}  // namespace apricot
