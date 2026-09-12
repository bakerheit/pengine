// The character animation set: the lifted clips, the crossfade, and the state
// machine that decides which clip a character is in.
//
// Three things here are load-bearing and easy to delete by accident.
//
// The CROSSFADE test has a NEGATIVE CONTROL. It measures the largest per-frame
// vertex movement, through the real dual-quaternion skinning path, across an
// idle-to-sprint transition — and then measures the same transition with the
// fade length set to zero. Without the control every assertion still passes if
// the blend silently stopped running, because a smooth clip is smooth on its
// own; the control is what proves the fade is doing the work.
//
// The BLEND test measures FOREARM SEPARATION at every step of the fade. That is
// what separates blending the pose parts from lerping two matrices: matrix lerp
// is smooth, continuous, and pulls the midpoint short by centimetres, which
// reads as a rig bug a long way from the code that caused it.
//
// The PUNCH test counts contacts per swing. character_punch.h's whole reason
// for existing is that a jab lands exactly once, and "exactly once" is invisible
// to any check that only looks at the pose.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "app/character_animation.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/skeletal_animation.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kDt = 1.0f / 60.0f;
const char* const kPlayerRoot = "models/characters/psx_pack/player_male_01/skin";

struct Rig {
    SkinnedEmesh mesh;
    Skeleton skeleton;
    CharacterClipSet clips;
    float scale = 1.0f;
};

bool load_rig(const std::string& root, Rig& out) {
    if (!read_skinned_emesh(asset_path(root + ".emesh"), out.mesh)) return false;
    if (!out.skeleton.load(asset_path(root + ".eskel"))) return false;
    if (!out.skeleton.accepts(out.mesh)) return false;
    if (!out.clips.load(out.skeleton)) return false;
    out.scale = 1.76f / out.mesh.bounds.size().y;
    return true;
}

// The real skinning path, end to end: sample -> blend -> compose -> root policy
// -> skin matrices -> dual quaternions -> the CPU mirror of the vertex shader.
struct Skinner {
    std::vector<BonePose> parts_a;
    std::vector<BonePose> parts_b;
    std::vector<glm::mat4> local;
    std::vector<glm::mat4> skin;
    std::vector<glm::vec4> real;
    std::vector<glm::vec4> dual;

    void evaluate(const Rig& rig, const CharacterAnimSample& sample) {
        evaluate_character_pose(rig.clips, rig.skeleton, sample, parts_a,
                                parts_b, local);
        rig.skeleton.compute_skin_matrices(local, skin);
        skin_matrices_to_dual_quaternions(skin, real, dual);
    }

    void positions(const Rig& rig, std::vector<glm::vec3>& out) const {
        out.clear();
        out.reserve(rig.mesh.vertices.size());
        for (const EmeshSkinnedVertex& vertex : rig.mesh.vertices) {
            out.push_back(skinned_vertex_position(vertex, real, dual) *
                          rig.scale);
        }
    }
};

float largest_move_m(const std::vector<glm::vec3>& a,
                     const std::vector<glm::vec3>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    float largest = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        largest = std::max(largest, glm::length(b[i] - a[i]));
    }
    return largest;
}

// -- 1. every lifted clip binds to every staged rig ---------------------------

void every_lifted_clip_binds_to_every_staged_rig() {
    static constexpr std::array<const char*, 6> kRigs = {
        "player_male_01", "civilian_male_03", "civilian_female_09",
        "civilian_male_17_police", "police_male_17", "police_male_19",
    };
    for (const char* name : kRigs) {
        Rig rig;
        const std::string root =
            std::string("models/characters/psx_pack/") + name + "/skin";
        REQUIRE_MSG(load_rig(root, rig), "clip set binds to rig", name);
        for (const CharacterClipInfo& info : kCharacterClips) {
            const Animation& clip = rig.clips.clip(info.clip);
            REQUIRE_MSG(clip.unresolved_channels() == 0,
                        "every channel resolves to a bone", info.name);
            REQUIRE_MSG(clip.duration() > 0.05f, "clip has a usable duration",
                        info.name);
            REQUIRE_MSG(rig.clips.duration(info.clip) == clip.duration(),
                        "the cached duration is the clip's own", info.name);

            // Sample it for real. A clip that loads and then produces a
            // non-finite palette is worse than one that fails to load.
            Skinner skinner;
            CharacterAnimSample sample;
            sample.clip = info.clip;
            sample.from = info.clip;
            sample.root = info.root;
            sample.anchor_xz = rig.clips.anchor_xz(info.clip);
            for (int step = 0; step <= 8; ++step) {
                sample.time = clip.duration() *
                              static_cast<float>(step) / 8.0f * 0.999f;
                skinner.evaluate(rig, sample);
                REQUIRE_MSG(skinner.real.size() ==
                                static_cast<std::size_t>(rig.skeleton.bone_count()),
                            "palette covers the skeleton", info.name);
                for (std::size_t i = 0; i < skinner.real.size(); ++i) {
                    REQUIRE_MSG(std::isfinite(glm::length(skinner.real[i])),
                                "rotation part is finite", info.name);
                    REQUIRE_NEAR(glm::length(skinner.real[i]), 1.0f, 2e-4f);
                    REQUIRE_MSG(std::isfinite(glm::length(skinner.dual[i])),
                                "translation part is finite", info.name);
                }
            }
        }
    }
    std::printf("      %d clips x %zu rigs, every channel bound\n",
                kCharacterClipCount, kRigs.size());
    apricot_test::pass("every lifted clip loads and samples on every rig");
}

