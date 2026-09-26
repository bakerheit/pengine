#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include "core/rng.h"

namespace apricot {

// The fireball a wrecked vehicle throws, as a bounded pool of particles.
// Shaped after game/blood_particles.h, for the same reason that one is
// shaped the way it is: a fixed pool with event-keyed variation costs nothing
// to reason about, cannot leak, and replays identically.
//
// Sizes are deliberately modest. These began as lit boxes with an alpha tint,
// and scaled up far enough to fill the screen they read as orange cubes; fire
// and smoke are flame-atlas cards now (app/molotov_visual.h, WreckBlastVisual)
// and debris is still a box. The rule outlived the boxes: a burst bigger than
// the machine it came off hides the wreck that is the subject of the shot.
//
// Three kinds of particle, because an explosion that is only a fireball reads
// as a flashbulb. The fire is gone in half a second, the debris arcs away
// under gravity, and the smoke is what is still there when the player looks
// back at the wreck.
enum class WreckParticleKind : uint8_t { Fire, Smoke, Debris };

struct WreckParticle {
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};
    float age = 0.f;
    float lifetime = 0.f;
    float size = 0.f;
    float spin = 0.f;
    WreckParticleKind kind = WreckParticleKind::Fire;
    bool live = false;
};

struct WreckParticleDraw {
    glm::vec3 position{0.f};
    glm::vec3 scale{0.f};
    glm::vec4 tint{0.f};
    float spin = 0.f;
    // Which kind, and how far through its life, so a presentation that draws
    // fire and smoke off a sprite atlas can pick the card and the frame.
    WreckParticleKind kind = WreckParticleKind::Fire;
    float age = 0.f;
    bool visible = false;
};

class WreckExplosion {
public:
    static constexpr std::size_t kCapacity = 96;
    static constexpr std::size_t kFireCount = 22;
    static constexpr std::size_t kSmokeCount = 16;
    static constexpr std::size_t kDebrisCount = 14;
    static constexpr float kGravity = -17.f;
    static constexpr float kDrag = 1.1f;
    static constexpr float kSmokeRise = 2.4f;

    // `event_id` decorrelates two blasts in the same place -- the wreck's
    // `impacts` counter is exactly the right thing to pass.
    void emit(glm::vec3 origin, uint64_t event_id) {
        if (!finite(origin)) return;
        Rng rng = rng_at(0x48454C49424F4F4Dull, coord(origin.x), coord(origin.z),
                         static_cast<uint32_t>(event_id & 0xFFFFFFFFull));
        for (std::size_t i = 0; i < kFireCount; ++i)
            spawn(rng, origin, WreckParticleKind::Fire);
        for (std::size_t i = 0; i < kSmokeCount; ++i)
            spawn(rng, origin, WreckParticleKind::Smoke);
        for (std::size_t i = 0; i < kDebrisCount; ++i)
            spawn(rng, origin, WreckParticleKind::Debris);
    }

    void step(float dt) {
        if (!(dt > 0.f) || !std::isfinite(dt)) return;
        dt = std::min(dt, 1.f / 30.f);
        for (auto& p : particles_) {
            if (!p.live) continue;
            p.age += dt;
            if (p.age >= p.lifetime) { p.live = false; continue; }
            // Smoke is buoyant and fire is briefly so; debris is not.
            if (p.kind == WreckParticleKind::Debris) p.velocity.y += kGravity * dt;
            else p.velocity.y += kSmokeRise * dt;
            p.velocity -= p.velocity * std::min(1.f, kDrag * dt);
            p.position += p.velocity * dt;
        }
    }

    void clear() { particles_ = {}; }

    std::size_t live_count() const {
        std::size_t n = 0;
        for (const auto& p : particles_) n += p.live ? 1u : 0u;
        return n;
    }

    WreckParticleDraw draw(std::size_t index) const {
        WreckParticleDraw out;
        if (index >= kCapacity) return out;
        const auto& p = particles_[index];
        if (!p.live || p.lifetime <= 0.f) return out;
        const float t = std::clamp(p.age / p.lifetime, 0.f, 1.f);
        out.visible = true;
        out.kind = p.kind;
        out.age = p.age;
        out.position = p.position;
        out.spin = p.spin * p.age;
        switch (p.kind) {
            case WreckParticleKind::Fire: {
                // Swells, then collapses. Bright yellow core cooling through
                // orange to a red that hands over to the smoke behind it.
                const float swell = 1.f + t * 1.8f;
                out.scale = glm::vec3{p.size * swell};
                out.tint = {1.f, .84f - t * .62f, .28f - t * .26f,
                            std::clamp(1.f - t * t * 1.3f, 0.f, 1.f)};
                break;
            }
            case WreckParticleKind::Smoke: {
                const float swell = .6f + t * 2.6f;
                out.scale = glm::vec3{p.size * swell};
                const float grey = .30f - t * .12f;
                out.tint = {grey, grey * .95f, grey * .92f,
                            std::clamp((1.f - t) * .42f, 0.f, 1.f)};
                break;
            }
            case WreckParticleKind::Debris:
                out.scale = glm::vec3{p.size};
                out.tint = {.20f, .18f, .17f, std::clamp(1.f - t * t, 0.f, 1.f)};
                break;
        }
        return out;
    }

private:
    static bool finite(glm::vec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    static int32_t coord(float v) {
        return static_cast<int32_t>(std::floor(std::clamp(v, -1e6f, 1e6f)));
    }

    // A cone that is wider than it is tall. A sphere of particles reads as a
    // firework; a fireball off a wreck spreads along the ground.
    void spawn(Rng& rng, glm::vec3 origin, WreckParticleKind kind) {
        WreckParticle* slot = nullptr;
        for (auto& p : particles_)
            if (!p.live) { slot = &p; break; }
        if (!slot) return;
        const float angle = rng.unit_float() * 6.28318531f;
        const float spread = rng.unit_float();
        const float up = rng.unit_float();
        glm::vec3 dir{std::cos(angle) * spread, .35f + up * .9f,
                      std::sin(angle) * spread};
        dir = glm::normalize(dir + glm::vec3{0.f, 1e-5f, 0.f});

        WreckParticle p;
        p.kind = kind;
        p.live = true;
        p.position = origin + dir * (rng.unit_float() * 1.8f);
        p.spin = (rng.unit_float() - .5f) * 9.f;
        switch (kind) {
            case WreckParticleKind::Fire:
                p.velocity = dir * (5.5f + rng.unit_float() * 9.f);
                p.lifetime = .34f + rng.unit_float() * .38f;
                p.size = .40f + rng.unit_float() * .85f;
                break;
            case WreckParticleKind::Smoke:
                p.velocity = dir * (1.8f + rng.unit_float() * 3.6f);
                p.lifetime = 1.9f + rng.unit_float() * 2.3f;
                p.size = .65f + rng.unit_float() * 1.15f;
                break;
            case WreckParticleKind::Debris:
                p.velocity = dir * (9.f + rng.unit_float() * 15.f);
                p.lifetime = .9f + rng.unit_float() * 1.3f;
                p.size = .18f + rng.unit_float() * .34f;
                break;
        }
        *slot = p;
    }

    std::array<WreckParticle, kCapacity> particles_{};
};

}  // namespace apricot
