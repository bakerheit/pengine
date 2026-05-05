#include "game/wanted_system.h"

#include <algorithm>

namespace pengine {

void WantedSystem::add_heat(float amount) {
    heat_ = std::clamp(heat_ + amount, 0.f, 15.f);
    decay_delay_ = 10.f;
    recompute_level();
}

void WantedSystem::update(float dt) {
    if (heat_ <= 0.f) {
        heat_ = 0.f;
        level_ = 0;
        decay_delay_ = 0.f;
        return;
    }

    if (decay_delay_ > 0.f) {
        decay_delay_ = std::max(0.f, decay_delay_ - dt);
    } else {
        // A level-1 mistake clears quickly; higher levels take longer but
        // still decay for this first playable pass.
        heat_ = std::max(0.f, heat_ - dt * 0.22f);
    }
    recompute_level();
}

void WantedSystem::reset() {
    heat_ = 0.f;
    decay_delay_ = 0.f;
    level_ = 0;
}

void WantedSystem::recompute_level() {
    level_ = heat_ >= 12.f ? 5
           : heat_ >=  9.f ? 4
           : heat_ >=  6.f ? 3
           : heat_ >=  3.f ? 2
           : heat_ >   0.f ? 1 : 0;
}

} // namespace pengine
