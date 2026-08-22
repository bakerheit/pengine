#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace apricot {

// Gain routing. HEADER-ONLY AND DEVICE-FREE on purpose: every emitter's final
// volume is decided by this arithmetic, and it is exactly the arithmetic that
// goes wrong silently. Keeping it here means a headless test can assert that
// muting music does not duck the engine, with no audio hardware in sight.
//
// The routing is deliberately shallow — one category trim times one master:
//
//     final = emitter_gain * category[c] * master
//
// Deeper graphs (per-emitter buses, sends) are where "why is this one sound
// quiet" becomes unanswerable. Add a category before you add a layer.

enum class Category : int {
    Engine = 0,
    Tyres,
    Impacts,
    Ambience,
    Ui,
    Music,
    kCount,
};

inline constexpr std::size_t kCategoryCount =
    static_cast<std::size_t>(Category::kCount);

inline constexpr const char* category_name(Category c) {
    switch (c) {
        case Category::Engine:   return "engine";
        case Category::Tyres:    return "tyres";
        case Category::Impacts:  return "impacts";
        case Category::Ambience: return "ambience";
        case Category::Ui:       return "ui";
        case Category::Music:    return "music";
        case Category::kCount:   break;
    }
    return "?";
}

struct Mixer {
    float master = 1.0f;
    std::array<float, kCategoryCount> category{};

    // Categories default to unity. Written as a constructor rather than a
    // member initialiser because a value-initialised std::array is all zeroes,
    // and a mixer that defaults to silent is a bug report waiting to happen.
    Mixer() { category.fill(1.0f); }

    // Trims are clamped to [0, 1]: a trim above unity is how you get clipping
    // that only appears when several loud things happen at once.
    void set_master(float g) { master = std::clamp(g, 0.0f, 1.0f); }

    void set_category(Category c, float g) {
        const std::size_t i = static_cast<std::size_t>(c);
        if (i < kCategoryCount) category[i] = std::clamp(g, 0.0f, 1.0f);
    }

    float category_gain(Category c) const {
        const std::size_t i = static_cast<std::size_t>(c);
        return i < kCategoryCount ? category[i] : 0.0f;
    }

    // The one function that decides how loud anything is.
    // `emitter_gain` is NOT clamped to 1: distance attenuation and one-shot
    // emphasis legitimately hand in values above unity. It is clamped at zero,
    // because a negative gain inverts the waveform's phase rather than making
    // it quiet, and that reads as a weirdly hollow mix rather than as an error.
    float gain(Category c, float emitter_gain = 1.0f) const {
        return std::max(emitter_gain, 0.0f) * category_gain(c) * master;
    }
};

}  // namespace apricot
