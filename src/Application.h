#pragma once

#include "core/time.h"
#include "platform/input.h"
#include "platform/window.h"

namespace pengine {

class Application {
public:
    bool init();
    int  run();
    void shutdown();

private:
    void process_events();
    void update(double dt);
    void render(double alpha);

    Window window_;
    Input  input_;
    FixedTimestep clock_;

    bool running_ = false;

    // Frame counter (1 Hz log).
    TimePoint fps_window_start_{};
    int       fps_frames_ = 0;
};

} // namespace pengine
