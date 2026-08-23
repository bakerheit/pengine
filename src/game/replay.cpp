#include "game/replay.h"

namespace apricot {

bool replay_input(const ReplayTape& tape, uint64_t frame, InputFrame& out) {
    if (frame >= tape.frames.size()) return false;
    out = tape.frames[static_cast<std::size_t>(frame)];
    return true;
}

}  // namespace apricot
