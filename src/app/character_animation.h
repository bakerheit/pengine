#pragma once

// The character animation set: which clips exist, which state a character is
// in, and how one state fades into the next.
//
// WHY THIS IS APP-SIDE AND HEADER-ONLY. It is presentation: nothing here feeds
// the sim, nothing here is recorded on a replay tape, and no two machines have
// to agree on it. It lives beside app/vehicle_driver_pose.h, which is the same
// shape for the same reason — header-only so a suite that links ONLY
// apricot_sim can still drive the real thing headless, because a state machine
// nobody can test is a state machine that quietly stops working.
//
// WHAT IT DOES NOT DO. It never reads a clock. Time arrives as `dt`, exactly
// like every other step function in this engine. It never calls rand(): idle
// variety and clip choice come out of hash_coord() keyed on a character's
// stable identity, so the same person breathes the same way on every machine
// and a crowd of twenty does not breathe in lockstep.
//
// THE SEAM. CharacterAnimInput is plain data — speed, grounded, downed,
// alarmed, punching — and deliberately knows nothing about PedAgent,
// PlayerCharacterState, or any other sim type. Whoever owns a character fills
// one in; the translation is ten lines and it belongs on the caller's side.
// core/input_frame.h is the same idea pointing the other way.
//
// THE PHASE CLOCKS ARE NOT REIMPLEMENTED HERE. city/character_punch.h and
// city/character_getup.h already own the jab timing, the mid-swing contact
// window, the left/right alternation and the get-up anchor. This is the caller
// they were written for: it holds the state transitions, the per-frame
// sampling and the one-shot hit latch, and asks them for the clock.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "city/character_getup.h"
#include "city/character_punch.h"
#include "core/asset_root.h"
#include "core/rng.h"
#include "core/skeletal_animation.h"

namespace apricot {

// ---------------------------------------------------------------------------
//  The clip registry
// ---------------------------------------------------------------------------
//
// Every clip below is a cooked .eanim staged by tools/lift_character_animations.py
// under the gitignored assets/models/. Add a clip by adding a row here and a
// row to that script's CLIPS table; nothing else in this header hard-codes a
// filename.

enum class CharacterClip : uint8_t {
    Idle,
    Walk,
    Sprint,
    PistolIdle,
    PistolWalk,
    PistolRun,
    PunchLeft,
    PunchRight,
    HitByCar,
    DieForward,
    DieBackward,
    StandUp,
    Jump,
    Count,
};

inline constexpr int kCharacterClipCount =
    static_cast<int>(CharacterClip::Count);

// How a clip's ROOT bone is reconciled with the movement the game already did.
enum class ClipRoot : uint8_t {
    // Locomotion and idles: the game moved the character, so the clip must not
    // move it again. strip_root_motion_xz().
    Strip,
    // A one-shot whose STANDING frame is its start (a fall). Keeps the
    // authored horizontal travel relative to that frame.
    AnchorStart,
    // A one-shot whose standing frame is its END (a get-up). Anchoring this one
    // at t=0 leaves the root at standing height while the pose is still lying
    // down and floats the body a full body-length up — see character_getup.h.
    AnchorEnd,
};

struct CharacterClipInfo {
    CharacterClip clip = CharacterClip::Idle;
    const char* name = "";      // file stem under the animations directory
    bool loops = false;         // a cycle, rather than a one-shot
    bool planted = false;       // gets a locomotion_plant_offset()
    ClipRoot root = ClipRoot::Strip;
};

inline constexpr std::array<CharacterClipInfo, kCharacterClipCount>
    kCharacterClips = {{
        {CharacterClip::Idle,        "idle",         true,  false, ClipRoot::Strip},
        {CharacterClip::Walk,        "walk",         true,  true,  ClipRoot::Strip},
        {CharacterClip::Sprint,      "sprint",       true,  true,  ClipRoot::Strip},
        {CharacterClip::PistolIdle,  "pistol_idle",  true,  false, ClipRoot::Strip},
        {CharacterClip::PistolWalk,  "pistol_walk",  true,  true,  ClipRoot::Strip},
        {CharacterClip::PistolRun,   "pistol_run",   true,  true,  ClipRoot::Strip},
        {CharacterClip::PunchLeft,   "punch_left",   false, false, ClipRoot::Strip},
        {CharacterClip::PunchRight,  "punch_right",  false, false, ClipRoot::Strip},
        {CharacterClip::HitByCar,    "hit_by_car",   false, false, ClipRoot::AnchorStart},
        {CharacterClip::DieForward,  "die_forward",  false, false, ClipRoot::AnchorStart},
        {CharacterClip::DieBackward, "die_backward", false, false, ClipRoot::AnchorStart},
        {CharacterClip::StandUp,     "stand_up",     false, false, ClipRoot::AnchorEnd},
        {CharacterClip::Jump,        "jump",         false, false, ClipRoot::Strip},
    }};

inline const CharacterClipInfo& character_clip_info(CharacterClip clip) {
    return kCharacterClips[static_cast<std::size_t>(clip)];
}

inline const char* character_clip_name(CharacterClip clip) {
    return character_clip_info(clip).name;
}

inline std::string character_clip_asset(CharacterClip clip) {
    return std::string("models/characters/psx_pack/animations/") +
           character_clip_name(clip) + ".eanim";
}

// ---------------------------------------------------------------------------
//  The loaded set
// ---------------------------------------------------------------------------
//
// One per character MODEL, not one shared by all of them. The staged rigs do
// not share a bone ORDER — measured across the 26 staged skeletons, bone counts
// run 28 to 54 and even two 28-bone rigs disagree on index — and Animation
// resolves its channels to bone indices at load. A set bound to one skeleton
// and sampled against another produces a character whose forearm is its shin.

class CharacterClipSet {
public:
    // Loads every registered clip against `skeleton`. Fails if any clip is
    // missing or leaves a channel unbound: a clip that half-binds animates half
    // a body, which reads as a rig bug and is not one.
    bool load(const Skeleton& skeleton, const std::string& root =
                  "models/characters/psx_pack/animations/") {
        loaded_ = false;
        for (const CharacterClipInfo& info : kCharacterClips) {
            const std::size_t index = static_cast<std::size_t>(info.clip);
            const std::string path = asset_path(root + info.name + ".eanim");
            if (!clips_[index].load(path, skeleton)) return false;
            if (clips_[index].unresolved_channels() != 0) return false;
            durations_[index] = clips_[index].duration();
        }
        // The anchor frame for each one-shot, sampled ONCE here rather than per
        // frame. AnchorEnd's reference is the clip's end because that is the
        // frame the character is standing on.
        std::vector<glm::mat4> pose;
        for (const CharacterClipInfo& info : kCharacterClips) {
            const std::size_t index = static_cast<std::size_t>(info.clip);
            if (info.root == ClipRoot::Strip) {
                anchors_[index] = glm::vec2{0.0f};
                continue;
            }
            const float reference_time = info.root == ClipRoot::AnchorEnd
                ? std::max(0.0f, durations_[index] -
                                     character_getup::SAMPLE_EPS)
                : 0.0f;
            clips_[index].sample(reference_time, skeleton, pose);
            anchors_[index] = root_translation_xz(skeleton, pose);
        }
        loaded_ = true;
        return true;
    }

