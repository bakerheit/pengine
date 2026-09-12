#include <limits>
#include "game/wreck_explosion.h"
#include "test_assert.h"
using namespace apricot;

int main() {
    WreckExplosion blast;
    REQUIRE(blast.live_count() == 0u);
    REQUIRE(!blast.draw(0).visible);
    REQUIRE(!blast.draw(WreckExplosion::kCapacity).visible);   // out of range

    // One bang fills the pool with all three kinds and nothing more.
    blast.emit({10, 4, -6}, 1);
    const std::size_t lit = blast.live_count();
    REQUIRE(lit == WreckExplosion::kFireCount + WreckExplosion::kSmokeCount +
                   WreckExplosion::kDebrisCount);
    REQUIRE(lit <= WreckExplosion::kCapacity);

    // Deterministic: the same place and the same event id give the same blast,
    // because a replayed session has to produce the same frame.
    WreckExplosion twin;
    twin.emit({10, 4, -6}, 1);
    for (std::size_t i = 0; i < WreckExplosion::kCapacity; ++i) {
        const auto a = blast.draw(i), b = twin.draw(i);
        REQUIRE(a.visible == b.visible);
        REQUIRE(a.position == b.position && a.scale == b.scale && a.tint == b.tint);
    }
    // ...and a different event id in the same place does not.
    WreckExplosion other;
    other.emit({10, 4, -6}, 2);
    bool differs = false;
    for (std::size_t i = 0; i < WreckExplosion::kCapacity; ++i)
        if (other.draw(i).position != blast.draw(i).position) { differs = true; break; }
    REQUIRE(differs);

    // It starts at the wreck, not spread across the map.
    for (std::size_t i = 0; i < WreckExplosion::kCapacity; ++i) {
        const auto d = blast.draw(i);
        if (!d.visible) continue;
        REQUIRE(glm::length(d.position - glm::vec3{10, 4, -6}) < 2.f);
        REQUIRE(d.tint.a > 0.f);
    }

    // The pool is bounded. Twenty blasts must not overrun it or wrap it.
    for (uint64_t i = 0; i < 20; ++i) blast.emit({10, 4, -6}, i + 3);
    REQUIRE(blast.live_count() <= WreckExplosion::kCapacity);

    // It burns out on its own and leaves nothing behind.
    WreckExplosion fade;
    fade.emit({0, 20, 0}, 7);
    REQUIRE(fade.live_count() > 0u);
    for (int i = 0; i < 1200; ++i) fade.step(1.f / 120);
    REQUIRE(fade.live_count() == 0u);
    for (std::size_t i = 0; i < WreckExplosion::kCapacity; ++i)
        REQUIRE(!fade.draw(i).visible);

    // Fire is brief and smoke is not: half a second in, the flame is going or
    // gone while the smoke is still there. That ordering is the whole look.
    WreckExplosion timed;
    timed.emit({0, 30, 0}, 11);
    for (int i = 0; i < 90; ++i) timed.step(1.f / 120);     // 0.75 s
    int bright = 0, dim = 0;
    for (std::size_t i = 0; i < WreckExplosion::kCapacity; ++i) {
        const auto d = timed.draw(i);
        if (!d.visible) continue;
        if (d.tint.r > .8f) ++bright; else ++dim;
    }
    REQUIRE(bright == 0);
    REQUIRE(dim > 0);

    // Debris falls, smoke rises. Same blast, opposite ends of the plume.
    WreckExplosion plume;
    plume.emit({0, 50, 0}, 13);
    for (int i = 0; i < 180; ++i) plume.step(1.f / 120);
    float highest = -1e9f, lowest = 1e9f;
    for (std::size_t i = 0; i < WreckExplosion::kCapacity; ++i) {
        const auto d = plume.draw(i);
        if (!d.visible) continue;
        highest = std::max(highest, d.position.y);
        lowest = std::min(lowest, d.position.y);
    }
    REQUIRE(highest > 50.f);
    REQUIRE(lowest < highest);

    // Degenerate input is ignored rather than poisoning the pool.
    WreckExplosion guard;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    guard.emit({nan, 0, 0}, 1);
    REQUIRE(guard.live_count() == 0u);
    guard.emit({0, 5, 0}, 1);
    const std::size_t before = guard.live_count();
    guard.step(0.f);
    guard.step(-1.f);
    guard.step(nan);
    REQUIRE(guard.live_count() == before);
    guard.clear();
    REQUIRE(guard.live_count() == 0u);

    apricot_test::pass("wreck explosion emits, is deterministic, bounded, "
                       "ordered fire-before-smoke, rises and falls, and burns out");
    return apricot_test::done("wreck_explosion_tests");
}