// -- 2. the parts form agrees with the matrix form ----------------------------

void sampling_parts_agrees_with_sampling_matrices() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));
    std::vector<BonePose> parts;
    std::vector<glm::mat4> composed;
    std::vector<glm::mat4> direct;
    float worst = 0.0f;
    for (const CharacterClipInfo& info : kCharacterClips) {
        const Animation& clip = rig.clips.clip(info.clip);
        for (int step = 0; step < 12; ++step) {
            const float time = clip.duration() *
                               static_cast<float>(step) / 12.0f;
            clip.sample_parts(time, rig.skeleton, parts);
            compose_local_poses(parts, composed);
            clip.sample(time, rig.skeleton, direct);
            REQUIRE(composed.size() == direct.size());
            for (std::size_t bone = 0; bone < direct.size(); ++bone) {
                for (int column = 0; column < 4; ++column) {
                    for (int row = 0; row < 4; ++row) {
                        worst = std::max(worst,
                            std::fabs(composed[bone][column][row] -
                                      direct[bone][column][row]));
                    }
                }
            }
        }
    }
    // sample() feeds every existing pose solver in src/app/; sample_parts()
    // feeds the crossfade. If they ever disagree, a fade would snap on its
    // first frame and nothing would say why.
    REQUIRE(worst < 1e-5f);
    std::printf("      worst parts-vs-matrix element delta %.3e\n",
                static_cast<double>(worst));
    apricot_test::pass("sample_parts composes to exactly what sample produces");
}

// -- 3. blending preserves limb length ----------------------------------------

void blending_parts_preserves_limb_length() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));
    const int hand = rig.skeleton.find_bone("mixamorig:RightHand");
    const int elbow = rig.skeleton.find_bone("mixamorig:RightForeArm");
    REQUIRE(hand >= 0 && elbow >= 0);

    const auto forearm_length = [&](const std::vector<glm::mat4>& skin) {
        const glm::mat4 hand_world = skin[static_cast<std::size_t>(hand)] *
            glm::inverse(rig.skeleton.bone(hand).inverse_bind);
        const glm::mat4 elbow_world = skin[static_cast<std::size_t>(elbow)] *
            glm::inverse(rig.skeleton.bone(elbow).inverse_bind);
        return glm::length(glm::vec3{hand_world[3]} -
                           glm::vec3{elbow_world[3]}) * rig.scale;
    };

    std::vector<BonePose> a;
    std::vector<BonePose> b;
    std::vector<BonePose> mixed;
    std::vector<glm::mat4> local;
    std::vector<glm::mat4> skin;

    // Two genuinely different arm poses: mid-jab and standing idle.
    rig.clips.clip(CharacterClip::PunchRight).sample_parts(0.45f, rig.skeleton, a);
    rig.clips.clip(CharacterClip::Idle).sample_parts(3.10f, rig.skeleton, b);

    compose_local_poses(a, local);
    rig.skeleton.compute_skin_matrices(local, skin);
    const float length_a = forearm_length(skin);
    compose_local_poses(b, local);
    rig.skeleton.compute_skin_matrices(local, skin);
    const float length_b = forearm_length(skin);
    REQUIRE(length_a > 0.05f && length_b > 0.05f);

    float worst_error = 0.0f;
    for (int step = 0; step <= 20; ++step) {
        const float weight = static_cast<float>(step) / 20.0f;
        blend_bone_poses(a, b, weight, mixed);
        compose_local_poses(mixed, local);
        rig.skeleton.compute_skin_matrices(local, skin);
        const float expected = length_a + (length_b - length_a) * weight;
        worst_error = std::max(worst_error,
                               std::fabs(forearm_length(skin) - expected));
    }
    // The joint separation must track the linear interpolation of its two
    // endpoints. Element-wise matrix lerp does not: it shears the basis and
    // pulls the midpoint SHORT by centimetres, which reads as a rig bug rather
    // than as a blending bug.
    REQUIRE(worst_error < 1.5e-3f);

    // The endpoints must be the inputs exactly, or a finished fade is not the
    // same pose as no fade at all and every state change ends on a small step.
    blend_bone_poses(a, b, 0.0f, mixed);
    for (std::size_t i = 0; i < mixed.size(); ++i) {
        REQUIRE_NEAR(glm::length(mixed[i].translation - a[i].translation),
                     0.0f, 1e-6f);
    }
    blend_bone_poses(a, b, 1.0f, mixed);
    for (std::size_t i = 0; i < mixed.size(); ++i) {
        REQUIRE_NEAR(glm::length(mixed[i].translation - b[i].translation),
                     0.0f, 1e-6f);
    }
    std::printf("      forearm %.4f m -> %.4f m, worst blend error %.4f mm\n",
                static_cast<double>(length_a), static_cast<double>(length_b),
                static_cast<double>(worst_error * 1000.0f));
    apricot_test::pass("blended parts interpolate joints without shearing them");
}