    bool loaded() const { return loaded_; }

    const Animation& clip(CharacterClip which) const {
        return clips_[static_cast<std::size_t>(which)];
    }
    float duration(CharacterClip which) const {
        return durations_[static_cast<std::size_t>(which)];
    }
    glm::vec2 anchor_xz(CharacterClip which) const {
        return anchors_[static_cast<std::size_t>(which)];
    }

private:
    std::array<Animation, kCharacterClipCount> clips_{};
    std::array<float, kCharacterClipCount> durations_{};
    std::array<glm::vec2, kCharacterClipCount> anchors_{};
    bool loaded_ = false;
};

// ---------------------------------------------------------------------------
//  The seam
// ---------------------------------------------------------------------------
//
// PLAIN DATA. Fill one of these from whatever you have — PlayerCharacterState
// on the player's side, a PedAgent plus its activity on the crowd's side — and
// hand it to the animator. Nothing here is a sim type on purpose: the crowd's
// activity enum and this struct must be able to change without either module
// having to rebuild the other's tests.

struct CharacterAnimInput {
    // Stable for the life of the character. Every deterministic choice below
    // is hash_coord() keyed on it, so the same person always idles the same
    // way and two people never idle identically. Never a loop counter.
    uint64_t identity = 0;

    float speed_mps = 0.0f;      // horizontal ground speed
    bool sprinting = false;      // running rather than walking, at any speed
    bool grounded = true;        // false puts the character in the air
    bool armed = false;          // selects the pistol locomotion set
    bool alarmed = false;        // panicked: runs sooner, idles more restlessly

    bool downed = false;         // knocked down and still alive
    bool dead = false;           // killed; terminal
    // Which way the blow came from, NOT which way they fall. A hit from the
    // front drops the body BACKWARD, so this selects die_backward; the default
    // (shot or struck from behind) is die_forward.
    bool impact_from_front = false;

