#pragma once

#include <cstdint>
#include <type_traits>

namespace apricot {

// One frame of player intent, in engine-neutral terms.
//
// THIS STRUCT IS THE REPLAY FORMAT. A recorded run is nothing but a seed plus
// a std::vector<InputFrame>, replayed through the same pure step functions, so
// the tape has to stay a flat block of bytes forever:
//
//   * POD. No pointers, no std::string, no virtuals, no owning members.
//   * Trivially copyable, standard layout, no padding (asserted below).
//   * Append new fields at the END and bump the tape version. Never reorder
//     or resize an existing field — that silently invalidates every tape
//     ever recorded, and the failure looks like "the physics changed".
//
// The host layer (platform/input.h) is the only thing that fills one of these
// in; the sim never learns what a keyboard is.
//
// Ranges are normalised so the sim never has to know about device scaling:
//   steer      [-1, 1]  negative = left, positive = right
//   throttle   [ 0, 1]
//   brake      [ 0, 1]
//   handbrake  [ 0, 1]  analogue on purpose: a rally handbrake is not a switch
//   look_dx/dy         accumulated look delta for the frame, in radians

// Button bits for InputFrame::held and InputFrame::pressed. Explicit values —
// these are serialised into replay tapes, so the numbers are the contract and
// renumbering them rewrites history. Append only.
enum ButtonBit : uint32_t {
    kBtnNone      = 0u,
    kBtnRespawn   = 1u << 0,
    kBtnLookBack  = 1u << 1,
    kBtnCamCycle  = 1u << 2,
    kBtnPause     = 1u << 3,
    kBtnShiftUp   = 1u << 4,
    kBtnShiftDown = 1u << 5,
    kBtnAccept    = 1u << 6,
    kBtnBack      = 1u << 7,
    kBtnQuit      = 1u << 8,
};

struct InputFrame {
    // --- analogue axes ------------------------------------------------------
    float steer     = 0.0f;
    float throttle  = 0.0f;
    float brake     = 0.0f;
    float handbrake = 0.0f;

    // --- look deltas (radians, this frame) ---------------------------------
    float look_dx = 0.0f;
    float look_dy = 0.0f;

    // --- digital state ------------------------------------------------------
    // Level: bits currently held down. Safe to sample in any step.
    uint32_t held = 0u;

    // LATCHED EDGES: bits that went down since the last time the sim consumed
    // them. This is latched rather than per-frame because a render frame can
    // owe ZERO sim steps (see core/fixed_step.h) — a press sampled per-frame
    // and cleared per-frame is simply lost on those frames, and the bug reads
    // as "the handbrake didn't take sometimes", which is agony to chase.
    //
    // The producer ORs edges in as they arrive; the consumer clears them only
    // after a step has actually run. Never clear these on the render cadence.
    uint32_t pressed = 0u;
};

// The replay format's whole reason for existing. If one of these ever fails,
// something added a non-POD member and every recorded tape just became junk.
static_assert(std::is_trivially_copyable_v<InputFrame>,
              "InputFrame is memcpy'd into the replay tape; keep it POD");
static_assert(std::is_standard_layout_v<InputFrame>,
              "InputFrame is serialised field-by-field; keep it standard layout");
static_assert(sizeof(InputFrame) == 32,
              "InputFrame gained padding or a field; bump the replay tape "
              "version deliberately, then update this number");

// --- small helpers -----------------------------------------------------------
// Free functions, not members, so InputFrame stays an aggregate you can write
// with a braced initialiser in tests.

constexpr bool is_held(const InputFrame& f, uint32_t bits) {
    return (f.held & bits) != 0u;
}

// True if the edge is latched. Does NOT consume it — the step loop clears the
// whole mask once, after stepping (see clear_edges).
constexpr bool was_pressed(const InputFrame& f, uint32_t bits) {
    return (f.pressed & bits) != 0u;
}

// Call ONCE per frame in which at least one sim step ran, after the last step.
constexpr void clear_edges(InputFrame& f) { f.pressed = 0u; }

}  // namespace apricot