// -- 4. the crossfade removes the pop, with a negative control ----------------

// Worst per-frame vertex movement over `frames`, in metres on the displayed
// 1.76 m character. `transition` decides whether the run starts from a settled
// idle (so the state change is inside the window) or from a settled sprint (so
// it is not, and the number is the clip's own motion).
float worst_frame_step_m(Rig& rig, float fade_seconds, bool transition) {
    CharacterAnimTuning tuning;
    tuning.fade_idle_s = fade_seconds;
    tuning.fade_locomotion_s = fade_seconds;

    CharacterAnimator animator;
    CharacterAnimInput input;
    input.identity = 0x51DEF00Dull;
    if (!transition) {
        input.speed_mps = 6.25f;
        input.sprinting = true;
    }

    Skinner skinner;
    std::vector<glm::vec3> previous;
    std::vector<glm::vec3> current;

    // Settle first, so the only event inside the measured window is the one
    // being measured.
    for (int frame = 0; frame < 180; ++frame) {
        animator.advance(rig.clips, input, kDt, tuning);
    }
    skinner.evaluate(rig, animator.sample());
    skinner.positions(rig, previous);

    input.speed_mps = 6.25f;
    input.sprinting = true;
    float worst = 0.0f;
    for (int frame = 0; frame < 60; ++frame) {
        animator.advance(rig.clips, input, kDt, tuning);
        skinner.evaluate(rig, animator.sample());
        skinner.positions(rig, current);
        worst = std::max(worst, largest_move_m(previous, current));
        previous.swap(current);
    }
    return worst;
}

void crossfading_removes_the_state_change_pop() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));
    // The yardstick: a full-speed sprint already moves a hand tens of
    // millimetres per frame. A transition is only a POP if it moves one further
    // than the clip does on its own, which is why an absolute threshold here
    // would be measuring the sprint rather than the fade.
    const float steady = worst_frame_step_m(rig, 0.18f, false);
    const float faded = worst_frame_step_m(rig, 0.18f, true);
    const float snapped = worst_frame_step_m(rig, 0.0f, true);
    REQUIRE(steady > 0.005f);

    // THE NEGATIVE CONTROL. Without it, a blend that silently stopped running
    // still passes every other assertion in this file, because an idle clip and
    // a sprint clip are each perfectly smooth on their own.
    REQUIRE(snapped > steady * 1.5f);
    REQUIRE(faded < snapped * 0.6f);
    REQUIRE(faded < steady * 1.15f);
    std::printf("      idle->sprint worst frame step: %.1f mm faded, "
                "%.1f mm snapped, %.1f mm steady sprint (%.1fx pop removed)\n",
                static_cast<double>(faded * 1000.0f),
                static_cast<double>(snapped * 1000.0f),
                static_cast<double>(steady * 1000.0f),
                static_cast<double>(snapped / std::max(faded, 1e-6f)));
    apricot_test::pass("the crossfade measurably removes the transition pop");
}

// -- 5. the state machine ------------------------------------------------------

void run_frames(CharacterAnimator& animator, const CharacterClipSet& clips,
                const CharacterAnimInput& input, int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        animator.advance(clips, input, kDt);
    }
}

