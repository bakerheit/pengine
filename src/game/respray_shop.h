#pragma once
// The respray at Rook's Auto Repair: the sim-side rules for one visit to a bay.
//
// What a respray does to the wanted level is decided here, from step-owned
// state, so it replays and a headless suite can hold it. The host layer owns
// the picker, the paint and the sound: it hands this step a pending order and
// acts on the event that comes back. Nothing here decodes, uploads or waits on
// a paint result, so the wanted outcome never depends on one.
//
// THE WANTED RULE. A completed respray clears the stars only if no cop had eyes
// on you when you pulled into the bay. "Pulled in" is a latch, not a sample:
//
//  - A visit starts over on any step that is not driving, is a different
//    vehicle, or does not fit a bay (repair_bay_fits). It remembers whether
//    that step was spent driving outside the bay (`outside_driving`).
//  - On the first fitted step, `pulled_in = outside_driving`. A visit that
//    starts already fitted never arrives: getting out and back in, a teleport,
//    a load, or a car swapped into the bay must drive out and pull in again.
//    That is the cost. Without it an exit and re-entry in the bay re-sampled
//    the sighting on a stationary car and cleared stars nobody pulled in for.
//  - While pulled in and not yet arrived, every step ORs the sighting into
//    `seen`, up to and including the first step the car is stopped and upright
//    (repair_shop_ready). `arrived` then latches and sightings stop counting.
//    A cop who turns up after you have stopped does not block the clear; that
//    is accepted, not missed.
//
// The host passes the same visible-police list that gates the wanted cooling,
// and runs this step after the wanted update, so there is no second sight query.
#include <algorithm>
#include <cstdint>

#include "core/input_frame.h"
#include "game/police_arrest.h"
#include "game/police_offenses.h"
#include "game/repair_shop.h"
#include "game/vehicle_paint.h"
#include "game/wanted_system.h"

