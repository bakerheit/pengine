#include "physics/ragdoll_rig.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "core/log.h"

namespace apricot {
namespace {

constexpr std::size_t J(RagdollJoint j) { return static_cast<std::size_t>(j); }

// Bone names per joint. Two spellings each, because the pack ships the Mixamo
// namespace prefix and a rig exported without it is otherwise identical.
struct JointBinding {
    RagdollJoint joint;
    const char* name;
};

constexpr JointBinding kBindings[] = {
    {RagdollJoint::Hips,      "Hips"},
    {RagdollJoint::Spine,     "Spine1"},
    {RagdollJoint::Chest,     "Spine2"},
    {RagdollJoint::Neck,      "Neck"},
    {RagdollJoint::Head,      "Head"},
    {RagdollJoint::ClavicleL, "LeftShoulder"},
    {RagdollJoint::ShoulderL, "LeftArm"},
    {RagdollJoint::ElbowL,    "LeftForeArm"},
    {RagdollJoint::WristL,    "LeftHand"},
    {RagdollJoint::ClavicleR, "RightShoulder"},
    {RagdollJoint::ShoulderR, "RightArm"},
    {RagdollJoint::ElbowR,    "RightForeArm"},
    {RagdollJoint::WristR,    "RightHand"},
    {RagdollJoint::HipL,      "LeftUpLeg"},
    {RagdollJoint::KneeL,     "LeftLeg"},
    {RagdollJoint::AnkleL,    "LeftFoot"},
    {RagdollJoint::ToeL,      "LeftToeBase"},
    {RagdollJoint::HipR,      "RightUpLeg"},
    {RagdollJoint::KneeR,     "RightLeg"},
    {RagdollJoint::AnkleR,    "RightFoot"},
    {RagdollJoint::ToeR,      "RightToeBase"},
};

static_assert(std::size(kBindings) ==
                  static_cast<std::size_t>(RagdollJoint::Count) - 1,
              "every ragdoll joint except the synthetic head tip needs a "
              "bone name");

// THE HEAD TIP IS NOT A BONE, and it cannot be one.
//
// Exactly one of the twenty-six shipped character rigs has a HeadTop_End;
// every other one stops at Head. Requiring it would bind one civilian and
// reject twenty-five, and dropping it instead would leave the Head bone with
// nothing to aim at, so a skull would inherit the neck's rotation and never
// turn — which is visible, because a head hitting a kerb is the one part of a
// knockdown a player actually watches.
//
// So it is derived: along the neck-to-head direction, at the proportion the
// one rig that HAS the bone actually uses (measured 2.04, taken as 2.0). It
// gives the head a direction to point and a sphere to land on, on every model,
// with no per-model table to keep true.
constexpr float kHeadTipExtend = 2.0f;

// Which joint each driven joint POINTS AT. The rotation of a bone is recovered
// from the direction to its child, so a joint with no child (the head tip, the
// toes, the wrists) inherits its parent's rotation and only supplies a
// position. That is correct: a wrist has no bone past it in this figure, so
// there is no direction to recover, and inventing one rolls the hand.
constexpr RagdollJoint kAimTarget[] = {
    /* Hips      */ RagdollJoint::Spine,
    /* Spine     */ RagdollJoint::Chest,
    /* Chest     */ RagdollJoint::Neck,
    /* Neck      */ RagdollJoint::Head,
    /* Head      */ RagdollJoint::HeadTip,
    /* HeadTip   */ RagdollJoint::Count,
    /* ClavicleL */ RagdollJoint::ShoulderL,
    /* ShoulderL */ RagdollJoint::ElbowL,
    /* ElbowL    */ RagdollJoint::WristL,
    /* WristL    */ RagdollJoint::Count,
    /* ClavicleR */ RagdollJoint::ShoulderR,
    /* ShoulderR */ RagdollJoint::ElbowR,
    /* ElbowR    */ RagdollJoint::WristR,
    /* WristR    */ RagdollJoint::Count,
    /* HipL      */ RagdollJoint::KneeL,
    /* KneeL     */ RagdollJoint::AnkleL,
    /* AnkleL    */ RagdollJoint::ToeL,
    /* ToeL      */ RagdollJoint::Count,
    /* HipR      */ RagdollJoint::KneeR,
    /* KneeR     */ RagdollJoint::AnkleR,
    /* AnkleR    */ RagdollJoint::ToeR,
    /* ToeR      */ RagdollJoint::Count,
};

static_assert(std::size(kAimTarget) == static_cast<std::size_t>(
                                           RagdollJoint::Count),
              "every ragdoll joint needs an aim target or Count");

// Relative masses. The torso is heavy and the limbs are light, which is what
// makes an arm whip when a hip is struck instead of the whole figure
// translating like a table. Absolute scale is irrelevant — the solver only
// ever uses ratios — so these are kilograms only in spirit.
constexpr float kMass[] = {
    /* Hips      */ 14.0f,
    /* Spine     */ 9.0f,
    /* Chest     */ 9.0f,
    /* Neck      */ 2.0f,
    /* Head      */ 4.5f,
    /* HeadTip   */ 0.6f,
    /* ClavicleL */ 2.5f,
    /* ShoulderL */ 3.0f,
    /* ElbowL    */ 2.0f,
    /* WristL    */ 1.0f,
    /* ClavicleR */ 2.5f,
    /* ShoulderR */ 3.0f,
    /* ElbowR    */ 2.0f,
    /* WristR    */ 1.0f,
    /* HipL      */ 5.0f,
    /* KneeL     */ 3.2f,
    /* AnkleL    */ 1.6f,
    /* ToeL      */ 0.5f,
    /* HipR      */ 5.0f,
    /* KneeR     */ 3.2f,
    /* AnkleR    */ 1.6f,
    /* ToeR      */ 0.5f,
};

static_assert(std::size(kMass) == static_cast<std::size_t>(RagdollJoint::Count),
              "every ragdoll joint needs a mass");

// Contact sphere radius as a fraction of STATURE (bind hips-to-head), so a
// shorter model gets a proportionally smaller body rather than a normal one
// hovering. Roughly half the real thickness of each part: too big and the
// corpse floats, too small and the pavement shows through an elbow.
constexpr float kRadiusFraction[] = {
    /* Hips      */ 0.190f,
    /* Spine     */ 0.185f,
    /* Chest     */ 0.185f,
    /* Neck      */ 0.090f,
    /* Head      */ 0.165f,
    /* HeadTip   */ 0.100f,
    /* ClavicleL */ 0.130f,
    /* ShoulderL */ 0.120f,
    /* ElbowL    */ 0.090f,
    /* WristL    */ 0.075f,
    /* ClavicleR */ 0.130f,
    /* ShoulderR */ 0.120f,
    /* ElbowR    */ 0.090f,
    /* WristR    */ 0.075f,
    /* HipL      */ 0.140f,
    /* KneeL     */ 0.100f,
    /* AnkleL    */ 0.085f,
    /* ToeL      */ 0.065f,
    /* HipR      */ 0.140f,
    /* KneeR     */ 0.100f,
    /* AnkleR    */ 0.085f,
    /* ToeR      */ 0.065f,
};

static_assert(std::size(kRadiusFraction) ==
                  static_cast<std::size_t>(RagdollJoint::Count),
              "every ragdoll joint needs a radius");

glm::vec3 translation_of(const glm::mat4& m) { return glm::vec3{m[3]}; }

// Used at build time on the bind pose and at launch time on the live pose, so
// the two can never disagree about where the top of the head is.
glm::vec3 ragdoll_head_tip(glm::vec3 neck, glm::vec3 head) {
    return head + (head - neck) * kHeadTipExtend;
}

// The minimal rotation taking `from` onto `to`, both unit. Written out rather
// than pulled from a glm extension because this tree uses no glm/gtx headers
// and one function is not worth the first one.
glm::quat rotation_between(glm::vec3 from, glm::vec3 to) {
    const float d = glm::dot(from, to);
    if (d > 0.99999f) return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    if (d < -0.99999f) {
        // Antiparallel: every axis perpendicular to `from` is a valid half
        // turn, so pick one that is definitely not parallel to it. Choosing
        // "whichever axis `from` is least aligned with" is what stops the
        // cross product collapsing to zero for an axis-aligned bone.
        glm::vec3 axis = std::fabs(from.x) < 0.9f ? glm::vec3{1.0f, 0.0f, 0.0f}
                                                  : glm::vec3{0.0f, 1.0f, 0.0f};
        axis = glm::normalize(glm::cross(from, axis));
        return glm::angleAxis(3.14159265358979f, axis);
    }
    const glm::vec3 axis = glm::cross(from, to);
    const float s = std::sqrt((1.0f + d) * 2.0f);
    return glm::normalize(
        glm::quat{s * 0.5f, axis.x / s, axis.y / s, axis.z / s});
}

void add_bone(RagdollFigure& figure,
              const std::array<glm::vec3, kRagdollJointCount>& bind,
              RagdollJoint a, RagdollJoint b) {
    const float rest = glm::length(bind[J(b)] - bind[J(a)]);
    figure.links.push_back({static_cast<uint8_t>(J(a)),
                            static_cast<uint8_t>(J(b)), rest, rest, 1.0f});
}

void add_limit(RagdollFigure& figure,
               const std::array<glm::vec3, kRagdollJointCount>& bind,
               RagdollJoint a, RagdollJoint b, float low, float high,
               float stiffness) {
    const float rest = glm::length(bind[J(b)] - bind[J(a)]);
    figure.links.push_back({static_cast<uint8_t>(J(a)),
                            static_cast<uint8_t>(J(b)), rest * low, rest * high,
                            stiffness});
}

// A limb's limit is measured against its own STRAIGHT length — the two bones
// added up — not against the bind chord. An A-pose rig binds with the elbow
// already slightly bent, and a maximum taken from that chord is a limit that
// forbids the arm from ever straightening.
void add_limb_limit(RagdollFigure& figure,
                    const std::array<glm::vec3, kRagdollJointCount>& bind,
                    RagdollJoint a, RagdollJoint mid, RagdollJoint b,
                    float low, float high) {
    const float straight = glm::length(bind[J(mid)] - bind[J(a)]) +
                           glm::length(bind[J(b)] - bind[J(mid)]);
    figure.links.push_back({static_cast<uint8_t>(J(a)),
                            static_cast<uint8_t>(J(b)), straight * low,
                            straight * high, 1.0f});
}

// A MINIMUM separation with no maximum: the two joints may go anywhere except
// through one another. `apart` is a fraction of stature, so a shorter model
// gets proportionally smaller limbs to keep out of its own torso.
//
// This is the whole of the self-collision story, and it is deliberately not a
// general one. A ragdoll has no body volume here — twenty points and some
// sticks — so nothing stops a forearm passing through a ribcage, and what a
// player sees when it does is an arm coming out of a chest. Naming the dozen
// pairs that actually intersect costs a dozen distance constraints; a real
// capsule-vs-capsule pass costs a broadphase and a solver rewrite for the same
// visible result at this camera distance.
void add_spread(RagdollFigure& figure, float stature, RagdollJoint a,
                RagdollJoint b, float apart) {
    figure.links.push_back({static_cast<uint8_t>(J(a)),
                            static_cast<uint8_t>(J(b)), stature * apart,
                            kRagdollUnbounded, 1.0f});
}

}  // namespace

bool ragdoll_rig_build(const Skeleton& skeleton, float model_scale,
                       RagdollRig& out) {
    out = RagdollRig{};
    out.bone.fill(-1);
    out.model_scale = model_scale;

    const int count = skeleton.bone_count();
    if (count <= 0 || !(model_scale > 1e-6f)) {
        AP_ERROR("ragdoll rig: skeleton has %d bones at scale %.6f", count,
                 static_cast<double>(model_scale));
        return false;
    }

    for (const JointBinding& binding : kBindings) {
        int index = skeleton.find_bone(std::string("mixamorig:") + binding.name);
        if (index < 0) index = skeleton.find_bone(binding.name);
        if (index < 0) {
            AP_ERROR("ragdoll rig: skeleton has no bone for joint '%s' "
                     "(looked for '%s')",
                     ragdoll_joint_name(binding.joint), binding.name);
            return false;
        }
        out.bone[J(binding.joint)] = index;
    }

    const std::size_t bones = static_cast<std::size_t>(count);
    out.parent.resize(bones);
    out.bind_local.resize(bones);
    out.bind_world.resize(bones);
    for (std::size_t i = 0; i < bones; ++i) {
        const Bone& b = skeleton.bone(static_cast<int>(i));
        out.parent[i] = b.parent;
        out.bind_local[i] = b.bind_local;
    }

    // Resolve a parent-first order. Repeated sweeps, taking whatever became
    // ready — the same thing compute_skin_matrices does per sample, done once
    // here instead. A skeleton whose hierarchy does not drain is a cycle, and
    // that is a broken asset rather than something to solve around.
    out.order.reserve(bones);
    std::vector<bool> placed(bones, false);
    bool progressed = true;
    while (out.order.size() < bones && progressed) {
        progressed = false;
        for (std::size_t i = 0; i < bones; ++i) {
            if (placed[i]) continue;
            const int32_t parent = out.parent[i];
            if (parent >= 0 && !placed[static_cast<std::size_t>(parent)])
                continue;
            placed[i] = true;
            out.order.push_back(static_cast<uint32_t>(i));
            progressed = true;
        }
    }
    if (out.order.size() != bones) {
        AP_ERROR("ragdoll rig: skeleton hierarchy contains a parent cycle");
        return false;
    }

    for (uint32_t i : out.order) {
        const std::size_t k = i;
        out.bind_world[k] = out.parent[k] < 0
            ? out.bind_local[k]
            : out.bind_world[static_cast<std::size_t>(out.parent[k])] *
                  out.bind_local[k];
    }

    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        if (out.bone[k] < 0) continue;  // the head tip; see kHeadTipExtend
        out.bind_metres[k] = translation_of(
            out.bind_world[static_cast<std::size_t>(out.bone[k])]) * model_scale;
    }
    out.bind_metres[J(RagdollJoint::HeadTip)] = ragdoll_head_tip(
        out.bind_metres[J(RagdollJoint::Neck)],
        out.bind_metres[J(RagdollJoint::Head)]);