void the_state_machine_reaches_every_state() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));
    CharacterAnimator animator;
    CharacterAnimInput input;
    input.identity = 0x0A11CEull;

    run_frames(animator, rig.clips, input, 4);
    REQUIRE(animator.state() == CharacterAnimState::Idle);
    REQUIRE(animator.sample().clip == CharacterClip::Idle);

    input.speed_mps = 1.6f;
    run_frames(animator, rig.clips, input, 4);
    REQUIRE(animator.state() == CharacterAnimState::Walk);
    REQUIRE(animator.sample().clip == CharacterClip::Walk);

    input.speed_mps = 6.25f;
    input.sprinting = true;
    run_frames(animator, rig.clips, input, 4);
    REQUIRE(animator.state() == CharacterAnimState::Run);
    REQUIRE(animator.sample().clip == CharacterClip::Sprint);

    // Armed swaps the whole locomotion set, not just the upper body.
    input.armed = true;
    run_frames(animator, rig.clips, input, 4);
    REQUIRE(animator.sample().clip == CharacterClip::PistolRun);
    input.speed_mps = 1.6f;
    input.sprinting = false;
    run_frames(animator, rig.clips, input, 4);
    REQUIRE(animator.sample().clip == CharacterClip::PistolWalk);
    input.speed_mps = 0.0f;
    run_frames(animator, rig.clips, input, 4);
    REQUIRE(animator.sample().clip == CharacterClip::PistolIdle);
    input.armed = false;

    // Airborne.
    input.grounded = false;
    run_frames(animator, rig.clips, input, 4);
    REQUIRE(animator.state() == CharacterAnimState::Jump);
    REQUIRE(animator.sample().clip == CharacterClip::Jump);
    input.grounded = true;
    run_frames(animator, rig.clips, input, 90);
    REQUIRE(animator.state() == CharacterAnimState::Idle);

    // A climb is AIRBORNE but is not a jump. It has to win over the air check,
    // or a character crossing a fence plays a star-jump up the side of it.
    input.grounded = false;
    input.climbing = true;
    input.climb_progress = 0.0f;
    animator.advance(rig.clips, input, kDt);
    REQUIRE(animator.state() == CharacterAnimState::Climb);
    REQUIRE(animator.sample().clip == CharacterClip::Climb);
    REQUIRE_NEAR(animator.sample().time, 0.0f, 1e-6f);
    // And its clock is the TRAVERSE, not dt. Thirty frames with the climb
    // standing still must leave the clip standing still too — game/climb.cpp
    // owns when the body reaches the ledge, and a second clock here would
    // arrive somewhere else and slide the hands off the wall.
    run_frames(animator, rig.clips, input, 30);
    REQUIRE_NEAR(animator.sample().time, 0.0f, 1e-6f);
    input.climb_progress = 0.5f;
    animator.advance(rig.clips, input, kDt);
    REQUIRE_NEAR(animator.sample().time,
                 rig.clips.duration(CharacterClip::Climb) * 0.5f, 1e-4f);
    input.climbing = false;
    input.climb_progress = 0.0f;
    input.grounded = true;
    run_frames(animator, rig.clips, input, 90);
    REQUIRE(animator.state() == CharacterAnimState::Idle);

    // A flinch is a stagger, not a knockdown: it recovers on its own and it
    // never reaches the ground.
    input.flinch = true;
    animator.advance(rig.clips, input, kDt);
    REQUIRE(animator.state() == CharacterAnimState::Flinch);
    REQUIRE(animator.sample().clip == CharacterClip::HitByCar);
    input.flinch = false;
    int flinch_frames = 1;
    while (animator.state() == CharacterAnimState::Flinch && flinch_frames < 600) {
        animator.advance(rig.clips, input, kDt);
        ++flinch_frames;
    }
    REQUIRE(flinch_frames < 60);
    REQUIRE(animator.state() == CharacterAnimState::Idle);

    // Knockdown -> prone hold -> get up -> back on your feet.
    input.downed = true;
    animator.advance(rig.clips, input, kDt);
    REQUIRE(animator.state() == CharacterAnimState::Knockdown);
    run_frames(animator, rig.clips, input, 600);
    REQUIRE(animator.state() == CharacterAnimState::Downed);
    const float held = animator.sample().time;
    run_frames(animator, rig.clips, input, 60);
    // Prone means STILL. A held frame that keeps advancing loops the fall.
    REQUIRE_NEAR(animator.sample().time, held, 1e-6f);

    input.downed = false;
    animator.advance(rig.clips, input, kDt);
    REQUIRE(animator.state() == CharacterAnimState::GetUp);
    REQUIRE(animator.sample().clip == CharacterClip::StandUp);
    int getup_frames = 1;
    while (animator.state() == CharacterAnimState::GetUp && getup_frames < 1200) {
        animator.advance(rig.clips, input, kDt);
        ++getup_frames;
    }
    REQUIRE(animator.state() == CharacterAnimState::Idle);
    std::printf("      flinch %d frames, get-up %d frames\n", flinch_frames,
                getup_frames);

    // Death is terminal and it picks its side from the impact direction.
    input.dead = true;
    animator.advance(rig.clips, input, kDt);
    REQUIRE(animator.state() == CharacterAnimState::Die);
    REQUIRE(animator.sample().clip == CharacterClip::DieForward);
    run_frames(animator, rig.clips, input, 900);
    REQUIRE(animator.state() == CharacterAnimState::Downed);
    const float corpse = animator.sample().time;
    run_frames(animator, rig.clips, input, 300);
    REQUIRE(animator.state() == CharacterAnimState::Downed);
    REQUIRE_NEAR(animator.sample().time, corpse, 1e-6f);

    CharacterAnimator backwards;
    CharacterAnimInput hit_from_front = input;
    hit_from_front.impact_from_front = true;
    backwards.advance(rig.clips, hit_from_front, kDt);
    REQUIRE(backwards.sample().clip == CharacterClip::DieBackward);

    apricot_test::pass("every state is reachable and every one-shot terminates");
}

// -- 6. the punch, and its one-shot hit latch ---------------------------------

