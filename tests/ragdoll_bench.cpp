// Where does a ragdoll's motion actually GO?
//
// "It flops around too much" is a real bug report and a useless test, so this
// is the instrument that turns it into numbers. It drives twelve real falls
// over the real collider and splits every joint's travel into the part that
// happens on the way down and the part that happens after the body has
// landed — because the second part is the whole of what flopping is. Nobody
// objects to a body tumbling; they object to it twitching on the pavement
// afterwards.
//
// THE THREE NUMBERS THAT MATTER, and what they were before the solver was
// taught to let the ground win (see RagdollTuning::resting_clearance_m):
//
//   post-landing travel   72.1 m of 192 m (38%)  ->  20.9 m of 141 m (15%)
//   direction reversals   153.3 per second        ->  70.7 per second
//   time on the ground    2.25 s before settling  ->  1.88 s
//
// Both columns are measured HERE, every run, by driving the same twelve falls
// twice — once as shipped and once with the ground release switched off. A
// quoted number in a comment goes stale the first time somebody retunes
// gravity; a number the test produces cannot.
//
// A reversal is a joint changing direction by more than a right angle in one
// step. A limb settling makes a few; a limb being argued over by two
// constraints makes ten a second, and that is what the eye picks up.
//
// The bounds below are deliberately loose around the measured values — this
// is a guard against the pump coming back, not a golden file. If a change
// makes these numbers WORSE, it has made bodies twitch.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/asset_root.h"
#include "core/fixed_step.h"
#include "core/rng.h"
#include "core/skeletal_animation.h"
#include "physics/ragdoll.h"
#include "physics/ragdoll_rig.h"
#include "physics/terrain_collider.h"

#include "test_assert.h"

using namespace apricot;
using apricot_test::pass;

