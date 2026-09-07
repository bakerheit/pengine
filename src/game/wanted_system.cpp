#include "game/wanted_system.h"

#include <algorithm>

namespace apricot {

void WantedSystem::add_heat(float amount, Crime crime) {
    if (!enabled_) return;
    if (amount > 0.0f) {
        crime_pending_ = true;
        crime_kind_ = crime;
    }
    heat_ = std::clamp(heat_ + amount, 0.0f, 15.0f);
    lose_track_timer_ = 0.0f;
    recompute_level();
}

void WantedSystem::update(float dt, bool in_police_view,
                          const PoliceTuning& tuning) {
    const HeatDecay next = wanted_heat_decay_step(
        heat_, lose_track_timer_, in_police_view, dt, tuning);
    heat_ = next.heat;
    lose_track_timer_ = next.lose_track_timer;
    recompute_level();
}

void WantedSystem::reset() {
    heat_ = 0.0f;
    lose_track_timer_ = 0.0f;
    level_ = 0;
    crime_pending_ = false;
    crime_kind_ = Crime::Other;
}

void WantedSystem::set_level(int level) {
    static constexpr float kHeatForLevel[] = {
        0.0f, 1.0f, 3.0f, 6.0f, 9.0f, 12.0f,
    };
    const int selected = std::clamp(level, 0, 5);
    heat_ = kHeatForLevel[selected];
    lose_track_timer_ = 0.0f;
    crime_pending_ = false;
    crime_kind_ = Crime::Other;
    recompute_level();
}

void WantedSystem::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) reset();
}

bool WantedSystem::take_crime_report(Crime& out) {
    if (!crime_pending_) return false;
    out = crime_kind_;
    crime_pending_ = false;
    return true;
}

void WantedSystem::recompute_level() {
    level_ = heat_ >= 12.0f ? 5
           : heat_ >= 9.0f  ? 4
           : heat_ >= 6.0f  ? 3
           : heat_ >= 3.0f  ? 2
           : heat_ > 0.0f   ? 1
                            : 0;
}

}  // namespace apricot