void the_punch_lands_exactly_once_and_alternates_fists() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));
    CharacterAnimator animator;
    CharacterAnimInput input;
    input.identity = 0xBEEFull;
    run_frames(animator, rig.clips, input, 10);

    std::array<CharacterClip, 4> sides{};
    std::array<int, 4> frames{};
    for (int swing = 0; swing < 4; ++swing) {
        input.punch = true;
        animator.advance(rig.clips, input, kDt);
        input.punch = false;   // a jab is an edge, never an auto-repeat
        REQUIRE(animator.state() == CharacterAnimState::Punch);
        sides[static_cast<std::size_t>(swing)] = animator.sample().clip;

        int contacts = animator.consume_punch_contact() ? 1 : 0;
        int count = 1;
        while (animator.state() == CharacterAnimState::Punch && count < 600) {
            animator.advance(rig.clips, input, kDt);
            if (animator.consume_punch_contact()) ++contacts;
            ++count;
        }
        frames[static_cast<std::size_t>(swing)] = count;
        // EXACTLY once. Not "at least once": the fist is extended for a dozen
        // frames and a latch that re-arms hits a pedestrian a dozen times.
        REQUIRE_MSG(contacts == 1, "one contact per swing", "punch");
        REQUIRE(count < 60);   // ~0.44 s at 60 Hz; a jab must feel like a jab
        run_frames(animator, rig.clips, input, 20);
    }
    REQUIRE(sides[0] == CharacterClip::PunchRight);
    REQUIRE(sides[1] == CharacterClip::PunchLeft);
    REQUIRE(sides[2] == CharacterClip::PunchRight);
    REQUIRE(sides[3] == CharacterClip::PunchLeft);

    // Holding the button down is one jab, not a stream of them.
    CharacterAnimator held;
    CharacterAnimInput holding;
    holding.identity = 0xBEEFull;
    holding.punch = true;
    int held_contacts = 0;
    for (int frame = 0; frame < 240; ++frame) {
        held.advance(rig.clips, holding, kDt);
        if (held.consume_punch_contact()) ++held_contacts;
    }
    REQUIRE(held_contacts == 1);

    // Death outranks a swing in progress.
    CharacterAnimator interrupted;
    CharacterAnimInput fatal;
    fatal.identity = 7ull;
    fatal.punch = true;
    interrupted.advance(rig.clips, fatal, kDt);
    REQUIRE(interrupted.state() == CharacterAnimState::Punch);
    fatal.punch = false;
    fatal.dead = true;
    interrupted.advance(rig.clips, fatal, kDt);
    REQUIRE(interrupted.state() == CharacterAnimState::Die);

    std::printf("      swings %d/%d/%d/%d frames, right-left-right-left\n",
                frames[0], frames[1], frames[2], frames[3]);
    apricot_test::pass("one jab, one contact, alternating fists");
}

// -- 6a. a press that releases inside one frame is still a press ---------------

void a_single_frame_press_still_throws_a_jab() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));

    // THE MEASURED FAILURE. The event loop and the animator run at different
    // rates, so a down-then-up inside one render frame leaves a plain bool
    // false by the time anything looks at it, and the jab silently never
    // happens. Reproduced in the running game with a synthesised key pair
    // before LatchedPress existed: every guard passed, nothing was logged, and
    // the character just stood there.
    LatchedPress plain;
    plain.set(true);
    plain.set(false);
    REQUIRE(plain.consume());          // the press survived the same-frame release
    REQUIRE(!plain.consume());         // and it is not still pending afterwards

    // Held across frames reads as one continuous level, so the animator's edge
    // detection still gives one jab rather than an auto-repeat.
    LatchedPress holding;
    holding.set(true);
    REQUIRE(holding.consume());
    REQUIRE(holding.consume());
    holding.set(false);
    REQUIRE(!holding.consume());

    // Now drive the real animator through it: one frame of press, released in
    // the same frame, must produce exactly one swing and exactly one contact.
    CharacterAnimator animator;
    CharacterAnimInput input;
    input.identity = 0xFEEDull;
    LatchedPress button;
    run_frames(animator, rig.clips, input, 30);

    button.set(true);
    button.set(false);
    int swings = 0;
    int contacts = 0;
    bool was_punching = false;
    for (int frame = 0; frame < 120; ++frame) {
        input.punch = button.consume();
        animator.advance(rig.clips, input, kDt);
        if (!was_punching && animator.punching()) ++swings;
        was_punching = animator.punching();
        if (animator.consume_punch_contact()) ++contacts;
    }
    REQUIRE(swings == 1);
    REQUIRE(contacts == 1);

    button.clear();
    REQUIRE(!button.consume());
    apricot_test::pass("a press and release in one frame still lands one jab");
}

// -- 6b. the contact window lands on the extended fist -------------------------