    // LEVELS, not edges. The animator finds the rising edge itself, the same
    // discipline core/input_frame.h uses, so a caller cannot lose a press by
    // clearing it on a frame that owed no update.
    bool punch = false;
    bool flinch = false;
};

// A press that arrives and releases inside ONE render frame must still be a
// press. This is the same rule core/input_frame.h states for InputFrame::pressed
// and tests/fixed_step_tests.cpp pins for the sim, arriving here for the same
// reason: the producer (the event loop) and the consumer (the animator, once a
// frame) run at different rates, so a level that is only sampled at consume time
// loses any press whose release beat the sample.
//
// It is not hypothetical. Measured in the running game: a synthesised F down
// plus F up pushed in the same frame set the level true and false before sync()
// ever looked, and the jab simply never happened — silently, with every guard
// satisfied and nothing logged. A real keyboard usually spaces the two events
// far enough apart to hide it, which is exactly what makes it the kind of bug
// that ships.
//
// set() ORs the press in; consume() reports held-or-latched and clears only the
// latch, so holding the button still reads as one continuous level (and the
// animator's own edge detection still gives you one jab, not an auto-repeat).
struct LatchedPress {
    bool held = false;
    bool latched = false;

    void set(bool down) {
        held = down;
        latched = latched || down;
    }
    bool consume() {
        const bool pressed = held || latched;
        latched = false;
        return pressed;
    }
    void clear() { held = false; latched = false; }
};

// ---------------------------------------------------------------------------
//  Tuning
// ---------------------------------------------------------------------------

struct CharacterAnimTuning {
    // Metres of ground covered per cycle of the clip. This is what keeps feet
    // from skating: clip time advances with DISTANCE, not with wall time.
    float walk_stride_m = 1.35f;
    float sprint_stride_m = 2.58f;
    float idle_speed_mps = 0.08f;    // below this the character is standing
    float run_speed_mps = 3.20f;     // above this a walk becomes a run
    float alarmed_run_speed_mps = 2.10f;

    // Crossfade lengths, in seconds. Different per transition because they are
    // doing different jobs: a locomotion change is a blend, a jab is a snap.
    float fade_locomotion_s = 0.18f;
    float fade_idle_s = 0.24f;
    float fade_action_s = 0.07f;
    float fade_recover_s = 0.16f;
    float fade_impact_s = 0.05f;

    // Idle variety. The ambient idle is nearly ten seconds long, so the cheap
    // and convincing variety is to live in a different part of it: every break,
    // crossfade to a fresh deterministic phase of the same clip. Rate jitter on
    // top stops two neighbours drifting back into step.
    float idle_break_min_s = 5.5f;
    float idle_break_span_s = 6.0f;
    float idle_rate_jitter = 0.10f;

    // The side itself is animator state, not tuning: AlternatingSide has to
    // remember which fist went last, and a tuning struct handed in by const
    // reference cannot.
    float punch_rate = 1.65f;
    float punch_tail_trim_s = 0.30f;

    // THE CONTACT WINDOW IS PER CLIP, AND ONE SHARED PAIR OF FRACTIONS IS
    // WRONG. The two jabs are not the same shape. Measured as the distance
    // from the hips to the swinging hand, through the real skinning path:
    //
    //   punch_right, trimmed to 0.567 s: reach peaks at 0.70 m at t=0.34,
    //                                    i.e. 0.60 of the clip.
    //   punch_left,  trimmed to 0.733 s: reach peaks at 0.78 m at t=0.55,
    //                                    i.e. 0.75 of the clip — and at 0.33
    //                                    (0.45 of the clip) the fist is at its
    //                                    most RETRACTED, 0.50 m.
    //
    // A single [0.35, 0.55) window therefore lands on the wind-up for the right
    // jab and on the pull-BACK for the left one, so a hit registers while the
    // fist is tucked in at the character's chest. Nothing about the animation
    // looks wrong when that happens; the swing plays perfectly and the damage
    // just arrives at the wrong moment. Pinned by a reach measurement in
    // tests/character_animation_tests.cpp.
    float punch_right_contact_lo_frac = 0.52f;
    float punch_right_contact_hi_frac = 0.72f;
    float punch_left_contact_lo_frac = 0.68f;
    float punch_left_contact_hi_frac = 0.86f;

    // THE GET-UP TRIM IS AT THE FRONT, AND character_getup.h SAYS OTHERWISE.
    // That header describes the clip as ending in ~1 s of settled standing
    // dead-time and trims the TAIL for snappiness. Measured on the cooked
    // stand_up clip that is backwards: head height is still climbing on the
    // final frame (1.21 m at 4.08 s, 1.44 at 4.27, 1.63 at 4.66) and the dead
    // time is the first 1.9 s, where the head moves 0.29 m to 0.44 m and the
    // character is essentially still lying there. Trimming a second off the end
    // hands control back with the character doubled over at 1.21 m and leaves
    // the fade to idle to snap them upright.
    //
    // So the tail trim is zero and the dead time is skipped at the FRONT, by
    // starting the phase clock partway in. character_getup::advance() is still
    // the clock — offsetting the elapsed time it is handed shortens the whole
    // phase and still finishes exactly at effective_end, which is where the
    // AnchorEnd root reference is taken. Pinned by the head-height assertion in
    // tests/character_animation_tests.cpp.
    float getup_rate = 1.35f;
    float getup_head_skip_s = 1.60f;
    float getup_tail_trim_s = 0.00f;

