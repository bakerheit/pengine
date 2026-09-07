#pragma once

#include <glm/glm.hpp>

namespace apricot {

// Preserve the proportions authored for the original 2560x1440 drawable.
// Height drives scale, including when only the window's height changes; width
// follows aspect ratio so edge-anchored panels remain on screen. This also
// removes backing-DPI differences between Retina and ordinary displays.
struct UiCanvas {
    static constexpr float kReferenceHeight = 1440.0f;
    glm::vec2 size{0.0f};

    static UiCanvas from_drawable(glm::vec2 drawable) {
        if (!(drawable.x > 0.0f) || !(drawable.y > 0.0f)) return {};
        return {{drawable.x / drawable.y * kReferenceHeight,
                 kReferenceHeight}};
    }

    // Applies equally to absolute pointer positions and relative map drags.
    glm::vec2 from_window(glm::vec2 point, glm::vec2 window_size) const {
        if (!(window_size.x > 0.0f) || !(window_size.y > 0.0f)) return {};
        return point * size / window_size;
    }
};

}  // namespace apricot
