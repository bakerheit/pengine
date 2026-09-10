#pragma once

// A ragdoll: a human figure solved as constrained particles at the sim's
// fixed step.
//
// WHY PARTICLES AND NOT RIGID BODIES. A rigid-body ragdoll needs a full
// articulated solver — inertia tensors, angular impulses, warm-started
// contacts — and every one of those is a place for a float to land differently
// and for a replay to diverge. This engine has one clock and one step and
// pays for that in determinism tests; a solver whose iteration count depends
// on a convergence tolerance breaks that on the first frame the tolerance is
// met early. So the figure is twenty points held apart by distance
// constraints, projected a FIXED number of times per step, in array order.
// Same input, same output, on every machine, forever.
//
// It buys the look, too. Bones do not stretch, joints do not pop, and the
// whole thing is unconditionally stable: a constraint projection can only ever
// move a point toward its rest length, so an impact of any magnitude settles
// instead of exploding. An impulse solver at 120 Hz with a car doing 25 m/s
// does not have that property for free.
//
// WHAT IT DOES NOT KNOW. This header knows nothing about skeletons, clips or
// models. It solves a figure described in metres. physics/ragdoll_rig.h binds
// it to a Skeleton in both directions — bind pose in, bone poses out — and is
// the only thing that needs core/skeletal_animation.h.
//
// NOTHING READS BACK FROM IT. A ragdoll is a leaf: no impulse returns to the
// car, no force reaches the crowd, no sim decision is taken from a node
// position. That is the same rule traffic/crowd.h already states about
// knockdowns, and keeping it means a ragdoll cannot desync a drive no matter
// how its floats land. Do not make anything upstream depend on this.
//
// AND THAT IS WHY THIS IS NOT ON THE SIM CLOCK. ragdoll_step() is pure in its
// arguments and reproduces exactly given the same dt sequence — that is what
// tests/ragdoll_tests.cpp pins, at the fixed step. The character layer calls
// it once per RENDER frame with the seconds since it last did, exactly as it
// already advances the animator, so two machines at different frame rates see
// the same person fall differently.
//
// That is a deliberate trade and it is only available because of the rule
// above: the fall is presentation, it feeds nothing back, and a replay's
// DRIVE is identical either way. If anything downstream ever needs the pose
// itself to be replay-exact, this has to move onto the fixed step, and that
// is a change to where it is called from rather than to anything in here.

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace apricot {

class TerrainCollider;

// The joints, and the order the solver walks them. Fixed, because the link
// table below is written against these indices and a rig that cannot supply
// one of them is not a humanoid.
enum class RagdollJoint : uint8_t {
    Hips = 0,
    Spine,
    Chest,
    Neck,
    Head,
    HeadTip,
    // THE CLAVICLES ARE HERE FOR A MEASURED REASON.
    //
    // Leave them out and the chest drives the upper arm directly, while the
    // skeleton still has a collarbone in between — undriven, rigidly following
    // the chest, with the arm teleported to wherever the solver put it. The
    // bone between the two is then constrained by nothing at all, and on a
    // landed body it measured 0.290 m against a bind length of 0.138 m: a
    // shoulder pulled to double its size, every time.
    //
    // The spine gets away with the same shape because Hips-Spine-Spine1 is
    // very nearly straight, so forcing the far end at bind distance leaves the
    // middle where it belongs. A collarbone sits at a right angle to the arm,
    // which is the worst case there is. Every bone this figure drives now has
    // a driven parent.
    ClavicleL,
    ShoulderL,
    ElbowL,
    WristL,
    ClavicleR,
    ShoulderR,
    ElbowR,
    WristR,
    HipL,
    KneeL,
    AnkleL,
    ToeL,
    HipR,
    KneeR,
    AnkleR,
    ToeR,
    Count,
};

inline constexpr int kRagdollJointCount = static_cast<int>(RagdollJoint::Count);

constexpr int joint_index(RagdollJoint j) { return static_cast<int>(j); }

const char* ragdoll_joint_name(RagdollJoint j);

// ONE constraint type for bones, for joint limits and for the torso braces.
//
// A bone is min == max: it holds its length exactly. A joint limit is
// min < max between the two ENDS of a two-bone chain: shortening past `min`
// is a limb folded further than a limb folds, lengthening past `max` is a
// knee bent backwards through straight. Both are the same projection with
// different numbers, which is why there is one of them. A second constraint
// type is a second place for the solve order to matter.
// The `max_m` of a constraint that only has a minimum. Far past any distance
// two joints on one body can reach, so the upper clamp never binds.
inline constexpr float kRagdollUnbounded = 1.0e3f;

struct RagdollLink {
    uint8_t a = 0;
    uint8_t b = 0;
    float min_m = 0.0f;
    float max_m = 0.0f;
    // 0..1. Applied once per iteration, so the effective stiffness is
    // 1 - (1 - k)^iterations; anything at or near 1 is rigid within a step.
    float stiffness = 1.0f;
};

// The one thing a distance constraint cannot say: WHICH WAY a knee bends.
//
// Elbows and knees are hinges, and a chain of distance links models them as
// ball joints — the limit stops the shin passing through straight, but
// nothing stops the whole knee inverting so the leg bends forward. A person
// on the pavement with their knees on backwards is the single most obvious
// ragdoll artefact there is, and it costs three lines to not have.
//
// So: the joint must stay on one side of the chord between its neighbours,
// measured along the figure's own forward axis (which is derived from the
// pelvis and chest each step, so it follows the body as it tumbles). Knees
// bulge forward, elbows bulge back.
struct RagdollBulge {
    uint8_t joint = 0;
    uint8_t a = 0;  // chord start (hip / shoulder)
    uint8_t b = 0;  // chord end   (ankle / wrist)
    float sign = 1.0f;  // +1 bulges along forward, -1 against it
};

// Everything about the figure that is a property of the MODEL rather than of
// one person on the floor: link rest lengths measured off the bind pose, and
// the per-joint masses and contact radii. Built once per character model by
// physics/ragdoll_rig.h and shared by every instance using it.
struct RagdollFigure {
    // SORTED: joint limits and torso braces first, rigid bones last.
    //
    // Gauss-Seidel gives the last constraint solved the final say, and the one
    // that must win is bone length — a centimetre of give in a torso brace is
    // invisible, a limb that grew is a rubber arm. Ordering alone halved the
    // worst stretch (0.0156 m to 0.0082 m at a 40 m/s launch) for no cost at
    // all, which makes it the cheapest correctness in this file.
    std::vector<RagdollLink> links;
    // Index of the first rigid link, so the solver can sweep that tail a
    // second time. See ragdoll_step().
    std::size_t rigid_first = 0;
    std::array<RagdollBulge, 4> bulges{};
    std::array<float, kRagdollJointCount> radius_m{};
    std::array<float, kRagdollJointCount> inv_mass{};
    // Straight-line hip-to-head height in the bind pose. The rig uses it to
    // scale the figure; the settle test uses it to size "has stopped moving"
    // to the body rather than to a hardcoded metre.
    float stature_m = 0.0f;
    bool valid = false;
};

// One person, mid-fall.
struct RagdollState {
    std::array<glm::vec3, kRagdollJointCount> position{};
    // Metres per second, explicitly.
    //
    // This was a `previous position` array for a while, which is the classic
    // Verlet form and is tidy right up until you substep: the gap between the
    // two is a displacement per SUBSTEP, so a launch seeded against the frame
    // dt throws the body at four times the intended speed and the bug reads as
    // "the physics tuning is wrong". A velocity has no such unit to get wrong.
    // The solver still recovers velocity from the positions after solving, so
    // a constraint projection is STILL automatically a change in velocity —
    // that property came from the update rule, not from the storage.
    std::array<glm::vec3, kRagdollJointCount> velocity{};
    // Ground/car contact normal from the last step, or zero. Kept for friction
    // and for the settle test, never for gameplay.
    std::array<glm::vec3, kRagdollJointCount> contact{};

    float elapsed_s = 0.0f;
    float still_s = 0.0f;
    bool active = false;

    glm::vec3 centre() const;
    // Whether the body is face-up. Decides which get-up clip to play.
    bool face_up() const;
};

struct RagdollTuning {
    // HEAVIER THAN EARTH, ON PURPOSE. At 9.81 a ragdoll of this size reads
    // floaty: it hangs at the top of its arc long enough to look like it is
    // falling in slow motion, which is the standard tell of a physics doll in
    // a game that is not simulating one at human scale. Cars in this engine
    // already run their own gravity; this is the character's.
    float gravity_mps2 = 17.5f;

    // SUBSTEPS FIRST, then iterations within each. Both fixed, and they must
    // stay fixed: "iterate until the residual is under eps" is a loop whose
    // trip count depends on the floats, which is a different answer on a
    // different machine and a divergent replay.
    //
    // The split is not arbitrary. Ten iterations in one 8.3 ms step could not
    // hold a femur together under a 40 m/s launch — measured, it stretched by
    // centimetres — because Gauss-Seidel converges on the ERROR it is given,
    // and one whole step of gravity and launch velocity is a lot of error to
    // hand it at once. Four substeps of four iterations is fewer total
    // projections and converges an order of magnitude better, because each one
    // starts from a quarter of the error. Substepping is worth more than
    // iterating; if this ever needs to be stiffer, raise substeps.
    int substeps = 4;
    int iterations = 4;

    // Per-second velocity retention, applied as pow(1 - damping, dt).
    //
    // 0.35 was a guess and 0.6 came off the trace. Measured over twelve falls,
    // raising it cuts the direction reversals a joint makes after landing from
    // 71.8/s to 44.9/s — below what the figure does with its joint limits
    // removed entirely — while costing only 16% of the total distance the body
    // travels, so the fall keeps its arc and loses its shimmer. Past about 0.9
    // the trade inverts: the tumble goes mushy (40% less travel) and the
    // reversals start climbing again.
    float linear_damping = 0.6f;

    // Tangential velocity kept per ground contact. Low numbers slide, high
    // numbers stop dead. Multiplied by the surface's own grip, so a body
    // slides further on wet tarmac than on grass without a second table.
    float ground_friction = 0.62f;
    float restitution = 0.04f;

    // Speed cap per node, in m/s. A car doing 25 m/s through a hip is a
    // legitimate launch; a constraint projection that disagrees with the
    // ground on the same step is not, and without a cap the two can trade a
    // point back and forth into the sky.
    float max_speed_mps = 26.0f;

    // How long the hinge limits are enforced for, from the launch.
    //
    // They have to be released, and the release has to be on a clock rather
    // than on a speed. A hinge is a limit with no reaction force behind it, so
    // against gravity it can neither win nor lose: an elbow resting on the
    // pavement is lifted to the anatomically correct side, falls back under
    // gravity, and is lifted again — a cycle that is energy-neutral in the
    // velocity bookkeeping and still does real work every step, because
    // raising a mass and dropping it is what a pump IS.
    //
    // Releasing on a speed threshold does not break the cycle: the cycle
    // itself keeps the joint above the threshold, so the gate holds itself
    // open. Measured, one body in eight then never went still at all — an
    // elbow was still doing 2.8 m/s after nine seconds face-down on the
    // ground, with the rest of the body long since asleep.
    //
    // Elapsed time cannot be self-sustained, which is the whole reason it is
    // the gate. And releasing is the right behaviour anyway: a backwards knee
    // is only visible while a body is tumbling, and a body that has landed
    // should keep the pose it landed in rather than be tidied under the
    // player's eye.
    float hinge_hold_s = 2.5f;

    // How hard the hinge limits pull, per iteration.
    float hinge_relaxation = 0.5f;

    // How close to its contact plane a joint counts as lying on it — and once
    // it does, its hinge limit stops being enforced at all.
    //
    // THIS IS THE FIX FOR THE FLOPPING, and it took four wrong ones to find.
    // A hinge lifts a joint against gravity and gravity drops it back, sixteen
    // times a frame, for as long as the limits are enforced: measured over
    // twelve falls, a quarter of everything a body ever did happened after it
    // had landed, at eleven direction reversals per second per knee, for 2.05
    // seconds. Letting the ground win takes that to 12.5 m out of 138 m and
    // 1.14 s — quieter than the same figure with its joint limits removed
    // entirely, because it also stops the limbs stirring the solver.
    //
    // What did NOT work, each measured: scaling the strength by the fastest
    // joint's speed (the flopping joint holds its own gate open, 0.0 m
    // difference); scaling by the TORSO's speed instead (helps — 38 m — but
    // not enough alone); correcting resting joints weakly (re-introduces the
    // pump for almost no gain); and projecting the correction along the ground
    // so it cannot lift (the bone links then drag the NEIGHBOURING joints up
    // instead, 28 m). Generous on purpose: a knee hovering a few centimetres
    // over the pavement is resting on it as far as the pump is concerned, and
    // every joint this misses keeps arguing with the floor.
    //
    // The cost is real and is stated rather than hidden: a body that lands
    // with a knee through the wrong side keeps it, because nothing is pushing
    // any more. That is the same trade hinge_hold_s already makes.
    float resting_clearance_m = 0.08f;
    // Also gating this on the torso having stopped tumbling was tried, on the
    // theory that a knee clipping the kerb mid-fall still needs its limit. It
    // bought 0.016 m of inversion and cost 10 m of post-landing motion — the
    // wrong side of that trade for the thing this exists to fix.

    // Settling. `still_speed` is scaled by the figure's stature so a child
    // model and an adult one settle on the same visual criterion.
    float still_speed_mps = 0.30f;
    float still_hold_s = 0.55f;
    // Nothing lies on the floor forever. Even a body wedged against geometry
    // gets up, for the identical reason PedLifeTuning bounds a downed timer.
    float max_active_s = 9.0f;

    // How far above a node the ground probe starts, and how far it reaches.
    float probe_lift_m = 0.6f;
    float probe_reach_m = 2.2f;
};

// The launch. Seeds every node from a posed figure and throws it.
//
// `world` is the joint positions of the CURRENT animated pose, in world space
// — not a bind pose. Handing over from the exact frame the animator was
// showing is what makes the switch invisible; seeding from bind snaps the
// body to a T-pose for one frame and reads as a glitch even at 30 m.
//
// `velocity` is the whole-body launch (the car's travel, scaled by how hard it
// hit). `spin` is an angular velocity in rad/s about the body centre; it is
// what stops every victim tumbling identically, and its caller derives it from
// hash_coord() on the person's identity, never from a stream.
void ragdoll_launch(RagdollState& state, const RagdollFigure& figure,
                    const std::array<glm::vec3, kRagdollJointCount>& world,
                    glm::vec3 velocity, glm::vec3 spin);

// One fixed step. Pure in (state, figure, tuning, collider, dt): no clock, no
// allocation, no statics, and the same number of constraint projections every
// time.
//
// `car` is an optional moving vehicle the body may still be in contact with,
// as centre / orientation / half extents. It pushes nodes out of its footprint
// and carries them along at its own speed. NOTHING GOES BACK THE OTHER WAY.
struct RagdollVehicle {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec2 half_extents{1.045f, 2.445f};
    float height_m = 1.45f;
    glm::vec3 velocity{0.0f};
};

void ragdoll_step(RagdollState& state, const RagdollFigure& figure,
                  const RagdollTuning& tuning, const TerrainCollider* world,
                  const RagdollVehicle* car, float dt);

// Slide the whole figure horizontally so its pelvis approaches `target_xz`.
//
// The SIM owns where a knocked-down person ends up — it is where they get up,
// where the next car finds them, and where an officer sees them — and this
// solver runs on the render clock, so it does not get a vote. Both integrate
// the same launch under the same gravity, so they stay within centimetres of
// each other on their own; this closes whatever the limbs and the ground
// contacts add on top.
//
// `blend` is the fraction of the remaining error to take, so a caller frames
// it as a time constant: 1 - exp(-dt / tau). It moves POSITIONS ONLY. There is
// no impulse here and there must not be: velocity is stored explicitly, so a
// re-anchor is invisible to the next step, and making it visible would turn a
// bookkeeping correction into a body that jerks toward its own shadow.
void ragdoll_anchor_xz(RagdollState& state, glm::vec2 target_xz, float blend);

// True once the body has stopped moving for `still_hold_s`, or once it has
// been active for `max_active_s`. The caller blends into a get-up from here.
bool ragdoll_settled(const RagdollState& state, const RagdollTuning& tuning);

}  // namespace apricot