void the_contact_window_lands_on_an_extended_fist() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));
    const CharacterAnimTuning tuning;
    const int hips = rig.skeleton.find_bone("mixamorig:Hips");
    REQUIRE(hips >= 0);

    std::vector<BonePose> a;
    std::vector<BonePose> b;
    std::vector<glm::mat4> local;
    std::vector<glm::mat4> skin;

    for (int side = 0; side < 2; ++side) {
        const bool right = side == 0;
        const CharacterClip clip = right ? CharacterClip::PunchRight
                                         : CharacterClip::PunchLeft;
        const int fist = rig.skeleton.find_bone(
            right ? "mixamorig:RightHand" : "mixamorig:LeftHand");
        REQUIRE(fist >= 0);
        const character_punch::ContactWindow window =
            CharacterAnimator::punch_contact_window(rig.clips, tuning, right);
        const float end = character_punch::effective_end(
            rig.clips.duration(clip), tuning.punch_tail_trim_s);
        REQUIRE(window.hi > window.lo);
        REQUIRE(window.hi <= end);

        const auto reach_at = [&](float time) {
            CharacterAnimSample sample;
            sample.clip = clip;
            sample.from = clip;
            sample.time = time;
            sample.root = character_clip_info(clip).root;
            sample.anchor_xz = rig.clips.anchor_xz(clip);
            evaluate_character_pose(rig.clips, rig.skeleton, sample, a, b, local);
            rig.skeleton.compute_skin_matrices(local, skin);
            const auto joint = [&](int bone) {
                return glm::vec3{skin[static_cast<std::size_t>(bone)] *
                    glm::inverse(rig.skeleton.bone(bone).inverse_bind) *
                    glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
            };
            return glm::length(joint(fist) - joint(hips)) * rig.scale;
        };

        // Where the fist ACTUALLY is furthest from the hips, over the clip.
        float peak_reach = 0.0f;
        float peak_time = 0.0f;
        float guard_reach = reach_at(0.0f);
        for (int step = 0; step <= 60; ++step) {
            const float time = end * static_cast<float>(step) / 60.0f;
            const float reach = reach_at(time);
            if (reach > peak_reach) {
                peak_reach = reach;
                peak_time = time;
            }
            guard_reach = std::min(guard_reach, reach);
        }
        // THE WINDOW MUST CONTAIN THE PUNCH. The two jabs peak at different
        // fractions of their own clip (0.60 for the right, 0.75 for the left),
        // and the left one is at its most RETRACTED near 0.45 — so a single
        // shared window puts the hit on a fist tucked at the chest. Nothing
        // about the animation looks wrong when that happens, which is exactly
        // why this is measured rather than eyeballed.
        REQUIRE_MSG(character_punch::is_contact(peak_time, window.lo, window.hi),
                    "peak extension falls inside the contact window",
                    character_clip_name(clip));
        REQUIRE(peak_reach > guard_reach * 1.25f);
        // And every frame of the window is a genuinely extended arm, not just
        // the one peak sample.
        for (int step = 0; step <= 8; ++step) {
            const float time = window.lo + (window.hi - window.lo) *
                               static_cast<float>(step) / 8.0f;
            REQUIRE_MSG(reach_at(std::min(time, end)) > peak_reach * 0.80f,
                        "the fist stays extended across the window",
                        character_clip_name(clip));
        }
        std::printf("      %s: peak reach %.3f m at %.3f s (guard %.3f m), "
                    "window [%.3f, %.3f)\n",
                    character_clip_name(clip),
                    static_cast<double>(peak_reach),
                    static_cast<double>(peak_time),
                    static_cast<double>(guard_reach),
                    static_cast<double>(window.lo),
                    static_cast<double>(window.hi));
    }
    apricot_test::pass("a jab registers while the fist is out, not tucked in");
}

// -- 7. determinism and idle variety ------------------------------------------

void idle_variety_is_deterministic_and_not_in_lockstep() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));

    const auto idle_trace = [&](uint64_t identity, std::vector<float>& out) {
        CharacterAnimator animator;
        CharacterAnimInput input;
        input.identity = identity;
        out.clear();
        for (int frame = 0; frame < 3600; ++frame) {
            animator.advance(rig.clips, input, kDt);
            if (frame % 30 == 0) out.push_back(animator.sample().time);
        }
    };

    std::vector<float> a;
    std::vector<float> b;
    std::vector<float> a_again;
    idle_trace(0x1111ull, a);
    idle_trace(0x2222ull, b);
    idle_trace(0x1111ull, a_again);

    // Same identity, same inputs, same result. hash_coord(), not a stream.
    REQUIRE(a.size() == a_again.size());
    for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == a_again[i]);

    // Two people are never in the same part of the idle at the same moment.
    REQUIRE(a.size() == b.size());
    float smallest_separation = 1e9f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        smallest_separation = std::min(smallest_separation,
                                       std::fabs(a[i] - b[i]));
    }
    REQUIRE(smallest_separation > 0.05f);

    // And one person does not sit in the same loop for a minute: the idle
    // breaks move them to a different part of the clip.
    float span_low = a[0];
    float span_high = a[0];
    for (const float value : a) {
        span_low = std::min(span_low, value);
        span_high = std::max(span_high, value);
    }
    REQUIRE(span_high - span_low > 1.0f);
    std::printf("      idle phase span %.2f s, closest two-character "
                "separation %.2f s over 60 s\n",
                static_cast<double>(span_high - span_low),
                static_cast<double>(smallest_separation));
    apricot_test::pass("idle variety is hash-derived, repeatable and unsynced");
}

// -- 8. a get-up ends standing, and a corpse does not -------------------------

// Highest skinned vertex, in metres above the character's own root plane. Head
// height is the cheapest honest answer to "is this person upright".
float head_height_m(Rig& rig, Skinner& skinner) {
    float highest = -1e9f;
    for (const EmeshSkinnedVertex& vertex : rig.mesh.vertices) {
        highest = std::max(highest,
            skinned_vertex_position(vertex, skinner.real, skinner.dual).y);
    }
    return (highest - rig.mesh.bounds.min.y) * rig.scale;
}

