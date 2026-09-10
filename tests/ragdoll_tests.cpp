// A ragdoll, driven the way the game drives it.
//
// Nothing here hand-builds a figure. Every case loads a SHIPPED skeleton,
// binds a real rig to it, launches it out of a real animated pose and steps it
// against a real TerrainCollider — because the failures this is guarding
// against are all failures of the join:
//
//   * a rig that binds nineteen of twenty joints and silently drops a knee
//   * bones that stretch under a hard enough impact, so a body arrives at the
//     kerb with one arm three metres long
//   * a solve that settles below the pavement, or hovering above it
//   * a readback that produces a beautiful set of bone poses which put the
//     mesh somewhere other than where the solver put the figure
//   * a solve that is not reproducible, which is the one failure that cannot
//     be seen by looking at it
//
// Each claim is paired with the thing that would make it vacuous, because
// "no bone stretched" is trivially true of a figure that never moved.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/asset_root.h"
#include "core/fixed_step.h"
#include "core/rng.h"
#include "core/skeletal_animation.h"
#include "physics/breakaway_contact.h"
#include "physics/ragdoll.h"
#include "physics/ragdoll_rig.h"
#include "physics/terrain_collider.h"

#include "test_assert.h"

using namespace apricot;
using apricot_test::pass;

namespace {

constexpr float kDt = static_cast<float>(kSimDt);
constexpr char kModel[] = "models/characters/psx_pack/civilian_male_02/skin";
// The height CharacterVisual authors civilians at, so the figure this test
// measures is the figure the game ships.
constexpr float kCharacterHeightM = 1.78f;

struct Fixture {
    Skeleton skeleton;
    SkinnedEmesh mesh;
    RagdollRig rig;
    std::vector<glm::mat4> pose;
    float model_scale = 1.0f;
    glm::mat4 model_world{1.0f};
};

// Load the model, bind the rig, and take the BIND pose as the launch pose.
//
// Deliberately not a sampled walk frame. The .eanim clips are lifted assets
// (tools/lift_character_animations.py, gitignored) and on this tree they are
// authored at a different scale from the skeletons they bind to — a skinned
// idle measures four hundred units tall against a 1.73 m bind mesh. That is a
// real problem and it belongs to the animation pipeline, not here; what
// matters for these cases is that the RAGDOLL is independent of it. It takes
// world joint positions from its caller and has no opinion about where they
// came from, so the bind pose — which is correct, tracked and stable — is the
// honest fixture. A clip-sourced pose would make every number below hostage
// to somebody else's export.
void build(Fixture& f, const TerrainCollider& world,
           glm::vec2 xz = {0.0f, 0.0f}) {
    REQUIRE_MSG(read_skinned_emesh(asset_path(std::string(kModel) + ".emesh"),
                                   f.mesh),
                "the shipped civilian mesh loads", "fixture");
    REQUIRE_MSG(f.skeleton.load(asset_path(std::string(kModel) + ".eskel")),
                "the shipped civilian skeleton loads", "fixture");

    const float source_height = f.mesh.bounds.size().y;
    REQUIRE(source_height > 1e-4f);
    f.model_scale = kCharacterHeightM / source_height;

    // The same chain CharacterVisual renders with: a world root times the
    // model's own recentre/scale/flip. Built here so the round-trip below is
    // through the real transform and not through an identity that would hide a
    // whole class of space errors.
    // Standing on the ground, not at y = 0. The figure has to start on the
    // surface it will land on or the first second of every fall is freefall.
    const glm::vec3 where{
        xz.x, world.height(xz.x, xz.y) + 0.02f, xz.y};
    const glm::mat4 root = glm::translate(glm::mat4{1.0f}, where);
    glm::mat4 local = glm::translate(
        glm::mat4{1.0f},
        glm::vec3{-f.mesh.bounds.center().x * f.model_scale,
                  -f.mesh.bounds.min.y * f.model_scale,
                  -f.mesh.bounds.center().z * f.model_scale});
    local *= glm::mat4_cast(
        glm::angleAxis(3.14159265358979f, glm::vec3{0.0f, 1.0f, 0.0f}));
    local = glm::scale(local, glm::vec3{f.model_scale});
    f.model_world = root * local;

    REQUIRE_MSG(ragdoll_rig_build(f.skeleton, f.model_scale, f.rig),
                "the rig binds to the shipped skeleton", "fixture");

    f.pose.resize(static_cast<std::size_t>(f.skeleton.bone_count()));
    for (int i = 0; i < f.skeleton.bone_count(); ++i)
        f.pose[static_cast<std::size_t>(i)] = f.skeleton.bone(i).bind_local;
}

// The real collider over the real procedural surface. No flat box under it:
// an early version of this file put one at y = 0 and asserted bodies settled
// near y = 0, and every body settled at y = 12 — which was CORRECT, because
// the terrain at the origin is twelve metres up and the box was buried under
// it. Heights here are therefore always measured against a probe, never
// against zero.
TerrainCollider test_world() { return TerrainCollider(0x9A5D0011ull); }

// Ground directly under a point, by the same probe the solver uses.
float ground_under(const TerrainCollider& world, glm::vec3 p) {
    const auto hit = world.probe_down(p + glm::vec3{0.0f, 2.0f, 0.0f}, 12.0f);
    REQUIRE_MSG(hit.hit, "there is no ground under the test fixture", "world");
    return hit.point.y;
}

float longest_bone_error(const RagdollRig& rig, const RagdollState& state) {
    float worst = 0.0f;
    for (const RagdollLink& link : rig.figure.links) {
        if (link.min_m != link.max_m) continue;  // a limit is allowed to give
        const float len = glm::length(state.position[link.b] -
                                      state.position[link.a]);
        worst = std::max(worst, std::fabs(len - link.min_m));
    }
    return worst;
}

// ---------------------------------------------------------------------------

void the_rig_binds_to_every_shipped_civilian() {
    std::size_t bound = 0;
    for (const char* name : {"civilian_male_02", "civilian_female_03",
                             "civilian_male_17_police", "player_male_01"}) {
        Skeleton skeleton;
        SkinnedEmesh mesh;
        const std::string root = std::string("models/characters/psx_pack/") +
                                 name + "/skin";
        REQUIRE_MSG(read_skinned_emesh(asset_path(root + ".emesh"), mesh),
                    "the model's mesh loads", name);
        REQUIRE_MSG(skeleton.load(asset_path(root + ".eskel")),
                    "the model's skeleton loads", name);
        // The model's OWN scale, not a round number: the figure is measured
        // in metres, and a rig built at the wrong scale reports limb lengths
        // that are self-consistent and completely wrong.
        const float source_height = mesh.bounds.size().y;
        REQUIRE(source_height > 1e-4f);
        RagdollRig rig;
        REQUIRE_MSG(ragdoll_rig_build(skeleton,
                                      kCharacterHeightM / source_height, rig),
                    "the rig binds every joint", name);
        REQUIRE_MSG(rig.figure.stature_m > 0.40f &&
                        rig.figure.stature_m < 0.90f,
                    "the measured hips-to-head is not a human one", name);
        for (int i = 0; i < kRagdollJointCount; ++i) {
            // The head tip is derived, not bound: only one of the shipped
            // rigs has a HeadTop_End bone. Everything else must be real.
            if (static_cast<RagdollJoint>(i) == RagdollJoint::HeadTip) {
                REQUIRE_MSG(rig.bone[static_cast<std::size_t>(i)] < 0,
                            "the head tip bound to a bone, so it is no longer "
                            "the same joint on every model", name);
                continue;
            }
            REQUIRE_MSG(rig.bone[static_cast<std::size_t>(i)] >= 0,
                        "a joint bound to no bone",
                        ragdoll_joint_name(static_cast<RagdollJoint>(i)));
        }
        // ... and it still has to be ABOVE the head, or the skull has no
        // sphere to land on and the neck aims at nothing.
        const glm::vec3 head =
            rig.bind_metres[static_cast<std::size_t>(RagdollJoint::Head)];
        const glm::vec3 tip =
            rig.bind_metres[static_cast<std::size_t>(RagdollJoint::HeadTip)];
        REQUIRE_MSG(glm::length(tip - head) > 0.05f,
                    "the derived head tip sits on top of the head", name);
        REQUIRE_MSG(rig.figure.links.size() >= 28u,
                    "the figure has no joint limits, only bones", name);
        ++bound;
    }
    REQUIRE_MSG(bound == 4, "no model was actually checked", "vacuity");
    std::printf("      %zu models bound, %d joints each\n", bound,
                kRagdollJointCount);
    pass("the ragdoll rig binds to the shipped character skeletons");
}

// The one that proves the rig DRIVES THE MESH. A solve is worthless if the
// bone poses it produces put the body somewhere else, and that failure is
// invisible in the solver's own numbers — it looks perfect right up until you
// render it.
void the_bone_poses_reproduce_the_solved_figure() {
    TerrainCollider world = test_world();
    Fixture f;
    build(f, world, {12.0f, -7.0f});

    std::array<glm::vec3, kRagdollJointCount> start{};
    ragdoll_sample_pose(f.rig, f.pose, f.model_world, start);

    RagdollState state;
    ragdoll_launch(state, f.rig.figure, start, {6.0f, 3.0f, 0.0f},
                   {0.0f, 2.0f, 1.0f});
    const RagdollTuning tuning;

    float worst = 0.0f;
    float travelled = 0.0f;
    const glm::vec3 origin = state.centre();
    for (int step = 0; step < 240; ++step) {
        ragdoll_step(state, f.rig.figure, tuning, &world, nullptr, kDt);

        std::vector<glm::mat4> local;
        ragdoll_bone_poses(f.rig, state, f.model_world, local);
        REQUIRE(local.size() == f.pose.size());

        // Push the poses through the SAME accumulation the renderer uses, then
        // read the joint positions back out in world space. If these do not
        // land on the solver's nodes, the mesh is not where the physics is.
        std::vector<glm::mat4> skin;
        f.skeleton.compute_skin_matrices(local, skin);
        REQUIRE_MSG(!skin.empty(), "the poses did not form a valid skeleton",
                    "readback");
        for (int j = 0; j < kRagdollJointCount; ++j) {
            const std::size_t k = static_cast<std::size_t>(j);
            // The head tip drives the skull's rotation but is not itself a
            // bone on twenty-five of the twenty-six shipped rigs, so there is
            // nothing to read it back out of. It is checked through the head,
            // whose direction is the only thing it exists to supply.
            if (f.rig.bone[k] < 0) continue;
            const std::size_t b = static_cast<std::size_t>(f.rig.bone[k]);
            // world_bone = model_world * pose_world, and pose_world is
            // skin * bind_world for this bone.
            const glm::mat4 bone_world =
                f.model_world * skin[b] * f.rig.bind_world[b];
            const glm::vec3 got{bone_world[3]};
            worst = std::max(worst, glm::length(got - state.position[k]));
        }
        travelled = std::max(travelled, glm::length(state.centre() - origin));
    }

    REQUIRE_MSG(travelled > 1.0f,
                "the figure barely moved, so a matching readback proves "
                "nothing", "vacuity");
    REQUIRE_MSG(worst < 2e-3f,
                "the bone poses put a joint somewhere other than the solver "
                "did", "readback");
    std::printf("      readback within %.5f m over 240 steps, %.2f m "
                "travelled\n", static_cast<double>(worst),
                static_cast<double>(travelled));
    pass("the bone poses the rig produces put the mesh on the solved figure");
}

void bones_do_not_stretch_under_a_car() {
    TerrainCollider world = test_world();
    Fixture f;
    build(f, world);
    std::array<glm::vec3, kRagdollJointCount> start{};
    ragdoll_sample_pose(f.rig, f.pose, f.model_world, start);

    const RagdollTuning tuning;
    float worst = 0.0f;
    float fastest_launch = 0.0f;
    // Everything from a nudge to a motorway impact. The cap in the solver is
    // 26 m/s; going past it on purpose is the point, because that is the
    // number a player will find.
    for (float speed : {4.0f, 12.0f, 25.0f, 40.0f}) {
        RagdollState state;
        ragdoll_launch(state, f.rig.figure, start,
                       {speed, speed * 0.35f, 0.0f},
                       {1.0f, speed * 0.2f, 0.5f});
        fastest_launch = std::max(fastest_launch, speed);
        for (int step = 0; step < 600; ++step) {
            ragdoll_step(state, f.rig.figure, tuning, &world, nullptr, kDt);
            worst = std::max(worst, longest_bone_error(f.rig, state));
            for (int j = 0; j < kRagdollJointCount; ++j) {
                const std::size_t k = static_cast<std::size_t>(j);
                REQUIRE_MSG(std::isfinite(state.position[k].x) &&
                                std::isfinite(state.position[k].y) &&
                                std::isfinite(state.position[k].z),
                            "a node went non-finite", "stability");
            }
        }
    }
    REQUIRE_MSG(fastest_launch > 26.0f,
                "nothing was thrown past the solver's own speed cap, so the "
                "case that matters was not tested", "vacuity");
    std::printf("      worst bone error %.5f m at up to 40 m/s\n",
                static_cast<double>(worst));
    REQUIRE_MSG(worst < 0.01f, "a bone changed length", "rigid");
    pass("bones hold their length at any impact speed");
}

void a_body_comes_to_rest_on_the_ground() {
    TerrainCollider world = test_world();
    Fixture f;
    build(f, world);
    std::array<glm::vec3, kRagdollJointCount> start{};
    ragdoll_sample_pose(f.rig, f.pose, f.model_world, start);

    const RagdollTuning tuning;
    std::size_t settled = 0;
    float lowest = 1e9f;
    float highest_rest = -1e9f;
    float slowest_settle = 0.0f;
    Rng rng{hash_coord(0x5AFEC0DEull, 3, 7)};
    for (int trial = 0; trial < 8; ++trial) {
        RagdollState state;
        const glm::vec3 launch{rng.range(-14.0f, 14.0f), rng.range(0.0f, 5.0f),
                               rng.range(-14.0f, 14.0f)};
        const glm::vec3 spin{rng.range(-4.0f, 4.0f), rng.range(-6.0f, 6.0f),
                             rng.range(-4.0f, 4.0f)};
        ragdoll_launch(state, f.rig.figure, start, launch, spin);

        int step = 0;
        const int limit = static_cast<int>(tuning.max_active_s / kDt) + 60;
        for (; step < limit; ++step) {
            ragdoll_step(state, f.rig.figure, tuning, &world, nullptr, kDt);
            if (ragdoll_settled(state, tuning)) break;
        }
        REQUIRE_MSG(step < limit,
                    "a body never settled, so nothing would ever get up",
                    "settle");
        ++settled;
        slowest_settle = std::max(slowest_settle,
                                  static_cast<float>(step) * kDt);

        // On the ground: no node under the surface beneath it, and the
        // pelvis not floating above it. Both measured per node against a real
        // probe, because the surface is procedural and is not level.
        for (int j = 0; j < kRagdollJointCount; ++j) {
            const std::size_t k = static_cast<std::size_t>(j);
            const float clearance = state.position[k].y -
                                    f.rig.figure.radius_m[k] -
                                    ground_under(world, state.position[k]);
            lowest = std::min(lowest, clearance);
        }
        highest_rest = std::max(
            highest_rest,
            state.centre().y - ground_under(world, state.centre()));
    }
    REQUIRE_MSG(settled == 8, "not every trial settled", "vacuity");
    std::printf("      8/8 settled in %.2f s at worst; deepest node %.3f m, "
                "highest pelvis %.3f m\n",
                static_cast<double>(slowest_settle),
                static_cast<double>(lowest),
                static_cast<double>(highest_rest));
    REQUIRE_MSG(lowest > -0.02f, "a body settled through the pavement",
                "sink");
    REQUIRE_MSG(highest_rest < 0.60f,
                "a body settled hovering above the pavement", "float");
    pass("a thrown body comes to rest on the ground and stays there");
}

// A knee this far through the wrong side reads as a broken leg rather than
// a solver residual.
constexpr float kVisiblyBackwards = 0.05f;

void knees_and_elbows_bend_the_way_they_bend() {
    TerrainCollider world = test_world();
    Fixture f;
    build(f, world);
    std::array<glm::vec3, kRagdollJointCount> start{};
    ragdoll_sample_pose(f.rig, f.pose, f.model_world, start);

    const RagdollTuning tuning;
    float worst_inversion = 0.0f;
    float worst_resting = 0.0f;
    std::size_t bent = 0;
    std::size_t airborne = 0;
    std::size_t backwards = 0;
    std::size_t samples = 0;
    Rng rng{hash_coord(0x5AFEC0DEull, 11, 2)};
    for (int trial = 0; trial < 6; ++trial) {
        RagdollState state;
        ragdoll_launch(state, f.rig.figure, start,
                       {rng.range(-16.0f, 16.0f), rng.range(1.0f, 6.0f),
                        rng.range(-16.0f, 16.0f)},
                       {rng.range(-7.0f, 7.0f), rng.range(-7.0f, 7.0f),
                        rng.range(-7.0f, 7.0f)});
        for (int step = 0; step < 700; ++step) {
            ragdoll_step(state, f.rig.figure, tuning, &world, nullptr, kDt);
            // Only while the limits are being ENFORCED. Past hinge_hold_s a
            // landed body keeps the pose it landed in on purpose, so an
            // inversion there is the design and not a defect — see
            // RagdollTuning::hinge_hold_s for why it has to be released at
            // all. Measured over the whole 700 steps instead, this reports
            // 0.400 m, and every metre of that is after the release.
            if (state.elapsed_s > tuning.hinge_hold_s) break;

            const glm::vec3 across =
                state.position[static_cast<std::size_t>(RagdollJoint::HipR)] -
                state.position[static_cast<std::size_t>(RagdollJoint::HipL)];
            const glm::vec3 up =
                state.position[static_cast<std::size_t>(RagdollJoint::Chest)] -
                state.position[static_cast<std::size_t>(RagdollJoint::Hips)];
            const glm::vec3 cross = glm::cross(across, up);
            if (glm::length(cross) < 1e-5f) continue;
            const glm::vec3 forward = glm::normalize(cross);

            for (const RagdollBulge& hinge : f.rig.figure.bulges) {
                // The SAME geometry the solver constrains: the joint's offset
                // from the limb axis, against the direction it should bulge.
                // Measuring against the chord midpoint instead would be a
                // different number that happens to share a sign, and a test
                // that agrees with the code only in sign is not pinning it.
                const glm::vec3 root = state.position[hinge.a];
                glm::vec3 axis = state.position[hinge.b] - root;
                const float span = glm::length(axis);
                if (span < 1e-5f) continue;
                axis /= span;
                const glm::vec3 centre =
                    root + axis * glm::dot(state.position[hinge.joint] - root,
                                           axis);
                const glm::vec3 radial = state.position[hinge.joint] - centre;
                const float radius = glm::length(radial);
                if (radius < 1e-4f) continue;  // limb straight: nothing to face
                glm::vec3 want = forward * hinge.sign;
                want -= axis * glm::dot(want, axis);
                const float want_len = glm::length(want);
                if (want_len < 1e-4f) continue;  // limb along the body axis
                want /= want_len;

                ++samples;
                // Airborne joints only, because that is the claim the solver
                // makes. A joint lying on the ground has its limit released on
                // purpose — see RagdollTuning::resting_clearance_m, which is
                // the whole fix for a body that would not stop twitching — so
                // asserting on one would be asserting against the design. The
                // resting figure is tracked and printed rather than dropped,
                // because it is the price of that fix and it should be
                // visible every time this runs.
                const float inversion = -glm::dot(radial, want);
                const float clearance =
                    state.position[hinge.joint].y -
                    f.rig.figure.radius_m[hinge.joint] -
                    ground_under(world, state.position[hinge.joint]);
                if (clearance < tuning.resting_clearance_m) {
                    worst_resting = std::max(worst_resting, inversion);
                    continue;
                }
                worst_inversion = std::max(worst_inversion, inversion);
                ++airborne;
                // HOW LONG a joint looks wrong, not how wrong its worst single
                // frame was. A body slamming into the road at 20 m/s spends a
                // frame or two with its geometry inside out before the limits
                // pull it back, and taking a maximum reports that transient as
                // if it were the resting state. What a player can actually see
                // is a knee that stays backwards.
                if (inversion > kVisiblyBackwards) ++backwards;
                // "Actually bent" means the joint is meaningfully off the limb
                // axis AND facing the right way. A limb held dead straight
                // satisfies the constraint for free and proves nothing.
                if (glm::dot(radial, want) > 0.04f) ++bent;
            }
        }
    }
    const double visible = 100.0 * static_cast<double>(backwards) /
                           static_cast<double>(std::max<std::size_t>(airborne, 1));
    std::printf("      %zu of %zu samples bent; %.2f%% of airborne joint-frames "
                "past %.2f m (peak %.4f m airborne, %.4f m resting)\n",
                bent, samples, visible,
                static_cast<double>(kVisiblyBackwards),
                static_cast<double>(worst_inversion),
                static_cast<double>(worst_resting));
    REQUIRE_MSG(bent > 200u,
                "no joint ever bent far enough for its direction to matter",
                "vacuity");
    // The limit is a TOLERANCE, not an equality: it leaves a joint alone
    // inside kHingeTolerance and turns a fraction of the remainder per
    // iteration, so a live tumble runs behind it. The peak is a transient at
    // the frame of impact and is reported above rather than asserted; what has
    // to hold is that a joint is almost never LEFT looking broken.
    REQUIRE_MSG(visible < 1.0,
                "knees and elbows spend a visible amount of time bent "
                "backwards", "hinge");
    pass("knees bulge forward and elbows bulge back, through every tumble");
}

// The shapes a body must never make.
//
// Both halves of this exist because both were seen in the running game before
// they were measured here: bodies "very twisted and bent and squashed". A
// figure of points and sticks has no volume, so nothing about it objects to a
// forearm inside a ribcage or a torso a fifth shorter than it was, and neither
// shows up in a bone-length check — every bone is exactly the right length the
// whole time.
void a_body_never_folds_into_a_shape_a_person_cannot_make() {
    TerrainCollider world = test_world();
    Fixture f;
    build(f, world);
    std::array<glm::vec3, kRagdollJointCount> start{};
    ragdoll_sample_pose(f.rig, f.pose, f.model_world, start);
    const RagdollTuning tuning;

    auto bind_gap = [&](RagdollJoint a, RagdollJoint b) {
        return glm::length(f.rig.bind_metres[static_cast<std::size_t>(b)] -
                           f.rig.bind_metres[static_cast<std::size_t>(a)]);
    };
    // The spans a squashed torso shortens. Measured against BIND, so the
    // number is "what fraction of this person is left".
    const std::pair<RagdollJoint, RagdollJoint> spans[] = {
        {RagdollJoint::Hips, RagdollJoint::Neck},
        {RagdollJoint::Hips, RagdollJoint::ClavicleL},
        {RagdollJoint::Hips, RagdollJoint::ClavicleR},
        {RagdollJoint::Chest, RagdollJoint::HipL},
        {RagdollJoint::Chest, RagdollJoint::HipR},
        {RagdollJoint::ClavicleL, RagdollJoint::HipR},
        {RagdollJoint::ClavicleR, RagdollJoint::HipL},
    };

    float worst_squash = 1.0f;
    float worst_overlap = 0.0f;
    const char* overlap_pair = "none";
    float most_bent = 1.0f;
    std::size_t samples = 0;
    Rng rng{hash_coord(0x5AFEC0DEull, 23, 5)};
    for (int trial = 0; trial < 8; ++trial) {
        RagdollState state;
        ragdoll_launch(state, f.rig.figure, start,
                       {rng.range(-18.0f, 18.0f), rng.range(0.0f, 6.0f),
                        rng.range(-18.0f, 18.0f)},
                       {rng.range(-8.0f, 8.0f), rng.range(-8.0f, 8.0f),
                        rng.range(-8.0f, 8.0f)});
        for (int step = 0; step < 700; ++step) {
            ragdoll_step(state, f.rig.figure, tuning, &world, nullptr, kDt);
            ++samples;
            for (const auto& span : spans) {
                const float now = glm::length(
                    state.position[static_cast<std::size_t>(span.second)] -
                    state.position[static_cast<std::size_t>(span.first)]);
                const float bind = bind_gap(span.first, span.second);
                if (bind > 1e-4f) worst_squash = std::min(worst_squash, now / bind);
            }
            // Every min-only constraint the rig declared, checked as a
            // constraint rather than as a list retyped here — a copy of the
            // table would agree with itself and with nothing else.
            for (const RagdollLink& link : f.rig.figure.links) {
                if (link.max_m < kRagdollUnbounded) continue;
                const float now = glm::length(state.position[link.b] -
                                              state.position[link.a]);
                if (link.min_m - now > worst_overlap) {
                    worst_overlap = link.min_m - now;
                    overlap_pair = ragdoll_joint_name(
                        static_cast<RagdollJoint>(link.a));
                }
            }
            // Vacuity: the spine has to actually curl, or a body that stayed
            // rigid passes both claims for the wrong reason.
            const float curl =
                glm::length(state.position[static_cast<std::size_t>(
                                RagdollJoint::Neck)] -
                            state.position[static_cast<std::size_t>(
                                RagdollJoint::Hips)]) /
                bind_gap(RagdollJoint::Hips, RagdollJoint::Neck);
            most_bent = std::min(most_bent, curl);
        }
    }

    std::printf("      over %zu samples: torso kept %.1f%% of its length, "
                "worst overlap %.4f m (%s)\n", samples,
                static_cast<double>(worst_squash * 100.0f),
                static_cast<double>(worst_overlap), overlap_pair);
    REQUIRE_MSG(most_bent < 0.995f,
                "the spine never curled at all, so no limit was ever tested",
                "vacuity");
    // The rig authors 0.88 as the tightest diagonal; anything materially past
    // that is the solver losing, not the limit allowing.
    REQUIRE_MSG(worst_squash > 0.85f,
                "the torso compressed past what the joint limits allow",
                "squash");
    REQUIRE_MSG(worst_overlap < 0.02f,
                "a limb passed through the body or through another limb",
                "overlap");
    pass("a tumbling body keeps its proportions and stays out of itself");
}

void the_same_fall_twice_is_the_same_fall() {
    TerrainCollider world = test_world();
    Fixture f;
    build(f, world, {41.0f, 19.0f});
    std::array<glm::vec3, kRagdollJointCount> start{};
    ragdoll_sample_pose(f.rig, f.pose, f.model_world, start);
    const RagdollTuning tuning;

    RagdollVehicle car;
    car.position = {41.0f, world.height(41.0f, 22.0f) + 0.7f, 22.0f};
    car.orientation = glm::angleAxis(0.35f, glm::vec3{0.0f, 1.0f, 0.0f});
    car.velocity = {0.0f, 0.0f, -14.0f};

    auto run = [&](std::vector<glm::vec3>& trace) {
        RagdollState state;
        ragdoll_launch(state, f.rig.figure, start, {2.0f, 2.5f, -13.0f},
                       {0.8f, -3.1f, 1.7f});
        RagdollVehicle moving = car;
        for (int step = 0; step < 400; ++step) {
            moving.position += moving.velocity * kDt;
            ragdoll_step(state, f.rig.figure, tuning, &world,
                         step < 40 ? &moving : nullptr, kDt);
            for (int j = 0; j < kRagdollJointCount; ++j)
                trace.push_back(state.position[static_cast<std::size_t>(j)]);
        }
    };

    std::vector<glm::vec3> a;
    std::vector<glm::vec3> b;
    run(a);
    run(b);
    REQUIRE_MSG(a.size() == b.size(), "the two runs were different lengths",
                "determinism");
    REQUIRE_MSG(!a.empty(), "nothing was traced", "vacuity");
    std::size_t differing = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) ++differing;
    REQUIRE_MSG(differing == 0,
                "the same launch produced a different fall", "determinism");