    // hit_by_car doubles as the stagger. The pack ships no dedicated flinch, so
    // a flinch is a WINDOW cut out of the knockdown — the impact recoil, before
    // the body commits to going down — played fast and faded back out. Honest
    // reuse, not a placeholder: it is the same body taking the same hit,
    // stopped before it falls.
    //
    // The window is measured, not guessed. Sampled head height through the
    // cooked clip: flat at 1.53 m for the first 0.6 s (the clip opens on a
    // standing brace and does nothing), 1.53 -> 1.45 m over 0.6-0.9 s, then it
    // drops away hard. So the only part of this clip that reads as a stagger is
    // [0.55, 0.90] and a window starting at zero shows the player nothing.
    float flinch_start_s = 0.55f;
    float flinch_end_s = 0.90f;
    float flinch_rate = 1.45f;

    // Same measurement, same reason. hit_by_car is on the ground and static
    // from 1.7 s of its 2.9 s, so over a second of it is a held frame either
    // way; trimming it makes the knockdown land sooner and changes nothing
    // about where the body ends up.
    float knockdown_rate = 1.15f;
    float knockdown_tail_trim_s = 1.05f;
    // die_forward is still falling at 2.75 s and settled by 2.90 of 3.67;
    // die_backward is settled from 2.68 of 4.60. One trim serves both only if
    // it respects the LATER of the two, so 0.55 s it is.
    float death_rate = 1.00f;
    float death_tail_trim_s = 0.55f;
    float jump_rate = 1.00f;
};

// ---------------------------------------------------------------------------
//  States and the per-frame result
// ---------------------------------------------------------------------------

enum class CharacterAnimState : uint8_t {
    Idle,
    Walk,
    Run,
    Jump,
    Punch,
    Flinch,
    Knockdown,   // going down, still moving
    Downed,      // on the ground, holding the frame it landed on
    GetUp,
    Die,
    Count,
};

inline const char* character_anim_state_name(CharacterAnimState state) {
    switch (state) {
        case CharacterAnimState::Idle:      return "idle";
        case CharacterAnimState::Walk:      return "walk";
        case CharacterAnimState::Run:       return "run";
        case CharacterAnimState::Jump:      return "jump";
        case CharacterAnimState::Punch:     return "punch";
        case CharacterAnimState::Flinch:    return "flinch";
        case CharacterAnimState::Knockdown: return "knockdown";
        case CharacterAnimState::Downed:    return "downed";
        case CharacterAnimState::GetUp:     return "getup";
        case CharacterAnimState::Die:       return "die";
        case CharacterAnimState::Count:     break;
    }
    return "?";
}

// What to sample this frame. `blend` is the weight of `clip`: 1 means the fade
// is over and `from` may be ignored. `from` and `clip` are allowed to be the
// same clip at different times — that is how an idle break works.
struct CharacterAnimSample {
    CharacterClip clip = CharacterClip::Idle;
    float time = 0.0f;
    CharacterClip from = CharacterClip::Idle;
    float from_time = 0.0f;
    float blend = 1.0f;
    ClipRoot root = ClipRoot::Strip;
    glm::vec2 anchor_xz{0.0f};
    bool fading() const { return blend < 1.0f; }
};

// ---------------------------------------------------------------------------
//  The animator
// ---------------------------------------------------------------------------

class CharacterAnimator {
public:
    // dt is seconds. There is no clock in here and there must never be one.
    void advance(const CharacterClipSet& clips, const CharacterAnimInput& in,
                 float dt, const CharacterAnimTuning& tuning = {}) {
        if (!clips.loaded()) return;
        if (!(dt > 0.0f) || !std::isfinite(dt)) dt = 0.0f;
        identity_ = in.identity;

        const bool punch_edge = in.punch && !previous_punch_;
        const bool flinch_edge = in.flinch && !previous_flinch_;
        previous_punch_ = in.punch;
        previous_flinch_ = in.flinch;

        alarmed_idle_ = in.alarmed;
        decide(clips, in, tuning, punch_edge, flinch_edge);
        elapsed_ += dt;
        advance_clock(clips, in, tuning, dt);
        advance_fade(dt);
    }

    CharacterAnimState state() const { return state_; }
    const CharacterAnimSample& sample() const { return sample_; }

    // Plant lift for this frame, interpolated across a fade so the character
    // does not step up or drop as the blend crosses over.
    float plant(const std::array<float, kCharacterClipCount>& plants) const {
        const float to = plants[static_cast<std::size_t>(sample_.clip)];
        if (!sample_.fading()) return to;
        const float from = plants[static_cast<std::size_t>(sample_.from)];
        return from + (to - from) * sample_.blend;
    }