void the_get_up_ends_standing_and_the_dead_stay_down() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));
    Skinner skinner;
    CharacterAnimator animator;
    CharacterAnimInput input;
    input.identity = 0xC0FFEEull;

    run_frames(animator, rig.clips, input, 60);
    skinner.evaluate(rig, animator.sample());
    const float standing = head_height_m(rig, skinner);
    REQUIRE(standing > 1.4f);

    // Knock them down, wait out the fall, then let them up.
    input.downed = true;
    run_frames(animator, rig.clips, input, 600);
    REQUIRE(animator.state() == CharacterAnimState::Downed);
    skinner.evaluate(rig, animator.sample());
    const float prone = head_height_m(rig, skinner);
    // A body on the ground has its head near the ground. If this ever passes at
    // standing height the root anchor has drifted and the character is lying
    // down in mid-air.
    REQUIRE(prone < 0.75f);

    input.downed = false;
    float lowest_during_getup = 1e9f;
    float last = 0.0f;
    int frames = 0;
    while (frames < 1200) {
        animator.advance(rig.clips, input, kDt);
        skinner.evaluate(rig, animator.sample());
        last = head_height_m(rig, skinner);
        lowest_during_getup = std::min(lowest_during_getup, last);
        ++frames;
        if (animator.state() != CharacterAnimState::GetUp) break;
    }
    // THE TRIM IS A CLAIM, AND THIS IS WHERE IT GETS CHECKED. character_getup.h
    // trims a settle tail off the clip so recovery feels reactive. Trim too much
    // and the state machine hands control back while the character is still
    // doubled over, and the 0.24 s fade to idle has to snap them upright.
    // Measured on the cooked stand_up clip, not assumed from the clip's length.
    REQUIRE(lowest_during_getup < 0.9f);          // they really did get up off the floor
    REQUIRE(std::fabs(last - standing) < 0.16f);  // and they finish on their feet

    // THE FLINCH WINDOW IS A CLAIM TOO, and the obvious window is the wrong
    // one. hit_by_car opens on a standing brace that does not move for 0.6 s,
    // so a flinch taken from the start of the clip plays a stagger in which
    // nothing staggers — and every assertion about states and timing still
    // passes while the player sees no reaction at all.
    CharacterAnimator staggering;
    CharacterAnimInput hit;
    hit.identity = 0xC0FFEEull;
    run_frames(staggering, rig.clips, hit, 60);
    skinner.evaluate(rig, staggering.sample());
    const float before = head_height_m(rig, skinner);
    hit.flinch = true;
    staggering.advance(rig.clips, hit, kDt);
    hit.flinch = false;
    float flinch_low = before;
    int flinch_frames = 1;
    while (staggering.state() == CharacterAnimState::Flinch &&
           flinch_frames < 600) {
        staggering.advance(rig.clips, hit, kDt);
        skinner.evaluate(rig, staggering.sample());
        flinch_low = std::min(flinch_low, head_height_m(rig, skinner));
        ++flinch_frames;
    }
    REQUIRE(before - flinch_low > 0.05f);   // it visibly rocks the character
    REQUIRE(flinch_low > 1.20f);            // and it never puts them down
    REQUIRE(flinch_frames < 40);            // a stagger, not a lie-down

    // Death is the opposite claim: the held frame must stay on the ground.
    CharacterAnimator dying;
    CharacterAnimInput fatal;
    fatal.identity = 0xC0FFEEull;
    fatal.dead = true;
    run_frames(dying, rig.clips, fatal, 900);
    REQUIRE(dying.state() == CharacterAnimState::Downed);
    skinner.evaluate(rig, dying.sample());
    const float corpse = head_height_m(rig, skinner);
    REQUIRE(corpse < 0.65f);

    std::printf("      head height: %.2f m standing, %.2f m prone, %.2f m at "
                "end of get-up, %.2f m flinch low (%d frames), %.2f m dead\n",
                static_cast<double>(standing), static_cast<double>(prone),
                static_cast<double>(last), static_cast<double>(flinch_low),
                flinch_frames, static_cast<double>(corpse));
    apricot_test::pass("a get-up finishes upright and a corpse stays down");
}

// -- 9. root policy ------------------------------------------------------------