    RagdollFigure& figure = out.figure;
    const std::array<glm::vec3, kRagdollJointCount>& bind = out.bind_metres;
    figure.stature_m = glm::length(bind[J(RagdollJoint::Head)] -
                                   bind[J(RagdollJoint::Hips)]);
    if (!(figure.stature_m > 0.05f)) {
        AP_ERROR("ragdoll rig: bind hips-to-head is %.4f m, which is not a "
                 "person", static_cast<double>(figure.stature_m));
        return false;
    }

    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        figure.inv_mass[k] = 1.0f / kMass[k];
        figure.radius_m[k] = kRadiusFraction[k] * figure.stature_m;
    }

    // --- bones -------------------------------------------------------------
    figure.links.reserve(32);
    add_bone(figure, bind, RagdollJoint::Hips, RagdollJoint::Spine);
    add_bone(figure, bind, RagdollJoint::Spine, RagdollJoint::Chest);
    add_bone(figure, bind, RagdollJoint::Chest, RagdollJoint::Neck);
    add_bone(figure, bind, RagdollJoint::Neck, RagdollJoint::Head);
    add_bone(figure, bind, RagdollJoint::Head, RagdollJoint::HeadTip);
    add_bone(figure, bind, RagdollJoint::Chest, RagdollJoint::ClavicleL);
    add_bone(figure, bind, RagdollJoint::ClavicleL, RagdollJoint::ShoulderL);
    add_bone(figure, bind, RagdollJoint::ShoulderL, RagdollJoint::ElbowL);
    add_bone(figure, bind, RagdollJoint::ElbowL, RagdollJoint::WristL);
    add_bone(figure, bind, RagdollJoint::Chest, RagdollJoint::ClavicleR);
    add_bone(figure, bind, RagdollJoint::ClavicleR, RagdollJoint::ShoulderR);
    add_bone(figure, bind, RagdollJoint::ShoulderR, RagdollJoint::ElbowR);
    add_bone(figure, bind, RagdollJoint::ElbowR, RagdollJoint::WristR);
    add_bone(figure, bind, RagdollJoint::Hips, RagdollJoint::HipL);
    add_bone(figure, bind, RagdollJoint::HipL, RagdollJoint::KneeL);
    add_bone(figure, bind, RagdollJoint::KneeL, RagdollJoint::AnkleL);
    add_bone(figure, bind, RagdollJoint::AnkleL, RagdollJoint::ToeL);
    add_bone(figure, bind, RagdollJoint::Hips, RagdollJoint::HipR);
    add_bone(figure, bind, RagdollJoint::HipR, RagdollJoint::KneeR);
    add_bone(figure, bind, RagdollJoint::KneeR, RagdollJoint::AnkleR);
    add_bone(figure, bind, RagdollJoint::AnkleR, RagdollJoint::ToeR);

    // --- the torso is a shape, not a chain ---------------------------------
    // A spine modelled as three links in a row has no resistance to folding,
    // and a body that folds in half at the waist reads as a bag rather than a
    // person. The shoulder and pelvis spans are rigid and every diagonal
    // between them is very nearly so.
    //
    // THESE USED TO ALLOW 22% COMPRESSION, and that is not a tolerance, it is
    // a torso that can lose a fifth of its height and keep it. The mesh does
    // not stretch to match — the bones hold their bind length and the skinning
    // pulls the vertices between them — so what a player sees is a person
    // squashed. A real ribcage compresses by almost nothing; 8% is already
    // generous and only exists so the spine can curl at all.
    // Collarbone to collarbone is the rigid span now — it is the one that is
    // actually bone. The shoulders themselves may close a little, because a
    // person's do.
    add_bone(figure, bind, RagdollJoint::ClavicleL, RagdollJoint::ClavicleR);
    add_limit(figure, bind, RagdollJoint::ShoulderL, RagdollJoint::ShoulderR,
              0.82f, 1.04f, 1.0f);
    add_bone(figure, bind, RagdollJoint::HipL, RagdollJoint::HipR);
    add_limit(figure, bind, RagdollJoint::Hips, RagdollJoint::Neck, 0.92f,
              1.00f, 1.0f);
    // Braced to the COLLARBONES, not the shoulders. The shoulder joints hang
    // off the clavicles and are allowed to close — a person shrugs — so a
    // torso whose shape is defined through them is a torso that shortens
    // every time the arms come together. Measured, bracing to the shoulders
    // after the clavicles became soft cost 4% of torso length.
    add_limit(figure, bind, RagdollJoint::Hips, RagdollJoint::ClavicleL, 0.92f,
              1.01f, 1.0f);
    add_limit(figure, bind, RagdollJoint::Hips, RagdollJoint::ClavicleR, 0.92f,
              1.01f, 1.0f);
    add_limit(figure, bind, RagdollJoint::Chest, RagdollJoint::HipL, 0.92f,
              1.01f, 1.0f);
    add_limit(figure, bind, RagdollJoint::Chest, RagdollJoint::HipR, 0.92f,
              1.01f, 1.0f);
    // The true diagonals, which are what a TWIST shortens. Without them the
    // shoulders can rotate a long way over the hips while every link above is
    // still perfectly happy, and the result is a body wrung out like a towel.
    add_limit(figure, bind, RagdollJoint::ClavicleL, RagdollJoint::HipR, 0.88f,
              1.04f, 0.9f);
    add_limit(figure, bind, RagdollJoint::ClavicleR, RagdollJoint::HipL, 0.88f,
              1.04f, 0.9f);

    // --- how far a joint bends ---------------------------------------------
    add_limb_limit(figure, bind, RagdollJoint::ShoulderL, RagdollJoint::ElbowL,
                   RagdollJoint::WristL, 0.32f, 0.98f);
    add_limb_limit(figure, bind, RagdollJoint::ShoulderR, RagdollJoint::ElbowR,
                   RagdollJoint::WristR, 0.32f, 0.98f);
    add_limb_limit(figure, bind, RagdollJoint::HipL, RagdollJoint::KneeL,
                   RagdollJoint::AnkleL, 0.38f, 0.99f);
    add_limb_limit(figure, bind, RagdollJoint::HipR, RagdollJoint::KneeR,
                   RagdollJoint::AnkleR, 0.38f, 0.99f);
    // A neck folded to 60% of straight puts the chin through the sternum.
    add_limb_limit(figure, bind, RagdollJoint::Chest, RagdollJoint::Neck,
                   RagdollJoint::Head, 0.82f, 0.99f);

    // --- limbs stay out of the body, and out of each other -----------------
    const float st = figure.stature_m;
    add_spread(figure, st, RagdollJoint::KneeL, RagdollJoint::KneeR, 0.20f);
    add_spread(figure, st, RagdollJoint::AnkleL, RagdollJoint::AnkleR, 0.22f);
    add_spread(figure, st, RagdollJoint::WristL, RagdollJoint::WristR, 0.18f);
    add_spread(figure, st, RagdollJoint::WristL, RagdollJoint::Hips, 0.34f);
    add_spread(figure, st, RagdollJoint::WristR, RagdollJoint::Hips, 0.34f);
    add_spread(figure, st, RagdollJoint::WristL, RagdollJoint::Chest, 0.30f);
    add_spread(figure, st, RagdollJoint::WristR, RagdollJoint::Chest, 0.30f);
    add_spread(figure, st, RagdollJoint::ElbowL, RagdollJoint::Hips, 0.42f);
    add_spread(figure, st, RagdollJoint::ElbowR, RagdollJoint::Hips, 0.42f);
    // Legs against the chest are the loosest of these on purpose. A person
    // curled up really does get a heel close to their own ribs, and the leg's
    // own bones outrank a separation anyway — set this to the distance a
    // straight-backed adult can manage and it does not hold, it just loses to
    // the femur every time a body tucks.
    add_spread(figure, st, RagdollJoint::AnkleL, RagdollJoint::Chest, 0.42f);
    add_spread(figure, st, RagdollJoint::AnkleR, RagdollJoint::Chest, 0.42f);
    add_spread(figure, st, RagdollJoint::KneeL, RagdollJoint::Chest, 0.38f);
    add_spread(figure, st, RagdollJoint::KneeR, RagdollJoint::Chest, 0.38f);
    // A head driven down through its own shoulders is the other half of the
    // "wrung out" look, and the spine chain alone does not stop it.
    add_spread(figure, st, RagdollJoint::Head, RagdollJoint::ShoulderL, 0.32f);
    add_spread(figure, st, RagdollJoint::Head, RagdollJoint::ShoulderR, 0.32f);

    // --- and which way ------------------------------------------------------
    figure.bulges[0] = {static_cast<uint8_t>(J(RagdollJoint::KneeL)),
                        static_cast<uint8_t>(J(RagdollJoint::HipL)),
                        static_cast<uint8_t>(J(RagdollJoint::AnkleL)), 1.0f};
    figure.bulges[1] = {static_cast<uint8_t>(J(RagdollJoint::KneeR)),
                        static_cast<uint8_t>(J(RagdollJoint::HipR)),
                        static_cast<uint8_t>(J(RagdollJoint::AnkleR)), 1.0f};
    figure.bulges[2] = {static_cast<uint8_t>(J(RagdollJoint::ElbowL)),
                        static_cast<uint8_t>(J(RagdollJoint::ShoulderL)),
                        static_cast<uint8_t>(J(RagdollJoint::WristL)), -1.0f};
    figure.bulges[3] = {static_cast<uint8_t>(J(RagdollJoint::ElbowR)),
                        static_cast<uint8_t>(J(RagdollJoint::ShoulderR)),
                        static_cast<uint8_t>(J(RagdollJoint::WristR)), -1.0f};

    // Limits and braces first, rigid bones last. See RagdollFigure::links.
    const auto split = std::stable_partition(
        figure.links.begin(), figure.links.end(),
        [](const RagdollLink& link) { return link.min_m != link.max_m; });
    figure.rigid_first = static_cast<std::size_t>(
        std::distance(figure.links.begin(), split));

    figure.valid = true;
    out.valid = true;
    return true;
}