    // THE ONE-SHOT HIT LATCH. True exactly once per swing, on the first frame
    // inside character_punch's contact window. Clearing on read is what stops a
    // single jab registering on every frame it is extended.
    bool consume_punch_contact() {
        const bool hit = punch_contact_pending_;
        punch_contact_pending_ = false;
        return hit;
    }
    // Which fist is mid-swing. Presentation and tests only; the alternation
    // itself lives in character_punch::AlternatingSide.
    bool punching_right() const { return punch_right_; }
    bool punching() const { return state_ == CharacterAnimState::Punch; }

    void reset() { *this = CharacterAnimator{}; }

private:
    // -- transition ---------------------------------------------------------
    void decide(const CharacterClipSet& clips, const CharacterAnimInput& in,
                const CharacterAnimTuning& tuning, bool punch_edge,
                bool flinch_edge) {
        // Death outranks everything and is terminal.
        if (in.dead) {
            if (state_ != CharacterAnimState::Die &&
                state_ != CharacterAnimState::Downed) {
                const CharacterClip fall = in.impact_from_front
                    ? CharacterClip::DieBackward : CharacterClip::DieForward;
                enter(CharacterAnimState::Die, fall, tuning.fade_impact_s);
            }
            return;
        }
        if (state_ == CharacterAnimState::Die ||
            (state_ == CharacterAnimState::Downed && hold_was_death_)) {
            // Revived. Treat it as a knockdown recovery rather than snapping
            // upright, which is the only thing the clip set can express.
            enter(CharacterAnimState::GetUp, CharacterClip::StandUp,
                  tuning.fade_recover_s);
            hold_was_death_ = false;
            return;
        }

        if (in.downed) {
            if (state_ != CharacterAnimState::Knockdown &&
                state_ != CharacterAnimState::Downed) {
                enter(CharacterAnimState::Knockdown, CharacterClip::HitByCar,
                      tuning.fade_impact_s);
            }
            return;
        }
        if (state_ == CharacterAnimState::Downed ||
            state_ == CharacterAnimState::Knockdown) {
            enter(CharacterAnimState::GetUp, CharacterClip::StandUp,
                  tuning.fade_recover_s);
            return;
        }
        if (state_ == CharacterAnimState::GetUp) return;  // runs to completion

        if (flinch_edge && state_ != CharacterAnimState::Flinch) {
            enter(CharacterAnimState::Flinch, CharacterClip::HitByCar,
                  tuning.fade_impact_s);
            return;
        }
        if (state_ == CharacterAnimState::Flinch) return;

        if (!in.grounded) {
            if (state_ != CharacterAnimState::Jump) {
                enter(CharacterAnimState::Jump, CharacterClip::Jump,
                      tuning.fade_action_s);
            }
            return;
        }
        if (punch_edge) {
            begin_punch(tuning);
            return;
        }
        if (state_ == CharacterAnimState::Punch) return;

        enter_locomotion(clips, in, tuning);
    }

    void begin_punch(const CharacterAnimTuning& tuning) {
        // The side comes from character_punch::AlternatingSide, so consecutive
        // jabs trade fists. take() is the call that advances it, and calling it
        // exactly once per swing is the whole contract.
        punch_right_ = punch_side_.take();
        enter(CharacterAnimState::Punch,
              punch_right_ ? CharacterClip::PunchRight : CharacterClip::PunchLeft,
              tuning.fade_action_s);
        punch_contact_latched_ = false;
    }

    void enter_locomotion(const CharacterClipSet& clips,
                          const CharacterAnimInput& in,
                          const CharacterAnimTuning& tuning) {
        const float run_threshold = in.alarmed ? tuning.alarmed_run_speed_mps
                                               : tuning.run_speed_mps;
        CharacterAnimState wanted = CharacterAnimState::Idle;
        if (in.speed_mps > tuning.idle_speed_mps) {
            wanted = (in.sprinting || in.speed_mps > run_threshold)
                ? CharacterAnimState::Run : CharacterAnimState::Walk;
        }
        const CharacterClip wanted_clip = locomotion_clip(wanted, in.armed);
        if (state_ == wanted && sample_.clip == wanted_clip) {
            if (wanted == CharacterAnimState::Idle) idle_break(clips, tuning);
            return;
        }
        const float fade = wanted == CharacterAnimState::Idle ||
                           state_ == CharacterAnimState::Idle
            ? tuning.fade_idle_s : tuning.fade_locomotion_s;
        enter(wanted, wanted_clip, fade);
        if (wanted == CharacterAnimState::Idle) start_idle(clips, tuning);
    }

