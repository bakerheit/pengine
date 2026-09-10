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

// A bone's local transform kept in its authored PARTS rather than composed.
//
// Sampling produces these because a crossfade has to interpolate the parts.
// Lerping two composed matrices element by element is not a rotation: halfway
// between two poses a limb shears and shrinks, and the shorter it gets the more
// the shoulder looks dislocated. Slerp the rotation, lerp the translation and
// the scale, THEN compose. The composition order is the same one Animation
// already used: translate * rotate * scale.
struct BonePose {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

glm::mat4 bone_pose_matrix(const BonePose& pose);

// The inverse. Exported for the same reason blend_bone_poses() is: anything
// that has to CROSSFADE a pose needs it in parts, and a pose that arrives as
// matrices — from a solver, from a procedural rig — has no other way back.
// Exact for a rigid transform; a sheared basis is not representable and comes
// back as the nearest rotation, which is the only sensible answer.
BonePose decompose_bone_pose(const glm::mat4& matrix);

void compose_local_poses(const std::vector<BonePose>& poses,
                         std::vector<glm::mat4>& out_local);

// compose_local_poses() the other way round.
void decompose_local_poses(const std::vector<glm::mat4>& local,
                           std::vector<BonePose>& out_poses);

// out = from at weight 0, to at weight 1. Sizes must match; a mismatch clears
// the output rather than blending half a skeleton, because a half-blended
// palette draws as a body torn in two and points at nothing.
//
// `out` MAY alias `to` (each element is read before it is written), which is
// what lets a caller fade into a buffer it already filled without a third one.
void blend_bone_poses(const std::vector<BonePose>& from,
                      const std::vector<BonePose>& to, float weight,
                      std::vector<BonePose>& out);

class Animation {
public:
    struct PosKey { float time = 0.0f; glm::vec3 value{}; };
    struct RotKey { float time = 0.0f; glm::quat value{}; };
    struct ScaleKey { float time = 0.0f; glm::vec3 value{1.0f}; };

    bool load(const std::string& path, const Skeleton& skeleton);
    float duration() const { return duration_; }
    int unresolved_channels() const { return unresolved_channels_; }

    // The parts form. sample() is this plus compose_local_poses().
    void sample_parts(float time, const Skeleton& skeleton,
                      std::vector<BonePose>& out_parts) const;

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

// Read the root bone's horizontal translation out of a sampled pose. The
// anchor reference for a one-shot clip is taken with this, once, at load.
glm::vec2 root_translation_xz(const Skeleton& skeleton,
                              const std::vector<glm::mat4>& local_poses);

// The other half of the root-motion story, and the one a fall needs.
//
// strip_root_motion_xz() is right for locomotion: the game moves the character
// and the clip must not move it again. It is WRONG for a knockdown or a death,
// where the authored travel — the metre the body carries forward as it goes
// down — is the whole point, and pinning the root to bind leaves the character
// collapsing on the spot like a dropped puppet.
//
// So a one-shot keeps its authored travel RELATIVE to a reference frame:
// bind XZ plus (sampled XZ - reference XZ). Which frame is the reference is
// not always t=0. For a fall it is the clip's standing start; for a get-up it
// is the clip's END, because that is where the character is standing — see
// city/character_getup.h, which paid for that distinction with a body floating
// a full body-length above the ground.
void anchor_root_motion_xz(const Skeleton& skeleton,
                           std::vector<glm::mat4>& local_poses,
                           const glm::vec2& reference_xz);

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
