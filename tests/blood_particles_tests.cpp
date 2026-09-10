#include "game/blood_particles.h"
#include "test_assert.h"
#include <limits>

using namespace apricot;

namespace {
void same_droplets(const BloodParticles& a, const BloodParticles& b) {
    for (std::size_t i = 0; i < BloodParticles::kCapacity; ++i) {
        const auto& left = a.droplets()[i];
        const auto& right = b.droplets()[i];
        REQUIRE(left.position == right.position);
        REQUIRE(left.velocity == right.velocity);
        REQUIRE(left.age == right.age);
        REQUIRE(left.lifetime == right.lifetime);
        REQUIRE(left.size == right.size);
        REQUIRE(left.live == right.live);
    }
}
}

int main() {
    BloodParticles first;
    BloodParticles second;
    const glm::vec3 hit{3.f, 2.f, -5.f};
    const glm::vec3 direction{0.f, 0.f, 1.f};
    first.emit(hit, direction, 17);
    second.emit(hit, direction, 17);
    REQUIRE(first.live_count() == BloodParticles::kBurstCount);
    same_droplets(first, second);
    for (std::size_t i = 0; i < BloodParticles::kBurstCount; ++i) {
        const auto& drop = first.droplets()[i];
        const auto draw = first.draw(i);
        REQUIRE(drop.position == hit);
        REQUIRE(glm::dot(glm::normalize(drop.velocity), direction) > .80f);
        REQUIRE(glm::length(drop.velocity) >= 1.999f);
        REQUIRE(glm::length(drop.velocity) <= 5.001f);
        REQUIRE(drop.lifetime >= .22f && drop.lifetime <= .45f);
        REQUIRE(draw.visible);
        REQUIRE(draw.position == drop.position);
        REQUIRE(draw.tint.r > draw.tint.g * 10.f);
        REQUIRE(draw.tint.a == 1.f);  // no emissive boost on a body hit
        REQUIRE(draw.scale.y >= .05f && draw.scale.y <= .10f);
    }
    // A body hit must cover several pixels at a normal six-metre aim range,
    // including the narrow axis, or shaded walls swallow the whole spray.
    constexpr float focal_pixels = 1080.f / (2.f * .577350269f);
    for (std::size_t i = 0; i < BloodParticles::kBurstCount; ++i) {
        const auto draw = first.draw(i);
        REQUIRE(draw.scale.x * focal_pixels / 6.f >= 5.f);
        REQUIRE(draw.tint.r >= .8f);
        REQUIRE(draw.tint.a == 1.f);
    }
    const auto initial = first.droplets()[0];
    const auto initial_draw = first.draw(0);
    constexpr float dt = 1.f / 120.f;
    first.step(dt);
    second.step(dt);
    same_droplets(first, second);
    const auto& moved = first.droplets()[0];
    const auto moved_draw = first.draw(0);
    REQUIRE(moved.velocity.y < initial.velocity.y);
    REQUIRE(moved.position.z > initial.position.z);
    REQUIRE(moved_draw.tint.r < initial_draw.tint.r);
    REQUIRE(moved_draw.scale.y < initial_draw.scale.y);
    REQUIRE(moved_draw.position == moved.position);
    apricot_test::pass("real droplet state emits a bounded crimson cone and draw state darkens");

    for (int i = 0; i < 60; ++i) {
        first.step(dt);
        second.step(dt);
    }
    same_droplets(first, second);
    REQUIRE(first.live_count() == 0);
    for (std::size_t i = 0; i < BloodParticles::kCapacity; ++i)
        REQUIRE(!first.draw(i).visible);
    apricot_test::pass("fixed-step gravity is deterministic and all droplets expire");

    for (uint64_t event = 0; event < 1000; ++event) {
        first.emit(hit, direction, event);
        REQUIRE(first.live_count() <= BloodParticles::kCapacity);
    }
    REQUIRE(first.live_count() == BloodParticles::kCapacity);
    first.clear();
    REQUIRE(first.live_count() == 0);
    REQUIRE(!first.draw(BloodParticles::kCapacity).visible);
    first.emit(hit, direction, 17);
    BloodParticles fresh;
    fresh.emit(hit, direction, 17);
    same_droplets(first, fresh);
    apricot_test::pass("burst spam recycles a fixed pool and clear resets its draw state");

    BloodParticles variants;
    variants.emit(hit, direction, 18);
    REQUIRE(variants.droplets()[0].velocity != first.droplets()[0].velocity);
    variants.clear();
    variants.emit(hit, {0.f, 0.f, 0.f}, 19);
    variants.emit(hit, {0.f, 1.f, 0.f}, 20);
    REQUIRE(variants.live_count() == BloodParticles::kBurstCount * 2u);
    for (const auto& drop : variants.droplets()) {
        if (!drop.live) continue;
        REQUIRE(std::isfinite(drop.velocity.x));
        REQUIRE(std::isfinite(drop.velocity.y));
        REQUIRE(std::isfinite(drop.velocity.z));
    }
    const auto before = variants;
    variants.step(-1.f);
    variants.step(std::numeric_limits<float>::quiet_NaN());
    variants.emit(hit, {0.f, std::numeric_limits<float>::infinity(), 0.f}, 21);
    same_droplets(variants, before);
    apricot_test::pass("event IDs vary spray and vertical or missing directions stay finite");
    return apricot_test::done("blood_particles_tests");
}
