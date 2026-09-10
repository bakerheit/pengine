#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include "core/rng.h"

namespace apricot {

struct BloodDroplet {
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};
    float age = 0.f;
    float lifetime = 0.f;
    float size = 0.f;
    bool live = false;
};

struct BloodDropletDraw {
    glm::vec3 position{0.f};
    glm::vec3 scale{0.f};
    glm::vec4 tint{0.f};
    bool visible = false;
};

// Probable Cause's short forward blood cone, ported to a bounded pool with
// event-keyed variation. This state has no relation to the current gun's shot
// clock: earlier hits keep falling when the player fires again or holsters.
class BloodParticles {
public:
    static constexpr std::size_t kCapacity = 64;
    static constexpr std::size_t kBurstCount = 11;
    static constexpr float kGravity = -16.f;
    static constexpr float kDrag = 1.5f;

    void emit(glm::vec3 position, glm::vec3 direction, uint64_t event_id) {
        if (!finite(position) || !finite(direction)) return;
        const float length = glm::length(direction);
        if (!std::isfinite(length)) return;
        const glm::vec3 forward = length > .0001f ? direction / length : glm::vec3{0.f, 0.f, 1.f};
        const glm::vec3 reference = std::abs(forward.y) > .9f ? glm::vec3{1.f, 0.f, 0.f} : glm::vec3{0.f, 1.f, 0.f};
        const glm::vec3 right = glm::normalize(glm::cross(forward, reference));
        const glm::vec3 up = glm::cross(right, forward);
        for (std::size_t i = 0; i < kBurstCount; ++i) {
            auto& drop = droplets_[next_];
            next_ = (next_ + 1u) % kCapacity;
            const auto random = [event_id, i](uint32_t channel) {
                const uint64_t bits = hash_coord3(event_id ^ 0xB100D715ull,
                    static_cast<int32_t>(i), 0, channel);
                return static_cast<float>(bits >> 40) / 16777215.f;
            };
            const glm::vec3 axis = glm::normalize(forward + right * (-.45f + .9f * random(0))
                                                           + up * (-.20f + .75f * random(1)));
            drop.position = position;
            drop.velocity = axis * (2.f + 3.f * random(2));
            drop.age = 0.f;
            drop.lifetime = .22f + .23f * random(3);
            // At normal aiming distance this matches the original sprite's
            // readable footprint; centimetre specks vanished against walls.
            drop.size = .05f + .05f * random(4);
            drop.live = true;
        }
    }

    void step(float dt) {
        if (!std::isfinite(dt) || dt <= 0.f) return;
        const float drag = std::exp(-kDrag * dt);
        for (auto& drop : droplets_) {
            if (!drop.live) continue;
            drop.age += dt;
            if (drop.age >= drop.lifetime) {
                drop.live = false;
                continue;
            }
            drop.velocity.y += kGravity * dt;
            drop.velocity *= drag;
            drop.position += drop.velocity * dt;
        }
    }

    void clear() { droplets_ = {}; next_ = 0; }
    const std::array<BloodDroplet, kCapacity>& droplets() const { return droplets_; }
    std::size_t live_count() const {
        return static_cast<std::size_t>(std::count_if(droplets_.begin(), droplets_.end(),
            [](const auto& drop) { return drop.live; }));
    }
    BloodDropletDraw draw(std::size_t index) const {
        BloodDropletDraw out;
        if (index >= kCapacity) return out;
        const auto& drop = droplets_[index];
        if (!drop.live) return out;
        const float life = std::clamp(drop.age / drop.lifetime, 0.f, 1.f);
        // The original sprites were unlit. A stronger crimson albedo keeps
        // this lit geometry readable in shade without an emissive boost.
        const glm::vec3 color = glm::mix(glm::vec3{.82f, .04f, .03f}, glm::vec3{.22f, 0.f, 0.f}, life);
        out.position = drop.position;
        // The scene path is opaque. Shrinking area supplies the old squared
        // fade without treating body hits as glowing or transparent sparks.
        out.scale = glm::vec3{.65f, 1.f, .65f} * drop.size * (1.f - life);
        out.tint = glm::vec4{color, 1.f};
        out.visible = true;
        return out;
    }

private:
    std::array<BloodDroplet, kCapacity> droplets_{};
    std::size_t next_ = 0;
    static bool finite(glm::vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }
};

}  // namespace apricot