    static CharacterClip locomotion_clip(CharacterAnimState state, bool armed) {
        switch (state) {
            case CharacterAnimState::Walk:
                return armed ? CharacterClip::PistolWalk : CharacterClip::Walk;
            case CharacterAnimState::Run:
                return armed ? CharacterClip::PistolRun : CharacterClip::Sprint;
            default:
                return armed ? CharacterClip::PistolIdle : CharacterClip::Idle;
        }
    }

    void enter(CharacterAnimState state, CharacterClip clip, float fade) {
        if (state_ == state && sample_.clip == clip) return;
        begin_fade(clip, fade);
        state_ = state;
        elapsed_ = 0.0f;
        if (state != CharacterAnimState::Punch) punch_contact_latched_ = false;
    }

    // Capture where we are now, then start the new clip from zero. The captured
    // pose keeps advancing during the fade, which is what stops a walk-to-idle
    // transition freezing a leg in mid-air.
    void begin_fade(CharacterClip clip, float fade) {
        sample_.from = sample_.clip;
        sample_.from_time = sample_.time;
        from_state_ = state_;
        fade_length_ = std::max(fade, 0.0f);
        fade_elapsed_ = 0.0f;
        sample_.blend = fade_length_ > 0.0f ? 0.0f : 1.0f;
        sample_.clip = clip;
        sample_.time = 0.0f;
    }

    void advance_fade(float dt) {
        if (fade_length_ <= 0.0f) {
            sample_.blend = 1.0f;
            return;
        }
        fade_elapsed_ += dt;
        if (fade_elapsed_ >= fade_length_) {
            fade_length_ = 0.0f;
            sample_.blend = 1.0f;
            return;
        }
        // Smoothstep rather than linear: a linear fade has a corner at each end
        // and the corner is exactly what reads as a pop.
        const float t = fade_elapsed_ / fade_length_;
        sample_.blend = t * t * (3.0f - 2.0f * t);
    }

    // -- clocks -------------------------------------------------------------
    void advance_clock(const CharacterClipSet& clips,
                       const CharacterAnimInput& in,
                       const CharacterAnimTuning& tuning, float dt) {
        const CharacterClipInfo& info = character_clip_info(sample_.clip);
        sample_.root = info.root;
        sample_.anchor_xz = clips.anchor_xz(sample_.clip);
        const float duration = clips.duration(sample_.clip);

        switch (state_) {
            case CharacterAnimState::Idle:
                sample_.time += dt * idle_rate(tuning);
                break;
            case CharacterAnimState::Walk:
            case CharacterAnimState::Run: {
                // Distance, not time. A character shoved to half speed takes
                // half-length strides instead of moon-walking.
                const float stride = state_ == CharacterAnimState::Run
                    ? tuning.sprint_stride_m : tuning.walk_stride_m;
                const float cycles = stride > 1e-4f
                    ? std::max(in.speed_mps, 0.0f) * dt / stride : 0.0f;
                sample_.time += cycles * duration;
                break;
            }
            case CharacterAnimState::Punch:
                advance_punch(clips, tuning);
                break;
            case CharacterAnimState::Flinch:
                advance_flinch(clips, tuning);
                break;
            case CharacterAnimState::Knockdown:
                advance_one_shot(duration, tuning.knockdown_rate,
                                 tuning.knockdown_tail_trim_s, true);
                break;
            case CharacterAnimState::Die:
                advance_one_shot(duration, tuning.death_rate,
                                 tuning.death_tail_trim_s, true);
                break;
            case CharacterAnimState::GetUp:
                advance_getup(duration, tuning);
                break;
            case CharacterAnimState::Jump:
                // Hold the descending pose until collision says we landed.
                sample_.time = std::min(elapsed_ * tuning.jump_rate,
                                         duration * 0.75f);
                break;
            case CharacterAnimState::Downed:
                sample_.time = hold_time_;
                break;
            case CharacterAnimState::Count:
                break;
        }
        // Advance the outgoing pose too, at its own rate. A frozen source pose
        // is what makes a crossfade look like a dissolve between two photos.
        if (sample_.fading()) {
            sample_.from_time += dt * outgoing_rate(in, tuning);
            sample_.from_time = wrap_cycle(sample_.from_time,
                                           clips.duration(sample_.from));
        }
        // Keep a looping clock inside its own cycle. Animation::sample() would
        // fmod it anyway, but a float that has been accumulating for an hour
        // has lost the resolution to say WHERE in the cycle it is, and the
        // symptom is a character whose idle gets choppier the longer you watch.
        if (character_clip_info(sample_.clip).loops) {
            sample_.time = wrap_cycle(sample_.time, duration);
        }
    }

    static float wrap_cycle(float time, float duration) {
        if (!(duration > 0.0f) || !std::isfinite(time)) return 0.0f;
        time = std::fmod(time, duration);
        return time < 0.0f ? time + duration : time;
    }

