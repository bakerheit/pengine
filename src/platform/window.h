#pragma once

#include <string>

struct SDL_Window;

namespace apricot {

// The OS window and its GL 3.3 core context.
//
// Forward-declares SDL_Window rather than including the platform header, so
// including this from app code does not drag the whole windowing library into
// every translation unit that merely wants a width.

struct WindowConfig {
    std::string title = "apricot";
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

    // Creates the window and a GL 3.3 CORE profile context, then loads GL
    // entry points. Returns false (having logged the specific failure) rather
    // than throwing; the caller decides whether it can continue.
    bool init(const WindowConfig& cfg);
    void shutdown();

    void swap();

    // Returns the swap interval actually settled on — drivers are free to
    // refuse — so callers can mirror the true state rather than what they
    // asked for.
    bool set_vsync(bool on);

    SDL_Window* sdl() const { return window_; }
    void* gl_context() const { return gl_context_; }

    // DRAWABLE size in pixels, not logical points. On a Retina display the two
    // differ by the backing scale factor, and using logical size for the GL
    // viewport renders the frame into the bottom-left quarter of the window.
    int width() const { return width_; }
    int height() const { return height_; }

    void on_resize(int w, int h) {
        width_ = w;
        height_ = h;
    }

private:
    SDL_Window* window_ = nullptr;
    void* gl_context_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace apricot
