#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>

#include "core/log.h"

namespace pengine {

bool Application::init() {
    WindowConfig cfg;
    cfg.title = "pengine [phase 0: bootstrap]";
    if (!window_.init(cfg)) return false;

    glViewport(0, 0, window_.width(), window_.height());
    glClearColor(0.05f, 0.07f, 0.10f, 1.0f);

    fps_window_start_ = Clock::now();
    running_ = true;
    return true;
}

int Application::run() {
    while (running_) {
        process_events();

        auto tick = clock_.advance();
        for (int i = 0; i < tick.updates; ++i) {
            update(clock_.fixed_dt);
        }
        render(tick.alpha);

        ++fps_frames_;
        if (seconds_since(fps_window_start_) >= 1.0) {
            PE_INFO("frame %d  (~%d Hz)", fps_frames_, fps_frames_);
            fps_frames_ = 0;
            fps_window_start_ = Clock::now();
        }
    }
    return 0;
}

void Application::shutdown() {
    window_.shutdown();
}

void Application::process_events() {
    input_.begin_frame();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                running_ = false;
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    int w = 0, h = 0;
                    SDL_GL_GetDrawableSize(window_.sdl(), &w, &h);
                    window_.on_resize(w, h);
                    glViewport(0, 0, w, h);
                }
                break;
            default:
                input_.handle_event(e);
                break;
        }
    }

    if (input_.pressed(SDL_SCANCODE_ESCAPE)) {
        running_ = false;
    }
}

void Application::update(double /*dt*/) {
    // Phase 0: nothing to update yet.
}

void Application::render(double /*alpha*/) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        PE_WARN("GL error 0x%x", err);
    }

    window_.swap();
}

} // namespace pengine