void ragdoll_getup_blend(const std::vector<BonePose>& from,
                         const std::vector<BonePose>& to, float weight,
                         std::vector<BonePose>& out) {
    if (from.size() != to.size()) {
        AP_ERROR("ragdoll: refusing to blend %zu bones against %zu",
                 from.size(), to.size());
        out.clear();
        return;
    }
    const float alpha = std::clamp(weight, 0.0f, 1.0f);
    out.resize(from.size());
    for (std::size_t i = 0; i < from.size(); ++i) {
        const float a = glm::length(from[i].translation);
        const float b = glm::length(to[i].translation);
        if (a > 1e-6f && b > 1e-6f) {
            // Direction and length, separately. The length is a plain lerp
            // between two bone lengths; the direction is where the bone points.
            const glm::vec3 dir = glm::mix(from[i].translation / a,
                                           to[i].translation / b, alpha);
            const float len = glm::length(dir);
            out[i].translation = len > 1e-6f
                ? dir * (glm::mix(a, b, alpha) / len)
                // Exactly opposed: there is no shorter arc between them and
                // no direction to pick, so fall back rather than divide by
                // nothing.
                : glm::mix(from[i].translation, to[i].translation, alpha);
        } else {
            out[i].translation = glm::mix(from[i].translation,
                                          to[i].translation, alpha);
        }
        out[i].rotation = glm::normalize(
            glm::slerp(from[i].rotation, to[i].rotation, alpha));
        out[i].scale = glm::mix(from[i].scale, to[i].scale, alpha);
    }
}

