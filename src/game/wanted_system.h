#pragma once

#include "city/police_ai.h"

namespace apricot {

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

    // Drains the newest report once. Several heat bumps before a drain collapse
    // into one report, matching the alpha's anti-spam contract.
    bool take_crime_report(Crime& out);

private:
    void recompute_level();

    bool enabled_ = true;
    float heat_ = 0.0f;
    float lose_track_timer_ = 0.0f;
    int level_ = 0;
    bool crime_pending_ = false;
    Crime crime_kind_ = Crime::Other;
};

}  // namespace apricot