namespace {

constexpr float kDt = static_cast<float>(kSimDt);
constexpr char kModel[] = "models/characters/psx_pack/civilian_male_02/skin";
constexpr float kCharacterHeightM = 1.78f;
// A joint that turns by more than this in one step has reversed rather than
// curved. cos(115 degrees), so a glancing change of heading does not count.
constexpr float kReversalCos = -0.42f;
// Below this the pelvis is on the ground rather than still falling.
constexpr float kLandedHeightM = 0.30f;

struct Budget {
    double airborne_s = 0.0;
    double grounded_s = 0.0;
    std::array<double, kRagdollJointCount> total{};
    std::array<double, kRagdollJointCount> after{};
    std::array<long, kRagdollJointCount> reversals{};
    int falls = 0;
};

void measure(Budget& out, const RagdollTuning& tuning) {
    Skeleton skeleton;
    SkinnedEmesh mesh;
    REQUIRE_MSG(read_skinned_emesh(asset_path(std::string(kModel) + ".emesh"),
                                   mesh),
                "the shipped civilian mesh loads", "fixture");
    REQUIRE_MSG(skeleton.load(asset_path(std::string(kModel) + ".eskel")),
                "the shipped civilian skeleton loads", "fixture");
    const float source_height = mesh.bounds.size().y;
    REQUIRE(source_height > 1e-4f);
    const float scale = kCharacterHeightM / source_height;

    RagdollRig rig;
    REQUIRE_MSG(ragdoll_rig_build(skeleton, scale, rig), "the rig binds",
                "fixture");
    std::vector<glm::mat4> bind(static_cast<std::size_t>(skeleton.bone_count()));
    for (int i = 0; i < skeleton.bone_count(); ++i)
        bind[static_cast<std::size_t>(i)] = skeleton.bone(i).bind_local;

    TerrainCollider world(0x50454421ull);
    Rng rng{hash_coord(0x1707711ull, 7, 3)};

    for (int trial = 0; trial < 12; ++trial) {
        // Spread across the real map so no two falls land on the same slope.
        const glm::vec2 xz{30.0f + static_cast<float>(trial) * 6.0f, -40.0f};
        const float ground = world.height(xz.x, xz.y);
        const glm::vec3 where{xz.x, ground + 0.02f, xz.y};
        glm::mat4 local = glm::translate(
            glm::mat4{1.0f},
            glm::vec3{-mesh.bounds.center().x * scale,
                      -mesh.bounds.min.y * scale,
                      -mesh.bounds.center().z * scale});
        local *= glm::mat4_cast(
            glm::angleAxis(3.14159265358979f, glm::vec3{0.0f, 1.0f, 0.0f}));
        local = glm::scale(local, glm::vec3{scale});
        const glm::mat4 model_world =
            glm::translate(glm::mat4{1.0f}, where) * local;

        std::array<glm::vec3, kRagdollJointCount> start{};
        ragdoll_sample_pose(rig, bind, model_world, start);

        // The crowd's own launch split, so this measures what the game does.
        const float speed = rng.range(5.0f, 24.0f);
        const glm::vec2 dir = glm::normalize(
            glm::vec2{rng.unit_float(), rng.unit_float()});
        const glm::vec3 launch{dir.x * speed * 0.62f, speed * 0.30f,
                               dir.y * speed * 0.62f};
        const glm::vec3 across{-dir.y, 0.0f, dir.x};
        const glm::vec3 spin =
            across * rng.range(2.6f, 6.4f) +
            glm::vec3{rng.unit_float(), rng.unit_float(), rng.unit_float()} *
                2.2f;

        RagdollState state;
        ragdoll_launch(state, rig.figure, start, launch, spin);

        auto previous = state.position;
        std::array<glm::vec3, kRagdollJointCount> heading{};
        int landed = -1;
        int settled = -1;
        for (int step = 0; step < 1400; ++step) {
            ragdoll_step(state, rig.figure, tuning, &world, nullptr, kDt);
            for (int j = 0; j < kRagdollJointCount; ++j) {
                const std::size_t k = static_cast<std::size_t>(j);
                const glm::vec3 move = state.position[k] - previous[k];
                const double len = glm::length(move);
                out.total[k] += len;
                if (landed >= 0) {
                    out.after[k] += len;
                    if (len > 1e-5) {
                        const glm::vec3 now = move / static_cast<float>(len);
                        if (glm::dot(now, heading[k]) < kReversalCos)
                            ++out.reversals[k];
                        heading[k] = now;
                    }
                }
                previous[k] = state.position[k];
            }
            const glm::vec3 centre = state.centre();
            if (landed < 0 &&
                centre.y - world.height(centre.x, centre.z) < kLandedHeightM)
                landed = step;
            if (ragdoll_settled(state, tuning)) {
                settled = step;
                break;
            }
        }
        REQUIRE_MSG(settled >= 0, "a body never settled at all", "settle");
        if (landed < 0) landed = settled;
        out.airborne_s += static_cast<double>(landed) * kDt;
        out.grounded_s += static_cast<double>(settled - landed) * kDt;
        ++out.falls;
    }
}

struct Score {
    double travelled = 0.0;
    double after = 0.0;
    double after_pct = 0.0;
    double reversals_per_s = 0.0;
    double grounded_s = 0.0;
};

Score score(const Budget& b) {
    Score s;
    long reversals = 0;
    for (int j = 0; j < kRagdollJointCount; ++j) {
        const std::size_t k = static_cast<std::size_t>(j);
        s.travelled += b.total[k];
        s.after += b.after[k];
        reversals += b.reversals[k];
    }
    s.after_pct = 100.0 * s.after / s.travelled;
    s.reversals_per_s = static_cast<double>(reversals) / b.grounded_s;
    s.grounded_s = b.grounded_s / b.falls;
    s.travelled /= b.falls;
    s.after /= b.falls;
    return s;
}

void the_motion_budget_is_spent_falling_and_not_twitching() {
    Budget shipping;
    measure(shipping, RagdollTuning{});
    REQUIRE_MSG(shipping.falls == 12, "not every fall was measured",
                "vacuity");

    // The same twelve falls with the ground NOT outranking the joint limits,
    // which is what this solver used to do. Measured here rather than quoted
    // from a comment, so the claim stays true as the rest of the tuning moves.
    RagdollTuning pumping;
    pumping.resting_clearance_m = 0.0f;
    Budget before;
    measure(before, pumping);

    const Score now = score(shipping);
    const Score was = score(before);

    std::printf("      %d falls: %.2f s airborne, then %.2f s on the ground\n",
                shipping.falls, shipping.airborne_s / shipping.falls,
                now.grounded_s);
    std::printf("      %-11s %9s %9s %8s %s\n", "joint", "total m", "after m",
                "after %", "rev/s");
    for (int j = 0; j < kRagdollJointCount; ++j) {
        const std::size_t k = static_cast<std::size_t>(j);
        const std::string n(ragdoll_joint_name(static_cast<RagdollJoint>(j)));
        // The extremities and the pelvis: the ones a player watches, and the
        // one that says whether the body itself has stopped.
        if (n.find("wrist") == std::string::npos &&
            n.find("ankle") == std::string::npos &&
            n.find("knee") == std::string::npos &&
            n.find("elbow") == std::string::npos && n != "hips" && n != "head")
            continue;
        std::printf("      %-11s %9.2f %9.2f %7.0f%% %8.1f\n", n.c_str(),
                    shipping.total[k] / shipping.falls,
                    shipping.after[k] / shipping.falls,
                    100.0 * shipping.after[k] /
                        std::max(1e-9, shipping.total[k]),
                    static_cast<double>(shipping.reversals[k]) /
                        shipping.grounded_s);
    }
    std::printf("      SHIPPING: %.1f m travelled, %.1f m after landing "
                "(%.0f%%), %.1f reversals/s, %.2f s settling\n",
                now.travelled, now.after, now.after_pct, now.reversals_per_s,
                now.grounded_s);
    std::printf("      WITHOUT the resting release: %.1f m / %.1f m (%.0f%%), "
                "%.1f reversals/s, %.2f s\n",
                was.travelled, was.after, was.after_pct, was.reversals_per_s,
                was.grounded_s);

    REQUIRE_MSG(now.travelled > 40.0,
                "the bodies barely moved, so a low twitch score means nothing",
                "vacuity");
    REQUIRE_MSG(now.grounded_s > 0.05,
                "nothing spent any time on the ground, so nothing was measured",
                "vacuity");

    // Against the old behaviour, so a change that quietly restores the pump
    // fails here even if the absolute bounds below were loosened.
    REQUIRE_MSG(now.after < was.after * 0.55,
                "releasing the joint limits on the ground no longer makes a "
                "landed body meaningfully quieter", "causation");
    REQUIRE_MSG(now.reversals_per_s < was.reversals_per_s * 0.7,
                "the ground release no longer reduces twitching", "causation");

    // Absolute guards, with headroom over the measured values.
    REQUIRE_MSG(now.after_pct < 22.0,
                "too much of the motion happens after the body has landed",
                "flopping");
    REQUIRE_MSG(now.reversals_per_s < 95.0,
                "joints change direction too often once the body is down",
                "twitching");
    REQUIRE_MSG(now.grounded_s < 2.1,
                "bodies take too long to stop moving after they land",
                "settling");
    pass("a ragdoll spends its motion falling rather than twitching");
}

}  // namespace

int main() {
    std::printf("ragdoll_bench\n");
    the_motion_budget_is_spent_falling_and_not_twitching();
    return apricot_test::done("ragdoll_bench");
}