void ragdoll_sample_pose(const RagdollRig& rig,
                         const std::vector<glm::mat4>& local_poses,
                         const glm::mat4& model_world,
                         std::array<glm::vec3, kRagdollJointCount>& out_world) {
    out_world.fill(glm::vec3{0.0f});
    if (!rig.valid || local_poses.size() != rig.bind_world.size()) return;

    // Same accumulation compute_skin_matrices does. Done here rather than
    // asking for its output because that output is already multiplied by the
    // inverse bind, and undoing it is a second inversion per bone for a number
    // this loop has anyway.
    std::vector<glm::mat4> world(local_poses.size());
    for (uint32_t i : rig.order) {
        const std::size_t k = i;
        world[k] = rig.parent[k] < 0
            ? local_poses[k]
            : world[static_cast<std::size_t>(rig.parent[k])] * local_poses[k];
    }
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        if (rig.bone[k] < 0) continue;
        const std::size_t b = static_cast<std::size_t>(rig.bone[k]);
        out_world[k] = glm::vec3{model_world * world[b] *
                                 glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    }
    out_world[J(RagdollJoint::HeadTip)] = ragdoll_head_tip(
        out_world[J(RagdollJoint::Neck)], out_world[J(RagdollJoint::Head)]);
}

void ragdoll_bone_poses(const RagdollRig& rig, const RagdollState& state,
                        const glm::mat4& model_world,
                        std::vector<glm::mat4>& out_local) {
    const std::size_t bones = rig.bind_world.size();
    out_local.assign(bones, glm::mat4{1.0f});
    if (!rig.valid || bones == 0) return;

    // World back to mesh space. The caller renders with the same matrix, so
    // this cancels exactly; the ragdoll's absolute world position survives
    // only through it, which is why the two must be the same matrix.
    const glm::mat4 to_mesh = glm::inverse(model_world);

    // Where each driven bone must END UP, in mesh space.
    std::array<glm::vec3, kRagdollJointCount> mesh{};
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        mesh[k] = glm::vec3{to_mesh * glm::vec4{state.position[k], 1.0f}};
    }

    // Rotation per bone, in mesh space. A driven bone takes the minimal
    // rotation carrying its BIND direction onto its SOLVED direction; an
    // undriven one inherits its parent and so keeps the shape it was authored
    // with. Both are then written as a world matrix and converted to local at
    // the end, because a local pose cannot be built until its parent's world
    // is known and the ragdoll supplies neither in that order.
    std::vector<glm::mat4> world(bones);
    std::array<int, kRagdollJointCount> aim{};
    aim.fill(-1);
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const RagdollJoint target = kAimTarget[static_cast<std::size_t>(i)];
        aim[static_cast<std::size_t>(i)] =
            target == RagdollJoint::Count ? -1 : static_cast<int>(target);
    }
    // Reverse map: skeleton bone -> ragdoll joint, so the walk below is over
    // bones in parent-first order and can ask "is this one driven".
    std::vector<int> joint_of(bones, -1);
    for (int i = 0; i < kRagdollJointCount; ++i) {
        const int b = rig.bone[static_cast<std::size_t>(i)];
        if (b >= 0) joint_of[static_cast<std::size_t>(b)] = i;
    }

    for (uint32_t index : rig.order) {
        const std::size_t b = index;
        const int32_t parent = rig.parent[b];
        const int j = joint_of[b];
        const int target =
            j >= 0 ? aim[static_cast<std::size_t>(j)] : -1;

        glm::mat4 m;
        if (target >= 0) {
            const std::size_t from = static_cast<std::size_t>(j);
            const std::size_t to = static_cast<std::size_t>(target);
            // Both directions are in the MESH's axes and differ only by the
            // uniform model scale, so normalising makes them comparable.
            const glm::vec3 bind_dir =
                rig.bind_metres[to] - rig.bind_metres[from];
            const glm::vec3 now_dir = mesh[to] - mesh[from];
            const float bl = glm::length(bind_dir);
            const float nl = glm::length(now_dir);
            const glm::quat rotation = (bl > 1e-6f && nl > 1e-6f)
                ? rotation_between(bind_dir / bl, now_dir / nl)
                : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
            // On the LEFT of the bind transform, so the bone turns about the
            // model origin's axes and is then repositioned — not about its own
            // authored frame, which would apply the correction twice.
            m = glm::mat4_cast(rotation) * rig.bind_world[b];
        } else if (parent >= 0) {
            // Undriven, or driven with nothing past it to aim at: a wrist and
            // a knuckle both keep the shape they were authored with and ride
            // whatever their parent did. Giving an unaimed bone its own bind
            // WORLD rotation instead leaves the hand pointing north while the
            // arm swings, which is the exact tell of a half-bound rig.
            m = world[static_cast<std::size_t>(parent)] * rig.bind_local[b];
        } else {
            m = rig.bind_world[b];
        }
        if (j >= 0) m[3] = glm::vec4{mesh[static_cast<std::size_t>(j)], 1.0f};
        world[b] = m;
    }

    for (std::size_t b = 0; b < bones; ++b) {
        const int32_t parent = rig.parent[b];
        out_local[b] = parent < 0
            ? world[b]
            : glm::inverse(world[static_cast<std::size_t>(parent)]) * world[b];
    }
}

}  // namespace apricot
