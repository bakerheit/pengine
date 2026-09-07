#pragma once
#include <algorithm>
#include <cmath>

namespace apricot::cutscene {
// Host advances this timeline; gameplay simulation stays paused throughout.
struct Playback {
    float time=0, duration=0;
    bool active=false, paused=false;
    void start(float seconds) {
        time=0;duration=std::isfinite(seconds)?std::max(seconds,0.f):0;
        active=duration>0;paused=false;
    }
    bool advance(float dt) {
        if (!active||paused||!std::isfinite(dt)||dt<=0) return false;
        time=std::min(duration,time+dt);
        if (time<duration) return false;
        active=false;return true;
    }
    bool skip() {
        if (!active) return false;
        time=duration;active=false;paused=false;return true;
    }
};
}