namespace apricot {

struct ResprayVisit {
    uint64_t vehicle = 0;
    bool outside_driving = false, in_bay = false, pulled_in = false,
         arrived = false, seen = false;
    float spray_s = -1.0f;  // seconds into the spray; negative when not spraying
    PaintOrder order{};
    constexpr bool spraying() const { return spray_s >= 0.0f; }
};

// Started:   an order was taken and the spray began this step.
// Rejected:  an order arrived that could not start (not arrived, not ready, or
//            a spray already running, which carries on).
// Cancelled: a running spray stopped early. The paint does not change.
// Completed: the spray ran its full time; `seen` and `order` say what to apply.
// One event per step. Cancelled and Completed win over Rejected, because the
// host must act on them.
enum class ResprayEvent : uint8_t { None, Started, Rejected, Cancelled, Completed };

struct ResprayResult {
    ResprayEvent event = ResprayEvent::None;
    bool seen = false;
    PaintOrder order{};
};

inline constexpr float kRespraySeconds = 1.0f;
// Accept (the exit button) is refused for this long into a spray, so a second
// tap of the button that confirmed the order does not throw the player out.
inline constexpr float kResprayExitGraceSeconds = 0.25f;

// One sim step of a respray visit. `vehicle` is the host's identity for the car
// being driven; `request` is the order the picker produced, on the one step it
// enters, else null.
inline ResprayResult step_respray_shop(ResprayVisit& visit, const VehicleState& car,
                                       const VehicleTuning& tuning, float dt,
                                       bool driving, uint64_t vehicle,
                                       bool police_eyes_on, const PaintOrder* request) {
    const bool fits = repair_bay_fits(car, tuning);
    const bool ready = repair_shop_ready(car, tuning);
    ResprayResult result;
    if (!driving || vehicle != visit.vehicle || !fits) {
        if (visit.spraying()) {
            result.event = ResprayEvent::Cancelled;
            result.order = visit.order;
        } else if (request) {
            result.event = ResprayEvent::Rejected;
            result.order = *request;
        }
        result.seen = visit.seen;
        visit = ResprayVisit{};
        visit.vehicle = vehicle;
        visit.outside_driving = driving && !fits;
        return result;
    }
    if (!visit.in_bay) {
        visit.in_bay = true;
        visit.pulled_in = visit.outside_driving;
    }
    if (visit.pulled_in && !visit.arrived) {
        visit.seen = visit.seen || police_eyes_on;
        if (ready) visit.arrived = true;
    }
    bool started = false;
    if (request) {
        result.order = *request;
        if (visit.arrived && ready && !visit.spraying()) {
            visit.spray_s = 0.0f;
            visit.order = *request;
            result.event = ResprayEvent::Started;
            started = true;
        } else {
            result.event = ResprayEvent::Rejected;
        }
    }
    if (visit.spraying() && !started) {
        if (!ready) {
            result.event = ResprayEvent::Cancelled;
            result.order = visit.order;
            visit.spray_s = -1.0f;
        } else {
            visit.spray_s += std::max(0.0f, dt);
            if (visit.spray_s + 1e-4f >= kRespraySeconds) {
                result.event = ResprayEvent::Completed;
                result.order = visit.order;
                visit.spray_s = -1.0f;
            }
        }
    }
    result.seen = visit.seen;
    return result;
}

enum class ResprayOutcome : uint8_t { Painted, WantedCleared, WantedKept };

// Decided from the wanted level at completion and the visit's latch, before any
// paint is applied.
constexpr ResprayOutcome respray_outcome(int wanted_level, bool seen) {
    if (wanted_level <= 0) return ResprayOutcome::Painted;
    return seen ? ResprayOutcome::WantedKept : ResprayOutcome::WantedCleared;
}

// Drops everything the pursuit holds, together, as dying does: the heat, the
// offences in flight and any arrest hold. Leaving one armed re-arrests a
// player whose stars just went. The host also resets the world's police
// context, as it does on death. Not set_level(0): that is developer setup.
inline void clear_heat_after_respray(WantedSystem& wanted, PoliceOffenseTracker& offenses,
                                     PoliceArrestTracker& arrest) {
    wanted.reset();
    offenses.reset();
    arrest.reset();
}

// The drive axes during a spray: no steer, throttle or brake, handbrake on, so
// a key still held from picker navigation cannot roll the car out and cancel
// the spray on its first step. Buttons and look are kept; Accept still reaches
// the exit check, which respray_blocks_exit() gates.
inline InputFrame hold_for_respray(InputFrame in) {
    in.steer = 0.0f;
    in.throttle = 0.0f;
    in.brake = 0.0f;
    in.handbrake = 1.0f;
    return in;
}

inline bool respray_blocks_exit(const ResprayVisit& v) {
    return v.spraying() && v.spray_s < kResprayExitGraceSeconds;
}

// The respray line of the in-car prompt. It draws on its own line.
enum class RespraySuffix : uint8_t {
    None, LotPullIn, LotWantedSeen, LotWantedClear, NeedsPullIn,
    Ready, ReadyUnseen, ReadySeen, Spraying, Unavailable
};

// driving:     in a road car, alive, not mid-transition
// on_lot:      on_repair_lot(car)
// fits:        repair_bay_fits(car, tuning)
// pending_car: the car has no paint profile yet
// eyes_on:     a cop sees the player now (the live list, render-side copy)
// visit:       the visit as the last sim step left it
struct ResprayHintInput {
    bool driving = false, on_lot = false, fits = false, pending_car = false,
         eyes_on = false;
    int wanted_level = 0;
    const ResprayVisit* visit = nullptr;
};

// Until the visit arrives the car is still pulling in, so the lot hints hold
// through the roll into the bay rather than blinking off; once the latch has
// seen a cop, the wanted hint says so even if that cop has since looked away.
constexpr RespraySuffix respray_hint(const ResprayHintInput& in) {
    if (!in.driving || in.visit == nullptr) return RespraySuffix::None;
    const ResprayVisit& v = *in.visit;
    if (v.spraying()) return RespraySuffix::Spraying;
    if (!in.fits && !in.on_lot) return RespraySuffix::None;
    if (in.pending_car) return RespraySuffix::Unavailable;
    if (in.fits && v.arrived) {
        if (in.wanted_level <= 0) return RespraySuffix::Ready;
        return v.seen ? RespraySuffix::ReadySeen : RespraySuffix::ReadyUnseen;
    }
    if (in.fits && !v.pulled_in) return RespraySuffix::NeedsPullIn;
    if (in.wanted_level <= 0) return RespraySuffix::LotPullIn;
    return in.eyes_on || v.seen ? RespraySuffix::LotWantedSeen
                                : RespraySuffix::LotWantedClear;
}

constexpr const char* respray_hint_text(RespraySuffix s) {
    switch (s) {
    case RespraySuffix::None: return "";
    case RespraySuffix::LotPullIn: return "PULL ALL THE WAY INTO A BAY - REPAIR + RESPRAY";
    case RespraySuffix::LotWantedSeen: return "PULL INTO A BAY TO LOSE THE COPS - A COP CAN SEE YOU";
    case RespraySuffix::LotWantedClear: return "PULL INTO A BAY TO LOSE THE COPS - NO COP SEES YOU";
    case RespraySuffix::NeedsPullIn: return "DRIVE OUT AND PULL IN TO RESPRAY";
    case RespraySuffix::Ready: return "R / X - RESPRAY";
    case RespraySuffix::ReadyUnseen: return "R / X - RESPRAY TO LOSE THE COPS";
    case RespraySuffix::ReadySeen: return "R / X - RESPRAY (STARS STAY - A COP SAW YOU PULL IN)";
    case RespraySuffix::Spraying: return "RESPRAYING - E / A TO ABORT";
    case RespraySuffix::Unavailable: return "RESPRAY NOT AVAILABLE FOR THIS CAR YET";
    }
    return "";
}

}  // namespace apricot
