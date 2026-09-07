#pragma once

#include <algorithm>
#include <glm/glm.hpp>

namespace apricot {

inline constexpr double kMinimumIntroSeconds = 7.0;

// Shared by the layered title renderer, menu drawing, pointer hit-testing and
// headless tests. Coordinates use UiCanvas, never framebuffer pixels.
struct IntroLayout {
    glm::vec2 logo_position;
    glm::vec2 logo_size;
    glm::vec2 menu_position;
    float menu_width;
    float row_height;

    static IntroLayout from_canvas(glm::vec2 vp) {
        const float width = std::min(vp.x * 0.40f, 960.0f);
        return {{vp.x * 0.06f, vp.y * 0.16f}, {width, width * 740.0f / 1600.0f},
                {vp.x * 0.06f, vp.y * 0.57f},
                std::min(520.0f, vp.x * 0.44f), 86.0f};
    }
};

inline glm::vec2 intro_cover_scale(glm::vec2 viewport, glm::vec2 image) {
    if (viewport.x <= 0 || viewport.y <= 0 || image.x <= 0 || image.y <= 0)
        return {1.0f, 1.0f};
    const float ratio = (viewport.x / viewport.y) / (image.x / image.y);
    return ratio > 1.0f ? glm::vec2{1.0f, 1.0f / ratio}
                        : glm::vec2{ratio, 1.0f};
}

inline bool intro_ready(double elapsed, bool assets_ready) {
    return assets_ready && elapsed >= kMinimumIntroSeconds;
}

}  // namespace apricot
