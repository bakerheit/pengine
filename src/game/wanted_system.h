#pragma once

#include "city/police_ai.h"

namespace apricot {

// What the HUD's cooldown meter reads. The escape has two legs: the level's
// lose-track grace (heat frozen, nobody can see you) and then the drain at
// heat_decay_rate. Both are deterministic while you stay unseen, so the
// countdown is exact, not an estimate — a cop sighting you resets it to Seen.
struct WantedCooldown {
    enum class Phase { Clear, Seen, LosingThem, Cooling };
    Phase phase = Phase::Clear;
    float seconds_to_clear = 0.0f;  // unseen from here on: both legs remaining
    float grace_left_s = 0.0f;      // of the lose-track window still to run
    // seconds_to_clear over the whole escape as it stood when the last
    // sighting ended: 1 the moment you break contact, 0 when the stars go.
    float remaining_fraction = 0.0f;
};

// Player notoriety lifted from the Probable Cause alpha. Crimes add heat;
// heat is bucketed into the five HUD/response levels. Cooling is gated by
// police contact so ducking around a corner matters more than a raw timer.
class WantedSystem {
public:
    enum class Crime {
        Other,
        Violent,
        VehicleTheft,
        Assault,
        VehicularAssault,
        OfficerAssault,
        TrafficViolation,
        PoliceVehicleCollision,
        ArmedThreat,
    };

    void add_heat(float amount, Crime crime = Crime::Other);
    void update(float dt, bool in_police_view, const PoliceTuning& tuning);
    void reset();
    // Host-side developer setup. Unlike add_heat(), this does not report a
    // crime; it places the system directly into the requested response bucket.
    void set_level(int level);

    void set_enabled(bool enabled);
    bool enabled() const { return enabled_; }

    int level() const { return level_; }
    float heat() const { return heat_; }
    WantedCooldown cooldown(const PoliceTuning& tuning) const;

    // Drains the newest report once. Several heat bumps before a drain collapse
    // into one report, matching the alpha's anti-spam contract.
    bool take_crime_report(Crime& out);

private:
    void recompute_level();
    void mark_escape_start();

    bool enabled_ = true;
    float heat_ = 0.0f;
    float lose_track_timer_ = 0.0f;
    bool seen_ = false;
    // The escape as it stood before any heat drained: the window of the level
    // you broke contact at, and the heat the drain starts from. HUD only —
    // nothing in the heat step reads them.
    float escape_window_s_ = 0.0f;
    float escape_heat_ = 0.0f;
    int level_ = 0;
    bool crime_pending_ = false;
    Crime crime_kind_ = Crime::Other;
};

}  // namespace apricot