    float idle_rate(const CharacterAnimTuning& tuning) const {
        const float jitter = tuning.idle_rate_jitter *
                             (unit_hash(1, 0) * 2.0f - 1.0f);
        return (1.0f + jitter) * (alarmed_idle_ ? 1.22f : 1.0f);
    }

    float outgoing_rate(const CharacterAnimInput& in,
                        const CharacterAnimTuning& tuning) const {
        switch (from_state_) {
            case CharacterAnimState::Walk:
            case CharacterAnimState::Run: {
                const float stride = from_state_ == CharacterAnimState::Run
                    ? tuning.sprint_stride_m : tuning.walk_stride_m;
                return stride > 1e-4f ? std::max(in.speed_mps, 0.0f) / stride
                                      : 0.0f;
            }
            case CharacterAnimState::Downed:
                return 0.0f;
            default:
                return 1.0f;
        }
    }

    void advance_one_shot(float duration, float rate, float trim, bool hold) {
        const character_getup::Progress progress = character_getup::advance(
            elapsed_, rate, duration, trim);
        sample_.time = progress.sample_t;
        if (!progress.finished) return;
        if (hold) {
            hold_time_ = progress.sample_t;
            hold_was_death_ = state_ == CharacterAnimState::Die;
            state_ = CharacterAnimState::Downed;
        }
    }

    // The get-up's dead time is at the FRONT (see getup_head_skip_s), so the
    // phase clock is handed an elapsed time that already includes the skip.
    // character_getup::advance() still owns the clock and still finishes at
    // effective_end, which is where the AnchorEnd root reference was taken.
    void advance_getup(float duration, const CharacterAnimTuning& tuning) {
        const float rate = std::max(tuning.getup_rate, 1e-3f);
        const float skip = std::max(tuning.getup_head_skip_s, 0.0f);
        const character_getup::Progress progress = character_getup::advance(
            elapsed_ + skip / rate, rate, duration, tuning.getup_tail_trim_s);
        sample_.time = progress.sample_t;
        if (progress.finished) {
            state_ = CharacterAnimState::Idle;
            elapsed_ = 0.0f;
        }
    }

    void advance_punch(const CharacterClipSet& clips,
                       const CharacterAnimTuning& tuning) {
        const float duration = clips.duration(sample_.clip);
        const character_punch::Progress progress = character_punch::advance(
            elapsed_, tuning.punch_rate, duration, tuning.punch_tail_trim_s);
        sample_.time = progress.sample_t;
        const character_punch::ContactWindow window =
            punch_contact_window(clips, tuning, punch_right_);
        if (!punch_contact_latched_ &&
            character_punch::is_contact(progress.sample_t, window.lo,
                                        window.hi)) {
            punch_contact_latched_ = true;
            punch_contact_pending_ = true;
        }
        if (progress.finished) {
            state_ = CharacterAnimState::Idle;
            elapsed_ = 0.0f;
        }
    }

    // A window out of the middle of the knockdown, not its opening. Same skip
    // trick as the get-up: start the clock past the clip's standing brace, stop
    // it before the body commits to the floor.
    void advance_flinch(const CharacterClipSet& clips,
                        const CharacterAnimTuning& tuning) {
        const float rate = std::max(tuning.flinch_rate, 1e-3f);
        const float end = std::min(clips.duration(sample_.clip),
                                   tuning.flinch_end_s);
        const float start = std::min(std::max(tuning.flinch_start_s, 0.0f),
                                     std::max(end - 0.05f, 0.0f));
        const character_getup::Progress progress = character_getup::advance(
            elapsed_ + start / rate, rate, end, 0.0f);
        sample_.time = progress.sample_t;
        if (progress.finished) {
            state_ = CharacterAnimState::Idle;
            elapsed_ = 0.0f;
        }
    }

    // The contact span for the fist currently swinging, in clip seconds.
    // Exposed as a static so the suite can ask for exactly what the animator
    // uses rather than recomputing the fractions and agreeing with itself.
public:
    static character_punch::ContactWindow punch_contact_window(
        const CharacterClipSet& clips, const CharacterAnimTuning& tuning,
        bool right) {
        const CharacterClip clip = right ? CharacterClip::PunchRight
                                         : CharacterClip::PunchLeft;
        const float end = character_punch::effective_end(
            clips.duration(clip), tuning.punch_tail_trim_s);
        return character_punch::contact_window(
            end,
            right ? tuning.punch_right_contact_lo_frac
                  : tuning.punch_left_contact_lo_frac,
            right ? tuning.punch_right_contact_hi_frac
                  : tuning.punch_left_contact_hi_frac);
    }

private:
    // -- idle variety -------------------------------------------------------
    void start_idle(const CharacterClipSet& clips,
                    const CharacterAnimTuning& tuning) {
        idle_break_ = 0;
        idle_next_break_s_ = break_period(tuning, 0);
        sample_.time = unit_hash(2, 0) * clips.duration(sample_.clip);
    }

