#pragma once

namespace pengine {

// Player notoriety: a single heat counter that decays back to zero after a
// brief grace period. Levels are bucketed thresholds (1..5) used by the
// HUD stars and by traffic/pedestrians to drive police response.
class WantedSystem {
public:
    void  add_heat(float amount);
    void  update(float dt);
    void  reset();

    int   level() const { return level_; }
    float heat()  const { return heat_;  }

private:
    void recompute_level();

    float heat_        = 0.f;
    float decay_delay_ = 0.f;
    int   level_       = 0;
};

} // namespace pengine