    // Vacuity: a trace of a body that never moved is bit-identical for the
    // wrong reason.
    float span = 0.0f;
    for (const glm::vec3& p : a) span = std::max(span, glm::length(p - a[0]));
    REQUIRE_MSG(span > 2.0f, "the body did not move, so nothing was compared",
                "vacuity");
    std::printf("      %zu traced positions bit-identical, %.2f m of motion\n",
                a.size(), static_cast<double>(span));
    pass("the same launch produces a bit-identical fall");
}

// The anchor moves the body without touching the body.
void the_sim_anchor_moves_the_figure_and_nothing_else() {
    TerrainCollider world = test_world();
    Fixture f;
    build(f, world);
    std::array<glm::vec3, kRagdollJointCount> start{};
    ragdoll_sample_pose(f.rig, f.pose, f.model_world, start);
    const RagdollTuning tuning;

    RagdollState state;
    ragdoll_launch(state, f.rig.figure, start, {5.0f, 3.0f, 2.0f},
                   {1.0f, 2.0f, 0.5f});
    for (int step = 0; step < 30; ++step)
        ragdoll_step(state, f.rig.figure, tuning, &world, nullptr, kDt);

    const auto before_pos = state.position;
    const auto before_vel = state.velocity;
    const float before_bones = longest_bone_error(f.rig, state);

    const glm::vec3 hips = state.centre();
    const glm::vec2 target{hips.x + 4.0f, hips.z - 3.0f};
    ragdoll_anchor_xz(state, target, 0.5f);

    // Half the error taken, exactly, on the pelvis.
    REQUIRE_NEAR(state.centre().x, hips.x + 2.0f, 1e-4f);
    REQUIRE_NEAR(state.centre().z, hips.z - 1.5f, 1e-4f);
    // Height untouched: the ground is the ragdoll's business, not the sim's.
    REQUIRE_NEAR(state.centre().y, hips.y, 1e-6f);

    // Rigid: every node moved by the SAME vector, so no constraint saw a
    // change. A per-node correction here would be a body being pulled apart
    // by its own bookkeeping.
    const glm::vec3 shift = state.position[0] - before_pos[0];
    float worst = 0.0f;
    for (int j = 0; j < kRagdollJointCount; ++j) {
        const std::size_t k = static_cast<std::size_t>(j);
        worst = std::max(worst,
                         glm::length((state.position[k] - before_pos[k]) - shift));
        // And no impulse: velocity is stored, not inferred, so a re-anchor is
        // invisible to the next step.
        REQUIRE_MSG(state.velocity[k] == before_vel[k],
                    "the anchor changed a node's velocity", "impulse");
    }
    REQUIRE_MSG(glm::length(shift) > 1.0f,
                "the anchor did not actually move anything", "vacuity");
    REQUIRE_MSG(worst < 1e-5f, "the anchor deformed the figure", "rigid");
    REQUIRE_NEAR(longest_bone_error(f.rig, state), before_bones, 1e-5f);

    std::printf("      moved %.2f m, worst per-node deviation %.7f m\n",
                static_cast<double>(glm::length(shift)),
                static_cast<double>(worst));
    pass("the sim anchor slides the whole figure and deforms nothing");
}