    // Every break, crossfade to a different phase of the SAME clip. The ambient
    // idle is nearly ten seconds long, so this is genuinely a different bit of
    // performance rather than a restart, and it costs one extra sample.
    void idle_break(const CharacterClipSet& clips,
                    const CharacterAnimTuning& tuning) {
        if (elapsed_ < idle_next_break_s_) return;
        ++idle_break_;
        const float duration = clips.duration(sample_.clip);
        const float destination = unit_hash(2, idle_break_) * duration;
        const CharacterClip clip = sample_.clip;
        begin_fade(clip, tuning.fade_idle_s);
        sample_.time = destination;
        elapsed_ = 0.0f;
        idle_next_break_s_ = break_period(tuning, idle_break_);
    }

    float break_period(const CharacterAnimTuning& tuning, int32_t index) const {
        return tuning.idle_break_min_s +
               unit_hash(3, index) * tuning.idle_break_span_s;
    }

    // [0, 1) from the character's identity. hash_coord(), never a stream: an
    // agent streamed out and back must idle the same way it did before.
    float unit_hash(int32_t salt, int32_t index) const {
        return static_cast<float>(hash_coord(identity_, salt, index) >> 40) /
               16777216.0f;
    }

    CharacterAnimState state_ = CharacterAnimState::Idle;
    CharacterAnimState from_state_ = CharacterAnimState::Idle;
    CharacterAnimSample sample_{};
    uint64_t identity_ = 0;
    float elapsed_ = 0.0f;
    float fade_length_ = 0.0f;
    float fade_elapsed_ = 0.0f;
    float hold_time_ = 0.0f;
    bool hold_was_death_ = false;
    bool alarmed_idle_ = false;
    bool previous_punch_ = false;
    bool previous_flinch_ = false;
    bool punch_right_ = true;
    bool punch_contact_latched_ = false;
    bool punch_contact_pending_ = false;
    int32_t idle_break_ = 0;
    float idle_next_break_s_ = 0.0f;
    character_punch::AlternatingSide punch_side_{};
};


// ---------------------------------------------------------------------------
//  Evaluating a sample
// ---------------------------------------------------------------------------

// Turn one CharacterAnimSample into local bone matrices: sample the incoming
// clip, sample the outgoing one if a fade is running, blend the PARTS, compose,
// then apply the clip's root policy.
//
// This is the function character_visual.cpp calls, which is the point — a
// headless suite driving it is exercising the real producer rather than a
// hand-written copy of it that agrees right up until somebody edits one of them.
//
// The scratch vectors are the caller's so a frame with sixty rigs on screen
// does not allocate sixty times. They may be empty on the first call.
//
// ONE root policy is applied, the incoming clip's. Across a fade the outgoing
// pose is reconciled the same way, which is exact whenever both sides agree
// (every locomotion pair) and leaks the outgoing clip's authored travel,
// scaled by the remaining fade weight, when they do not. That is bounded by
// the fade length: at 0.05 s into a knockdown it is under a centimetre.
inline void evaluate_character_pose(const CharacterClipSet& clips,
                                    const Skeleton& skeleton,
                                    const CharacterAnimSample& sample,
                                    std::vector<BonePose>& scratch_a,
                                    std::vector<BonePose>& scratch_b,
                                    std::vector<glm::mat4>& out_local) {
    // The controller owns jump height. Remove the clip's vertical root travel
    // before blending either side, preserving its arm/leg tuck and recovery.
    const auto strip_jump_lift = [&](CharacterClip clip,
                                      std::vector<BonePose>& parts) {
        if (clip != CharacterClip::Jump) return;
        for (int i = 0; i < skeleton.bone_count(); ++i) {
            if (skeleton.bone(i).parent < 0)
                parts[static_cast<std::size_t>(i)].translation.y =
                    skeleton.bone(i).bind_local[3].y;
        }
    };
    clips.clip(sample.clip).sample_parts(sample.time, skeleton, scratch_a);
    strip_jump_lift(sample.clip, scratch_a);
    if (sample.fading()) {
        clips.clip(sample.from).sample_parts(sample.from_time, skeleton,
                                             scratch_b);
        strip_jump_lift(sample.from, scratch_b);
        blend_bone_poses(scratch_b, scratch_a, sample.blend, scratch_a);
    }
    compose_local_poses(scratch_a, out_local);
    switch (sample.root) {
        case ClipRoot::Strip:
            strip_root_motion_xz(skeleton, out_local);
            break;
        case ClipRoot::AnchorStart:
        case ClipRoot::AnchorEnd:
            anchor_root_motion_xz(skeleton, out_local, sample.anchor_xz);
            break;
    }
}

}  // namespace apricot
