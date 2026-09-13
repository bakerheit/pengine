#include "platform/window.h"

#include <SDL.h>
#include <glad/gl.h>

#include "core/log.h"
#include "platform/foreground.h"

namespace apricot {

Window::~Window() { shutdown(); }

bool Window::init(const WindowConfig& cfg) {
    // An unattended run must not take the desktop. SDL's half of that is this
    // hint: with it set, SDL neither forces a Regular activation policy nor
    // calls -activateIgnoringOtherApps: on launch. It has to be set BEFORE
    // SDL_Init, because the video subsystem reads it on the way up and a hint
    // set afterwards silently does nothing — the same trap the MSAA attributes
    // below carry. The OTHER half is keep_out_of_foreground(), just after.
    SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, cfg.take_focus ? "0" : "1");


    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        AP_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    // Now that SDL has brought the application object into existence, and
    // before any window does. See platform/foreground.h for why the hint above
    // is not enough on its own.
    if (!cfg.take_focus) platform::keep_out_of_foreground();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    // Required on macOS to get anything above GL 2.1 at all.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    // 4x MSAA on the default framebuffer. Must be requested before the window
    // and context exist; setting it afterwards silently does nothing.
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    Uint32 flags =
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    // HIDDEN, not merely unfocused. A scripted check has nobody watching it,
    // and a window that appears covers a third of somebody's screen whether or
    // not it holds the keyboard. Rendering to it still works: the drawable is
    // real, the frames are real, and --screenshot reads them back exactly as
    // before — which is measured, not assumed, by every check that captures.
    // Use --attended when you want to watch one.
    if (!cfg.take_focus) flags |= SDL_WINDOW_HIDDEN;
    window_ = SDL_CreateWindow(cfg.title.c_str(), SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, cfg.width, cfg.height,
                               flags);
    if (!window_) {
        AP_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_) {
        AP_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }

    if (SDL_GL_MakeCurrent(window_, gl_context_) != 0) {
        AP_ERROR("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        return false;
    }

    const int gl_version =
        gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
    if (gl_version == 0) {
        AP_ERROR("gladLoadGL failed: no GL entry points loaded");
        return false;
    }

    // Verify what we actually GOT, not what we asked for. A driver may hand
    // back a lower context than requested, and the failure then arrives much
    // later as an unexplained blank screen. macOS legitimately returns 4.1
    // core for any 3.2+ core request, so this is a floor, not an equality.
    const int major = GLAD_VERSION_MAJOR(gl_version);
    const int minor = GLAD_VERSION_MINOR(gl_version);
    if (major < 3 || (major == 3 && minor < 3)) {
        AP_ERROR("need GL 3.3 core, got %d.%d", major, minor);
        return false;
    }

    if (SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0) != 0) {
        // Not fatal: some drivers refuse, and the app runs fine uncapped.
        AP_WARN("SDL_GL_SetSwapInterval failed: %s", SDL_GetError());
    }

    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    on_resize(w, h);

    AP_INFO("GL %d.%d core (%s)", major, minor,
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    AP_INFO("window %dx%d drawable, aspect %.4f, swap interval=%d (requested %d)",
            width_, height_, static_cast<double>(aspect_),
            SDL_GL_GetSwapInterval(), cfg.vsync ? 1 : 0);
    return true;
}

void Window::on_resize(int w, int h) {
    // Negative sizes are not a thing, but a window manager mid-drag has been
    // known to report one. Clamp rather than propagate.
    width_ = w > 0 ? w : 0;
    height_ = h > 0 ? h : 0;

    // Only recompute the aspect when there is a real area to compute it from.
    // While minimised, aspect_ deliberately keeps its previous value — see the
    // note in window.h about NaN projections and a permanently black restore.
    if (width_ > 0 && height_ > 0) {
        aspect_ = static_cast<float>(width_) / static_cast<float>(height_);
        AP_DEBUG("resize: %dx%d drawable, aspect %.4f", width_, height_,
                 static_cast<double>(aspect_));
    } else {
        AP_DEBUG("resize: minimised (%dx%d); holding aspect %.4f", w, h,
                 static_cast<double>(aspect_));
    }
}

void Window::apply_viewport() const {
    if (minimised()) return;
    glViewport(0, 0, width_, height_);
}

void Window::shutdown() {
    if (gl_context_) {
        SDL_GL_DeleteContext(gl_context_);
        gl_context_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Window::swap() {
    if (window_) SDL_GL_SwapWindow(window_);
}

bool Window::set_vsync(bool on) {
    if (SDL_GL_SetSwapInterval(on ? 1 : 0) != 0) {
        AP_WARN("SDL_GL_SetSwapInterval(%d) failed: %s", on ? 1 : 0,
                SDL_GetError());
        return SDL_GL_GetSwapInterval() != 0;
    }
    return SDL_GL_GetSwapInterval() != 0;
}

}  // namespace apricot
