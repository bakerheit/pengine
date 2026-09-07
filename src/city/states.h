#pragma once

#include <cstdint>

namespace apricot {
namespace city {

// A point in the XZ plane, in world metres. +X is east, +Z is SOUTH, +Y is up.
//
// Deliberately not glm::vec2. This is authored data: it is designated-
// initialised in tables, it is compared at compile time, and it wants to be a
// literal aggregate with named members that read as coordinates. `{.x, .z}`
// also stops the reader wondering whether `.y` meant height or northing.
struct Vec2 {
    float x = 0.0f;
    float z = 0.0f;
};

// State identity is separate from districts. A state owns a landmass and a map
// label; districts remain the smaller gameplay jurisdictions inside one state.
enum class StateId : uint8_t {
    OHaven = 0,
    Florangia,
    Count
};

inline constexpr int kStateCount = static_cast<int>(StateId::Count);

struct State {
    StateId id = StateId::Count;
    const char* name = nullptr;
    Vec2 map_label_anchor{};
};

inline constexpr State kStates[] = {
    {.id = StateId::OHaven,
     .name = "O'Haven",
     .map_label_anchor = {0.0f, -120.0f}},
    {.id = StateId::Florangia,
     .name = "Florangia",
     .map_label_anchor = {4850.0f, 5200.0f}},
};

inline constexpr const State& state(StateId id) {
    const int i = static_cast<int>(id);
    return kStates[i >= 0 && i < kStateCount ? i : 0];
}

inline constexpr const char* state_name(StateId id) {
    return id == StateId::Count ? "open water" : state(id).name;
}

constexpr bool states_are_dense_and_named() {
    if (static_cast<int>(sizeof(kStates) / sizeof(kStates[0])) != kStateCount)
        return false;
    for (int i = 0; i < kStateCount; ++i) {
        if (static_cast<int>(kStates[i].id) != i || kStates[i].name == nullptr)
            return false;
    }
    return true;
}
static_assert(states_are_dense_and_named(),
              "state ids must densely index named state metadata");

}  // namespace city
}  // namespace apricot
