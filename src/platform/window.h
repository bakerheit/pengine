#pragma once

#include <string>

struct SDL_Window;

namespace pengine {

struct WindowConfig {
    std::string title = "pengine";
    int width = 1280;
    int height = 720;
    bool vsync = true;
};

class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool init(const WindowConfig& cfg);
    void shutdown();

    void swap();

    SDL_Window* sdl() const { return window_; }
    void* gl_context() const { return gl_context_; }

    int width() const { return width_; }
    int height() const { return height_; }
    void on_resize(int w, int h) { width_ = w; height_ = h; }

private:
    SDL_Window* window_ = nullptr;
    void* gl_context_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

} // namespace pengine