void a_car_carries_a_body_instead_of_driving_through_it() {
    TerrainCollider world = test_world();
    Fixture f;
    build(f, world);
    std::array<glm::vec3, kRagdollJointCount> start{};
    ragdoll_sample_pose(f.rig, f.pose, f.model_world, start);
    const RagdollTuning tuning;

    // A car already through the person, still moving. Nothing may end up
    // inside its footprint, and the body must go with it rather than being
    // left standing where the bumper found it.
    RagdollVehicle car;
    car.position = {0.0f, world.height(0.0f, 3.0f) + 0.7f, 3.0f};
    car.velocity = {0.0f, 0.0f, -12.0f};

    RagdollState state;
    ragdoll_launch(state, f.rig.figure, start, {0.0f, 1.5f, -6.0f},
                   {0.0f, 0.0f, 0.0f});
    const float start_z = state.centre().z;
    std::size_t inside = 0;
    for (int step = 0; step < 120; ++step) {
        car.position += car.velocity * kDt;
        ragdoll_step(state, f.rig.figure, tuning, &world, &car, kDt);
        for (int j = 0; j < kRagdollJointCount; ++j) {
            const std::size_t k = static_cast<std::size_t>(j);
            if (std::fabs(state.position[k].y - car.position.y) > car.height_m)
                continue;
            const BreakawayContact hit = breakaway_contact(
                car.position, car.orientation, car.half_extents,
                state.position[k], f.rig.figure.radius_m[k] * 0.85f);
            if (hit.hit) ++inside;
        }
    }
    const float moved = start_z - state.centre().z;
    REQUIRE_MSG(inside == 0u, "a node stayed inside the car's footprint",
                "penetration");
    REQUIRE_MSG(moved > 1.0f,
                "the car drove off and left the body where it stood", "carry");
    std::printf("      pushed clear every step; body carried %.2f m down the "
                "road\n", static_cast<double>(moved));
    pass("a moving car pushes a body clear and carries it along");
}

