#include "platform/window.h"

#include <SDL.h>
#include <glad/gl.h>

#include "core/log.h"

namespace pengine {

Window::~Window() {
    shutdown();
}

bool Window::init(const WindowConfig& cfg) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        PE_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    window_ = SDL_CreateWindow(
        cfg.title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        cfg.width, cfg.height,
        flags
    );
    if (!window_) {
        PE_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_) {
        PE_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }

    if (SDL_GL_MakeCurrent(window_, gl_context_) != 0) {
        PE_ERROR("SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        return false;
    }

    int gl_version = gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress));
    if (gl_version == 0) {
        PE_ERROR("gladLoadGL failed");
        return false;
    }

    if (SDL_GL_SetSwapInterval(cfg.vsync ? 1 : 0) != 0) {
        PE_WARN("SDL_GL_SetSwapInterval failed: %s", SDL_GetError());
    }

    SDL_GL_GetDrawableSize(window_, &width_, &height_);

    PE_INFO("GL %d.%d  (%s)",
            GLAD_VERSION_MAJOR(gl_version),
            GLAD_VERSION_MINOR(gl_version),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    PE_INFO("Window %dx%d  vsync=%d", width_, height_, cfg.vsync ? 1 : 0);
    return true;
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
    SDL_GL_SwapWindow(window_);
}

} // namespace pengine
