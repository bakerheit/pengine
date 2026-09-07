#pragma once

#include <string>
#include <chrono>

#include <glad/gl.h>

#include "gfx/shader.h"
#include "gfx/texture.h"

namespace apricot {

// A layered startup/title pass. It deliberately lives beside the other GL
// passes instead of teaching Hud about arbitrary textures: Hud's atlas is a
// single-channel glyph resource and its one-draw contract should stay intact.
class LoadingScreen {
public:
    LoadingScreen() = default;
    ~LoadingScreen();

    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;

    // Builds the screen shader and loads a texture relative to the asset root.
    bool init(const std::string& texture_rel);
    void destroy();

    bool valid() const {
        return shader_.valid() && texture_.valid() && logo_.valid() && vao_ != 0;
    }

    // Draws aspect-cropped artwork, atmosphere and the separately scaled logo
    // in drawable pixels. Restores the state expected by the world pass.
    void render(int width, int height);
    float elapsed() const;
    void mark_ready();
    float menu_opacity() const;

private:
    Shader shader_;
    Texture texture_;
    Texture logo_;
    std::chrono::steady_clock::time_point started_{};
    float ready_at_ = -1.0f;
    GLuint vao_ = 0;
};

}  // namespace apricot