void one_shot_clips_land_where_the_character_stands() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));
    std::vector<BonePose> scratch_a;
    std::vector<BonePose> scratch_b;
    std::vector<glm::mat4> local;

    glm::vec2 bind_xz{0.0f};
    for (int bone = 0; bone < rig.skeleton.bone_count(); ++bone) {
        if (rig.skeleton.bone(bone).parent >= 0) continue;
        bind_xz = glm::vec2{rig.skeleton.bone(bone).bind_local[3].x,
                            rig.skeleton.bone(bone).bind_local[3].z};
        break;
    }

    // A get-up must FINISH standing where the game thinks the character is.
    // Anchoring it at t=0 instead puts the root at standing height while the
    // pose is still lying down, and floats the body a body-length up.
    CharacterAnimSample getup;
    getup.clip = CharacterClip::StandUp;
    getup.from = CharacterClip::StandUp;
    getup.root = ClipRoot::AnchorEnd;
    getup.anchor_xz = rig.clips.anchor_xz(CharacterClip::StandUp);
    getup.time = std::max(0.0f, rig.clips.duration(CharacterClip::StandUp) -
                                    character_getup::SAMPLE_EPS);
    evaluate_character_pose(rig.clips, rig.skeleton, getup, scratch_a, scratch_b,
                            local);
    const glm::vec2 landed = root_translation_xz(rig.skeleton, local);
    REQUIRE_NEAR(landed.x, bind_xz.x, 1e-4f);
    REQUIRE_NEAR(landed.y, bind_xz.y, 1e-4f);

    // A fall STARTS where the character was standing and then travels. Both
    // halves matter: pinning it to bind drops the body straight down like a
    // puppet with its strings cut.
    CharacterAnimSample fall;
    fall.clip = CharacterClip::DieForward;
    fall.from = CharacterClip::DieForward;
    fall.root = ClipRoot::AnchorStart;
    fall.anchor_xz = rig.clips.anchor_xz(CharacterClip::DieForward);
    fall.time = 0.0f;
    evaluate_character_pose(rig.clips, rig.skeleton, fall, scratch_a, scratch_b,
                            local);
    const glm::vec2 start = root_translation_xz(rig.skeleton, local);
    REQUIRE_NEAR(start.x, bind_xz.x, 1e-4f);
    REQUIRE_NEAR(start.y, bind_xz.y, 1e-4f);

    fall.time = rig.clips.duration(CharacterClip::DieForward) * 0.95f;
    evaluate_character_pose(rig.clips, rig.skeleton, fall, scratch_a, scratch_b,
                            local);
    const glm::vec2 landed_fall = root_translation_xz(rig.skeleton, local);
    const float travel = glm::length(landed_fall - start) * rig.scale;
    REQUIRE(travel > 0.10f);

    // Locomotion is the opposite rule: the game moved the character, so the
    // clip must not move it again.
    CharacterAnimSample walk;
    walk.clip = CharacterClip::Walk;
    walk.from = CharacterClip::Walk;
    walk.root = ClipRoot::Strip;
    for (int step = 0; step < 8; ++step) {
        walk.time = rig.clips.duration(CharacterClip::Walk) *
                    static_cast<float>(step) / 8.0f;
        evaluate_character_pose(rig.clips, rig.skeleton, walk, scratch_a,
                                scratch_b, local);
        const glm::vec2 root = root_translation_xz(rig.skeleton, local);
        REQUIRE_NEAR(root.x, bind_xz.x, 1e-5f);
        REQUIRE_NEAR(root.y, bind_xz.y, 1e-5f);
    }
    std::printf("      death carries the body %.3f m forward; get-up lands on "
                "bind; walk never moves the root\n",
                static_cast<double>(travel));
    apricot_test::pass("root policy: locomotion strips, one-shots anchor");
}

void jump_uses_the_pose_without_authored_vertical_travel() {
    Rig rig;
    REQUIRE(load_rig(kPlayerRoot, rig));
    CharacterAnimator animator;
    CharacterAnimInput input;
    input.grounded = false;
    std::vector<BonePose> a, b;
    std::vector<glm::mat4> local;
    for (int i = 0; i < 120; ++i) {
        input.punch = i == 20;
        animator.advance(rig.clips, input, kDt);
        REQUIRE(animator.state() == CharacterAnimState::Jump);
        REQUIRE(!animator.consume_punch_contact());
        evaluate_character_pose(rig.clips, rig.skeleton, animator.sample(), a, b, local);
        if (i > 5) {
            for (int bone = 0; bone < rig.skeleton.bone_count(); ++bone) {
                if (rig.skeleton.bone(bone).parent >= 0) continue;
                REQUIRE_NEAR(local[static_cast<std::size_t>(bone)][3].y,
                             rig.skeleton.bone(bone).bind_local[3].y, 1e-4f);
            }
        }
    }
    REQUIRE(animator.sample().time < rig.clips.duration(CharacterClip::Jump));
    input.grounded = true;
    input.speed_mps = 6.25f;
    input.sprinting = true;
    animator.advance(rig.clips, input, kDt);
    REQUIRE(animator.state() == CharacterAnimState::Run);
    REQUIRE(animator.sample().fading());
    REQUIRE(animator.sample().from == CharacterClip::Jump);
    apricot_test::pass("air pose strips vertical travel, holds in a fall, blends to run on landing");
}

}  // namespace

int main() {
    std::printf("character_animation_tests\n");
    if (!std::filesystem::is_regular_file(
            asset_path(std::string(kPlayerRoot) + ".emesh")) ||
        !std::filesystem::is_regular_file(asset_path(
            character_clip_asset(CharacterClip::StandUp)))) {
        std::printf("SKIP character_animation_tests (private character assets "
                    "not staged; run tools/lift_character_animations.py)\n");
        return 0;
    }
    jump_uses_the_pose_without_authored_vertical_travel();
    every_lifted_clip_binds_to_every_staged_rig();
    sampling_parts_agrees_with_sampling_matrices();
    blending_parts_preserves_limb_length();
    crossfading_removes_the_state_change_pop();
    the_state_machine_reaches_every_state();
    the_punch_lands_exactly_once_and_alternates_fists();
    a_single_frame_press_still_throws_a_jab();
    the_contact_window_lands_on_an_extended_fist();
    idle_variety_is_deterministic_and_not_in_lockstep();
    the_get_up_ends_standing_and_the_dead_stay_down();
    one_shot_clips_land_where_the_character_stands();
    return apricot_test::done("character_animation_tests");
}
