#include "game/vehicle_effects.h"

#include <algorithm>

#include <glm/glm.hpp>

#include "audio/audio_engine.h"
#include "game/traffic.h"
#include "game/vehicle.h"
#include "render/particles.h"
#include "world/road_grid.h"

namespace pengine::vehicle_effects {

void update(float dt, const TrafficSystem& traffic,
            Particles& particles, AudioEngine& audio) {
    // Spark emission: any car whose substep recorded a scraping chassis
    // corner over a paved surface (road or sidewalk) sprays a burst of
    // sparks. Gated to paved surfaces so dirt / grass off-road slides
    // stay silent and visually clean.
    float scrape_max_speed = 0.f;
    auto try_emit = [&](const TrafficSystem::Car& c) {
        for (const auto& sc : c.vehicle.scrape_contacts()) {
            if (!is_paved_surface(sc.world_pos.x, sc.world_pos.z))
                continue;
            // Tangential speed gates emission and density — slow drags
            // get a couple of weak sparks; high-speed scrapes shower.
            glm::vec3 tan = sc.world_vel; tan.y = 0.f;
            float tan_speed = glm::length(tan);
            if (tan_speed < 2.0f) continue;
            int count = 3 + static_cast<int>(std::min(10.f,
                            tan_speed * 0.4f));
            particles.emit_sparks(sc.world_pos, sc.world_vel, count);
            if (tan_speed > scrape_max_speed)
                scrape_max_speed = tan_speed;
        }
    };
    if (auto* pc = traffic.player_car()) try_emit(*pc);
    // Parked cars (e.g. just-hit AI) might also be scraping. AI cars
    // are kinematic and never populate scrape_contacts, so the filter
    // is defensive rather than load-bearing.
    for (const auto& cp : traffic.cars()) {
        if (!cp || cp.get() == traffic.player_car()) continue;
        if (cp->driver != TrafficSystem::Driver::AI) try_emit(*cp);
    }

    // Drive the looping metal-scrape sound off the same per-frame max.
    // Sample is already a real metal-on-concrete scrape, so pitch sits
    // near 1.0 — small speed-driven nudges add life without changing
    // the timbre. The sample itself is mastered quiet, so we drive
    // intensity well above 1.0 (miniaudio amplifies past unity) and
    // floor it with a non-zero MIN so even slow drags are audible.
    constexpr float REF_SPEED   = 12.f;  // m/s for full-intensity scrape
    constexpr float MAX_VOLUME  = 4.0f;
    constexpr float MIN_VOLUME  = 1.2f;  // floor when scrape is just starting
    constexpr float BASE_PITCH  = 0.85f;
    constexpr float PITCH_RANGE = 0.30f;
    float t = std::min(1.f, scrape_max_speed / REF_SPEED);
    float intensity = (scrape_max_speed > 0.f)
                        ? MIN_VOLUME + (MAX_VOLUME - MIN_VOLUME) * t
                        : 0.f;
    float pitch     = BASE_PITCH + PITCH_RANGE * t;
    audio.update_scrape(dt, intensity, pitch);
}

} // namespace pengine::vehicle_effects
