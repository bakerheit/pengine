#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/emesh_reader.h"

namespace apricot {

inline constexpr int kMaxSkinBones = 64;

struct Bone {
    int32_t parent = -1;
    std::string name;
    glm::mat4 inverse_bind{1.0f};
    glm::mat4 bind_local{1.0f};
};

class Skeleton {
public:
    bool load(const std::string& path);

    int bone_count() const { return static_cast<int>(bones_.size()); }
    const Bone& bone(int index) const {
        return bones_[static_cast<std::size_t>(index)];
    }
    int find_bone(const std::string& name) const;

    bool accepts(const SkinnedEmesh& mesh) const;
    void compute_skin_matrices(const std::vector<glm::mat4>& local_poses,
                               std::vector<glm::mat4>& out_skin) const;

private:
    std::vector<Bone> bones_;
};

class Animation {
public:
    struct PosKey { float time = 0.0f; glm::vec3 value{}; };
    struct RotKey { float time = 0.0f; glm::quat value{}; };
    struct ScaleKey { float time = 0.0f; glm::vec3 value{1.0f}; };

    bool load(const std::string& path, const Skeleton& skeleton);
    float duration() const { return duration_; }
    int unresolved_channels() const { return unresolved_channels_; }

    void sample(float time, const Skeleton& skeleton,
                std::vector<glm::mat4>& out_local) const;

private:
    struct Channel {
        std::string bone_name;
        int bone_index = -1;
        std::vector<PosKey> position;
        std::vector<RotKey> rotation;
        std::vector<ScaleKey> scale;
    };

    std::vector<Channel> channels_;
    float duration_ = 0.0f;
    int unresolved_channels_ = 0;
};

// Game movement owns horizontal translation. Preserve the authored root Y so
// crouch and hip motion survive, but prevent a walk clip from moving the whole
// character a second time.
void strip_root_motion_xz(const Skeleton& skeleton,
                          std::vector<glm::mat4>& local_poses);

void skin_matrices_to_dual_quaternions(
    const std::vector<glm::mat4>& skin,
    std::vector<glm::vec4>& out_real,
    std::vector<glm::vec4>& out_dual);

// CPU mirror of the production vertex shader. Useful for asset validation and
// measurements that must reflect the actual dual-quaternion result.
glm::vec3 skinned_vertex_position(
    const EmeshSkinnedVertex& vertex,
    const std::vector<glm::vec4>& real,
    const std::vector<glm::vec4>& dual);

// One constant lift per clip. It is calculated over the full interpolated
// animation, so feet stay planted without the per-frame bounding-box recenter
// that made the old eight-mesh bake visibly shake.
float locomotion_plant_offset(const SkinnedEmesh& mesh,
                              const Skeleton& skeleton,
                              const Animation& animation,
                              float model_scale,
                              int phase_samples = 48);

}  // namespace apricot
