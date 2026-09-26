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
    mark_escape_start();
}

void WantedSystem::update(float dt, bool in_police_view,
                          const PoliceTuning& tuning) {
    // The window in force is the one for the level you HAD when you broke
    // line of sight: read before the step, so cooling through a level
    // boundary never lengthens the grace mid-escape.
    const HeatDecay next = wanted_heat_decay_step(
        heat_, lose_track_timer_, in_police_view, dt,
        police_level_profile(level_).escape_s, tuning);
    heat_ = next.heat;
    lose_track_timer_ = next.lose_track_timer;
    seen_ = in_police_view && heat_ > 0.0f;
    recompute_level();
    // Still inside the grace, heat has not moved: this is the escape's start.
    if (lose_track_timer_ < police_level_profile(level_).escape_s)
        mark_escape_start();
}

WantedCooldown WantedSystem::cooldown(const PoliceTuning& tuning) const {
    WantedCooldown out;
    if (heat_ <= 0.0f) return out;
    if (seen_) {
        out.phase = WantedCooldown::Phase::Seen;
        out.remaining_fraction = 1.0f;
        return out;
    }
    // The window in force is the current level's. Once the drain has begun
    // the timer is past the higher level's window, so it is past every lower
    // one too, and grace_left stays zero as the stars drop.
    const float window = police_level_profile(level_).escape_s;
    const float rate = std::max(tuning.heat_decay_rate, 1e-4f);
    out.grace_left_s = std::max(0.0f, window - lose_track_timer_);
    out.phase = out.grace_left_s > 0.0f ? WantedCooldown::Phase::LosingThem
                                        : WantedCooldown::Phase::Cooling;
    out.seconds_to_clear = out.grace_left_s + heat_ / rate;
    const float total = escape_window_s_ + escape_heat_ / rate;
    out.remaining_fraction =
        total > 0.0f ? std::clamp(out.seconds_to_clear / total, 0.0f, 1.0f) : 0.0f;
    return out;
}

void WantedSystem::mark_escape_start() {
    escape_window_s_ = police_level_profile(level_).escape_s;
    escape_heat_ = heat_;
}

void WantedSystem::reset() {
    heat_ = 0.0f;
    lose_track_timer_ = 0.0f;
    seen_ = false;
    escape_window_s_ = 0.0f;
    escape_heat_ = 0.0f;
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
    mark_escape_start();
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