// The get-up crossfade: a settled ragdoll becoming a clip.
//
// src/app/ owns the fade itself, but everything that can go WRONG with it is
// here — it is a blend between two poses of a real skeleton, and the failure
// mode is a limb that shrinks on the way across. That is why the blend runs in
// parts and not on the matrices, and this measures the difference rather than
// asserting the comment.
void the_getup_crossfade_does_not_shear_the_body() {
    TerrainCollider world = test_world();
    Fixture f;
    build(f, world);
    std::array<glm::vec3, kRagdollJointCount> start{};
    ragdoll_sample_pose(f.rig, f.pose, f.model_world, start);
    const RagdollTuning tuning;

    // Land one, properly: whatever sprawl it comes to rest in is the pose the
    // fade has to start from.
    RagdollState state;
    ragdoll_launch(state, f.rig.figure, start, {11.0f, 3.4f, -4.0f},
                   {2.0f, -4.5f, 1.5f});
    int steps = 0;
    while (steps < 1200 && !ragdoll_settled(state, tuning)) {
        ragdoll_step(state, f.rig.figure, tuning, &world, nullptr, kDt);
        ++steps;
    }
    REQUIRE_MSG(ragdoll_settled(state, tuning), "the body never settled",
                "fixture");

    std::vector<glm::mat4> landed_local;
    ragdoll_bone_poses(f.rig, state, f.model_world, landed_local);

    // compose(decompose(x)) == x, which the fade assumes on both endpoints.
    std::vector<BonePose> parts;
    std::vector<glm::mat4> again;
    decompose_local_poses(landed_local, parts);
    compose_local_poses(parts, again);
    float round_trip = 0.0f;
    for (std::size_t b = 0; b < landed_local.size(); ++b)
        for (int col = 0; col < 4; ++col)
            round_trip = std::max(
                round_trip,
                glm::length(glm::vec3{again[b][col]} -
                            glm::vec3{landed_local[b][col]}));
    REQUIRE_MSG(round_trip < 1e-4f,
                "a pose did not survive being taken apart and put back "
                "together", "round-trip");

    // The target: the clip's pose. The bind pose stands in for it here for the
    // reason build() gives — the shipped .eanim clips are at the wrong scale
    // on this tree — and it is the right stand-in either way, because what is
    // being measured is a blend between two valid skeleton poses and bind is
    // as valid as any frame of stand_up.
    std::vector<BonePose> landed_parts, target_parts, mixed;
    decompose_local_poses(landed_local, landed_parts);
    decompose_local_poses(f.pose, target_parts);

    auto joints = [&](const std::vector<glm::mat4>& local,
                      std::array<glm::vec3, kRagdollJointCount>& out) {
        std::vector<glm::mat4> skin;
        f.skeleton.compute_skin_matrices(local, skin);
        REQUIRE(!skin.empty());
        for (int j = 0; j < kRagdollJointCount; ++j) {
            const std::size_t k = static_cast<std::size_t>(j);
            if (f.rig.bone[k] < 0) continue;
            const std::size_t b = static_cast<std::size_t>(f.rig.bone[k]);
            out[k] = glm::vec3{
                (f.model_world * skin[b] * f.rig.bind_world[b])[3]};
        }
    };

    // Shear, measured where shear actually shows: the world-space gap between
    // every SKELETON bone and its parent, against bind.
    //
    // Not the ragdoll's own links — most of those span more than one bone
    // (Chest to ShoulderL crosses the clavicle), so a blend legitimately
    // changes their length as that joint rotates, and a metric that counts it
    // reports 0.24 m of "stretch" for a blend that is behaving perfectly.
    // Parent-to-child cannot change for any honest rotation, which is what
    // makes it the one that answers the question.
    auto worst_stretch = [&](const std::vector<glm::mat4>& local) {
        std::vector<glm::mat4> world(local.size());
        for (uint32_t i : f.rig.order) {
            const std::size_t k = i;
            world[k] = f.rig.parent[k] < 0
                ? local[k]
                : world[static_cast<std::size_t>(f.rig.parent[k])] * local[k];
        }
        float worst = 0.0f;
        for (std::size_t b = 0; b < local.size(); ++b) {
            if (f.rig.parent[b] < 0) continue;
            const std::size_t p = static_cast<std::size_t>(f.rig.parent[b]);
            const float bind = glm::length(glm::vec3{f.rig.bind_world[b][3]} -
                                           glm::vec3{f.rig.bind_world[p][3]});
            // Bones the body is made of, not the knuckles. A finger segment is
            // a centimetre long, so a millimetre on it is 100% by this measure
            // and nothing at all on screen — the first version of this test
            // reported 109% and the bone was RightHandIndex2.
            if (bind < 0.04f * f.rig.figure.stature_m) continue;
            const float len = glm::length(glm::vec3{world[b][3]} -
                                          glm::vec3{world[p][3]});
            worst = std::max(worst, std::fabs(len - bind) / bind);
        }
        return worst;
    };

    std::array<glm::vec3, kRagdollJointCount> at{};
    std::array<glm::vec3, kRagdollJointCount> ends{};
    float worst_parts = 0.0f;
    float worst_matrix = 0.0f;
    std::vector<glm::mat4> naive(landed_local.size());
    for (int i = 0; i <= 20; ++i) {
        const float w = static_cast<float>(i) / 20.0f;

        ragdoll_getup_blend(landed_parts, target_parts, w, mixed);
        compose_local_poses(mixed, again);
        joints(again, at);
        for (int j = 0; j < kRagdollJointCount; ++j)
            REQUIRE_MSG(std::isfinite(at[static_cast<std::size_t>(j)].y),
                        "the blend produced a non-finite joint", "finite");
        worst_parts = std::max(worst_parts, worst_stretch(again));

        // The same fade done the obvious way, for the number. Element-wise on
        // the matrices is not a rotation: halfway between two poses the basis
        // vectors shorten, and every bone hanging off them comes with it.
        for (std::size_t b = 0; b < naive.size(); ++b)
            naive[b] = landed_local[b] * (1.0f - w) + f.pose[b] * w;
        worst_matrix = std::max(worst_matrix, worst_stretch(naive));

        if (i == 0) {
            std::array<glm::vec3, kRagdollJointCount> solved{};
            joints(landed_local, solved);
            for (int j = 0; j < kRagdollJointCount; ++j) {
                const std::size_t k = static_cast<std::size_t>(j);
                if (f.rig.bone[k] < 0) continue;
                REQUIRE_MSG(glm::length(at[k] - solved[k]) < 1e-3f,
                            "weight 0 is not the pose the body landed in",
                            "endpoint");
            }
        }
        if (i == 20) {
            joints(f.pose, ends);
            for (int j = 0; j < kRagdollJointCount; ++j) {
                const std::size_t k = static_cast<std::size_t>(j);
                if (f.rig.bone[k] < 0) continue;
                REQUIRE_MSG(glm::length(at[k] - ends[k]) < 1e-3f,
                            "weight 1 is not the clip's pose", "endpoint");
            }
        }
    }

    std::printf("      settled in %.2f s; worst bone stretch across the fade "
                "%.3f%% (element-wise on the matrices: %.3f%%)\n",
                static_cast<double>(static_cast<float>(steps) * kDt),
                static_cast<double>(worst_parts * 100.0f),
                static_cast<double>(worst_matrix * 100.0f));
    REQUIRE_MSG(worst_matrix > worst_parts * 4.0f,
                "the naive blend was not measurably worse, so this test is "
                "not comparing what it claims to", "vacuity");
    REQUIRE_MSG(worst_parts < 0.005f,
                "a bone changed length during the get-up crossfade",
                "shear");
    pass("the get-up crossfade holds every bone through the blend");
}

}  // namespace

int main() {
    std::printf("ragdoll_tests\n");
    the_rig_binds_to_every_shipped_civilian();
    the_bone_poses_reproduce_the_solved_figure();
    bones_do_not_stretch_under_a_car();
    a_body_comes_to_rest_on_the_ground();
    knees_and_elbows_bend_the_way_they_bend();
    a_body_never_folds_into_a_shape_a_person_cannot_make();
    the_same_fall_twice_is_the_same_fall();
    the_sim_anchor_moves_the_figure_and_nothing_else();
    a_car_carries_a_body_instead_of_driving_through_it();
    the_getup_crossfade_does_not_shear_the_body();
    return apricot_test::done("ragdoll_tests");
}
