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

    // Width / height, and NEVER a division by zero.
    //
    // A minimised window reports a drawable size of 0x0. Dividing by that
    // gives inf, the projection matrix becomes all-NaN, the frustum planes
    // become NaN, every comparison against them is false, and the entire world
    // is culled. The window then restores to a permanently black screen, which
    // looks like a renderer bug and is nothing of the kind. So this holds the
    // LAST GOOD aspect across a minimise instead: the size you had is a far
    // better guess than a number that poisons everything it touches.
    float aspect() const { return aspect_; }

    // True while the window has no drawable area — minimised, or collapsed to
    // zero on some window managers mid-drag. Skip rendering; do not skip the
    // sim, which must keep time whether or not anyone is looking.
    bool minimised() const { return width_ <= 0 || height_ <= 0; }

    // Feed the drawable size after a resize event. Clamps negatives, tracks
    // the minimised state and keeps aspect() valid. Logs the new size at
    // debug level — a resize that silently does not reach the projection is a
    // stretched image nobody can explain.
    void on_resize(int w, int h);

    // Set the GL viewport to the whole drawable area. No-ops while minimised
    // rather than issuing a zero-area viewport.
    void apply_viewport() const;

private:
    SDL_Window* window_ = nullptr;
    void* gl_context_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    // Seeded from the requested size at init, so it is valid before the first
    // resize event ever arrives.
    float aspect_ = 16.0f / 9.0f;
};

}  // namespace apricot
