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

// State identity is separate from cities and districts. A state owns a landmass
// and a map label; a city is the urban place on it; districts remain the
// smaller gameplay jurisdictions inside one city.
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

// CITIES. The level the player actually navigates by, and the one the map was
// missing: a state and its city used to share the single name "O'Haven", so the
// overview label doubled as the city label and the city had no identity of its
// own. They are separate now — O'Haven is the landmass, Pinatty is the place on
// it — and the map draws them in different zoom bands, because their centres
// are the same point and only one label can have it.
enum class CityId : uint8_t {
    Pinatty = 0,
    Miandi,
    Count
};

inline constexpr int kCityCount = static_cast<int>(CityId::Count);

struct City {
    CityId id = CityId::Count;
    StateId state = StateId::Count;
    const char* name = nullptr;
    Vec2 map_label_anchor{};
};

// Pinatty's anchor is the centre of the ten authored district polygons — the
// city's real extent — and not the world origin, which is downtown's.
//
// It does sit over Pinatty Row, and there is nowhere else for it to go: the ten
// districts tile the island, so no point reads as "the whole city" without
// covering some district. That is paid for with the zoom band instead. The map
// draws city names only between the state overview and the street labels, so at
// the one zoom where the name is over downtown, downtown has no detail drawn
// yet. See the label passes in game_ui.cpp.
//
// Miandi's anchor is `kMiandiWorldOrigin`; `miandi_layout.h` static_asserts the
// two agree, so the duplicate cannot drift.
inline constexpr City kCities[] = {
    {.id = CityId::Pinatty,
     .state = StateId::OHaven,
     .name = "Pinatty",
     .map_label_anchor = {75.0f, 175.0f}},
    {.id = CityId::Miandi,
     .state = StateId::Florangia,
     .name = "Miandi",
     .map_label_anchor = {7500.0f, 8400.0f}},
};

inline constexpr const City& city_at(CityId id) {
    const int i = static_cast<int>(id);
    return kCities[i >= 0 && i < kCityCount ? i : 0];
}

inline constexpr const char* city_name(CityId id) {
    return id == CityId::Count ? "open country" : city_at(id).name;
}

constexpr bool cities_are_dense_and_named() {
    if (static_cast<int>(sizeof(kCities) / sizeof(kCities[0])) != kCityCount)
        return false;
    for (int i = 0; i < kCityCount; ++i) {
        if (static_cast<int>(kCities[i].id) != i ||
            kCities[i].name == nullptr ||
            kCities[i].state == StateId::Count)
            return false;
    }
    return true;
}
static_assert(cities_are_dense_and_named(),
              "city ids must densely index named cities inside a real state");

}  // namespace city
}  // namespace apricot
