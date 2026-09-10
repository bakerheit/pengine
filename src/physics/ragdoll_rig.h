#pragma once

// Binding a ragdoll to a skeleton, in both directions.
//
// physics/ragdoll.h solves twenty points in metres and knows nothing about
// bones. This is the only place that knows both, and it does exactly two
// things:
//
//   BUILD   measure a RagdollFigure off a skeleton's bind pose, so limb
//           lengths, joint limits and contact radii are the model's own and
//           not a table of centimetres that is right for one character.
//
//   READ    turn solved node positions back into the local bone poses the
//           existing pose pipeline already consumes, so everything downstream
//           — compute_skin_matrices, the dual quaternions, the shader — is
//           untouched. A ragdoll is a different SOURCE of a pose, not a
//           different kind of pose.
//
// Bones the ragdoll does not drive (the clavicles, the fingers) inherit their
// parent's rotation and keep their bind offset. That is why the finger joints
// do not need to be in the joint enum: a hand that holds its bind shape while
// the arm swings is correct, and twenty-eight constrained points to animate
// knuckles nobody can see at thirty metres is not.

#include <array>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/skeletal_animation.h"
#include "physics/ragdoll.h"

namespace apricot {

struct RagdollRig {
    RagdollFigure figure;

    // Skeleton bone index per ragdoll joint, or -1. A rig missing any of them
    // is rejected outright rather than solved with a hole in it: a figure with
    // no left knee has no left leg, and a solver given one produces a body
    // whose foot is attached to its hip.
    std::array<int, kRagdollJointCount> bone{};

    // Bind pose, accumulated once. `bind_world` is mesh space, matching what
    // Skeleton::compute_skin_matrices builds at sample time. `parent` is the
    // skeleton's own hierarchy, copied so the two hot loops below can walk it
    // without holding a reference to the Skeleton that outlives the model.
    std::vector<int32_t> parent;
    std::vector<glm::mat4> bind_local;
    std::vector<glm::mat4> bind_world;

    // Bone indices in an order where every parent precedes its children.
    //
    // NOT the file order. Several of the shipped rigs store a child before its
    // parent, and Skeleton::compute_skin_matrices copes by sweeping repeatedly
    // until nothing is left — which is correct and is also a variable number
    // of passes over the whole skeleton. Both loops here need the same
    // guarantee every frame, so the order is resolved ONCE at build and then
    // walked straight through.
    std::vector<uint32_t> order;
    // Bind world position of each joint, in METRES — the same space the solver
    // works in — so the figure's rest lengths and the readback agree.
    std::array<glm::vec3, kRagdollJointCount> bind_metres{};

    // Mesh units per metre. The readback divides by it; the build multiplies.
    float model_scale = 1.0f;

    bool valid = false;
};

// Measure the figure. `model_scale` is the uniform scale that takes the mesh's
// own units to metres — CharacterVisual::Model::local.scale.
//
// Returns false, with a logged reason, if the skeleton is not a humanoid this
// can drive. Fail here, loudly, at load: a rig that silently half-binds
// produces a body that half-falls, and that reads as a physics bug for as long
// as it takes somebody to notice the elbow never moved.
bool ragdoll_rig_build(const Skeleton& skeleton, float model_scale,
                       RagdollRig& out);

// Joint positions of the CURRENT animated pose, in metres, ready to hand to
// ragdoll_launch(). `local_poses` is whatever the animator last produced and
// `model_world` is the matrix that takes mesh space to world space — for the
// character path that is (root * model.local).matrix().
void ragdoll_sample_pose(const RagdollRig& rig,
                         const std::vector<glm::mat4>& local_poses,
                         const glm::mat4& model_world,
                         std::array<glm::vec3, kRagdollJointCount>& out_world);

// Crossfade a solved ragdoll pose into an animated one, for the get-up.
//
// This is blend_bone_poses() with ONE difference, and the difference is the
// whole reason it exists: the local translation is interpolated as a direction
// and a length rather than as a vector.
//
// A skeleton's local translation IS its bone, and lerping between two of them
// shortens it — halfway between two directions is a vector shorter than
// either. Clip-to-clip fades never notice, because every clip carries the same
// bind translations and the lerp is between a value and itself. A solved pose
// does not: the readback writes the exact parent-relative offset, and for a
// bone whose PARENT has more than one child — the collarbones off the chest,
// the thighs off the pelvis — that direction genuinely differs from bind,
// because the parent's rotation was recovered from a different child. Measured
// on a landed body, a plain part-wise blend shortened a bone by 34% at the
// middle of the fade.
//
// Same shortest-arc slerp, same scale lerp, and identical to blend_bone_poses
// wherever the two translations are parallel — which is every case the
// animator has.
void ragdoll_getup_blend(const std::vector<BonePose>& from,
                         const std::vector<BonePose>& to, float weight,
                         std::vector<BonePose>& out);

// The other direction. Writes local bone poses that place the skeleton on the
// solved figure. `model_world` is the SAME matrix the caller will render with,
// so the inverse below cancels it exactly; passing a different one moves the
// body by the difference.
void ragdoll_bone_poses(const RagdollRig& rig, const RagdollState& state,
                        const glm::mat4& model_world,
                        std::vector<glm::mat4>& out_local);

}  // namespace apricot
