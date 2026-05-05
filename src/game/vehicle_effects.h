#pragma once

namespace pengine {

class AudioEngine;
class Particles;
class TrafficSystem;

// Per-frame chassis-scrape effects: spark emission at every paved-surface
// scrape contact, and the looping metal-scrape audio voice driven by the
// max tangential speed seen this frame.
namespace vehicle_effects {

void update(float dt, const TrafficSystem& traffic,
            Particles& particles, AudioEngine& audio);

} // namespace vehicle_effects
} // namespace pengine
