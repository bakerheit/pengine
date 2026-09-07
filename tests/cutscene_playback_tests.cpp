#include "game/cutscene_playback.h"
#include "test_assert.h"
#include <limits>
using namespace apricot::cutscene;
int main() {
    Playback p;p.start(69.41f);REQUIRE(p.active);
    REQUIRE(!p.advance(34));REQUIRE(p.time==34);
    p.paused=true;REQUIRE(!p.advance(100));REQUIRE(p.time==34&&p.active);
    p.paused=false;REQUIRE(!p.advance(-1));REQUIRE(!p.advance(std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(p.advance(100));REQUIRE(!p.active&&p.time==69.41f);
    REQUIRE(!p.advance(1));REQUIRE(!p.skip());
    p.start(69.41f);p.paused=true;REQUIRE(p.skip());
    REQUIRE(!p.active&&!p.paused&&p.time==69.41f);
    p.start(69.41f);REQUIRE(p.time==0&&!p.paused);
    p.start(0);REQUIRE(!p.active);
    p.start(std::numeric_limits<float>::infinity());REQUIRE(!p.active);
}
