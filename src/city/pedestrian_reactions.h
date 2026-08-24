#pragma once

#include <cstdint>

// How a punched pedestrian reacts.
//
// When the player throws a punch, the street reacts based on a hidden per-ped
// "nerve" rolled once at spawn. This header is the PURE decision logic behind
// that reaction — nothing from the host layer, no assets, no crowd-system
// state — so the headless gate calls exactly the functions the game calls and
// the two paths cannot diverge.
//
// The nerve roll itself belongs to the SPAWNER, which must key it to the
// pedestrian's spawn identity via hash_coord() rather than pull it off a
// stream. disposition_from_nerve() takes the [0,1) result and nothing else,
// which is what keeps that decision out of this header.
namespace apricot::ped_react {

// Hidden "nerve" bucket, rolled per civilian at spawn. Governs the punch
// reaction; unused for police (they run their own combat AI).
enum class Disposition : std::uint8_t {
    Coward,    // flees the instant it's punched
    Waverer,   // fights back, but breaks and flees once hurt (PCG-152)
    DieHard,   // fights to the death, never flees (PCG-152)
};

// Spawn split (feel-tunable seeds). A uniform [0,1) "nerve" roll buckets into a
// disposition: the low band is cowards (most people), the middle band waverers,
// the high band die-hards (the rare few who won't back down).
constexpr float COWARD_FRACTION  = 0.50f;
constexpr float WAVERER_FRACTION = 0.35f;   // die-hard = the remaining 0.15
static_assert(COWARD_FRACTION + WAVERER_FRACTION <= 1.0f,
              "disposition fractions must leave room for die-hards");

// Bucket a uniform [0,1) nerve roll into a disposition.
constexpr Disposition disposition_from_nerve(float nerve) {
    if (nerve < COWARD_FRACTION)                    return Disposition::Coward;
    if (nerve < COWARD_FRACTION + WAVERER_FRACTION) return Disposition::Waverer;
    return Disposition::DieHard;
}

// How a SURVIVING ped reacts to a landed melee (punch) hit. Cowards break and
// run; waverers and die-hards square up and fight (PCG-152 acts on Fight — for
// PCG-151 every survivor flees regardless, so the feature is coherent alone).
enum class PunchReaction : std::uint8_t { Flee, Fight };

constexpr PunchReaction punch_reaction(Disposition d) {
    return d == Disposition::Coward ? PunchReaction::Flee
                                    : PunchReaction::Fight;
}

// PCG-152 fighter tunables (feel-tunable seeds) ------------------------------

// A WAVERER breaks off and flees once its health falls to/below this fraction of
// max (got more than it bargained for). A DIE-HARD ignores this and fights on.
constexpr float WAVERER_BREAK_HEALTH_FRAC = 0.40f;   // 40% of 100 HP -> flee

// True when a waverer at `health` (out of `max_health`) should break and flee.
// Die-hards never break, so this is only consulted for waverers.
constexpr bool waverer_should_break(float health, float max_health) {
    return health <= max_health * WAVERER_BREAK_HEALTH_FRAC;
}

// Reach at which a fighter's punch connects with the player (m). Mirrors the
// player's own PUNCH_REACH_M (1.2) with a touch of slack so a shuffling fighter
// still lands blows.
constexpr float FIGHT_PUNCH_REACH_M = 1.4f;

// A fighter closes to just inside punch reach before it stops to swing, so it
// doesn't stutter-stop exactly on the reach boundary.
constexpr float FIGHT_CLOSE_RANGE_M = 1.1f;

// Seconds between the START of one swing and the next (contact cadence). Mirrors
// the player's swing pace / POLICE_SHOOT_PERIOD so hits land at a readable rhythm.
constexpr float FIGHT_PUNCH_PERIOD_S = 1.2f;

// Damage a landed NPC punch does to the player.
constexpr float FIGHT_PUNCH_DAMAGE = 10.0f;

// A fighter closes at a brisk pace (m/s) — faster than a walk so it can keep up
// with a backpedalling player, slower than a full panic sprint.
constexpr float FIGHT_CLOSE_SPEED_MPS = 3.2f;

// De-escalation: a fighter gives up (-> flee, then normal) once the player has
// been beyond this range, or otherwise unreachable (in a vehicle), continuously
// for GIVE_UP_SECONDS.
constexpr float FIGHT_GIVE_UP_RANGE_M = 15.0f;
constexpr float FIGHT_GIVE_UP_SECONDS = 3.0f;

// Fraction of a swing clip at which the fist is extended and the blow lands
// (mirrors the player's PUNCH_CONTACT_{LO,HI}_FRAC window). The hit fires once
// per swing when the clip's phase first enters this window.
constexpr float FIGHT_CONTACT_LO_FRAC = 0.30f;
constexpr float FIGHT_CONTACT_HI_FRAC = 0.55f;

} // namespace apricot::ped_react
