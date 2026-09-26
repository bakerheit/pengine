#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "city/construction_site.h"

namespace apricot::city {

struct ConstructionExpansionSite {
    StartSite site;
    const char* kind;
};

// These four blocks deliberately use different construction stories: a
// steel frame, a concrete parking deck, a facade retrofit, and a demolition
// parcel. They continue the open Pinatty Row perimeter without reusing one
// giant prefab. The steel-frame and demolition sites use the outer Briar
// column: their former (138,-31) and (138,155) cells were already occupied by
// the police station and Tacomaco. The tempting outer Cinder column is inside
// Nickel Heights' terrain feather and does not sit at the 12 m Pinatty datum.
inline constexpr std::array<ConstructionExpansionSite, 4>
    kAdditionalConstructionSites{{
        {{"Pinatty Steel Frame Annex", construction_grid_point(-322, -31),
          kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 30.0f, 12.0f, 1700.0f},
         "steel-frame"},
        {{"Pinatty Concrete Deck Works", construction_grid_point(138, 31),
          kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 30.0f, 12.0f, 1700.0f},
         "concrete-deck"},
        {{"Pinatty Facade Retrofit", construction_grid_point(138, 93),
          kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 30.0f, 12.0f, 1700.0f},
         "facade-retrofit"},
        {{"Pinatty Demolition Parcel", construction_grid_point(-322, 155),
          kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 30.0f, 12.0f, 1700.0f},
         "demolition"},
    }};

// Two more blocks each carry a pair of independent towers. The shared
// podiums make the block read as one development, while the separated cores
// and different heights keep each pair legible from the street. These use two
// northern Mercer-column cells on the exact 12 m plate. Their old southern
// cells were being lifted by Halloway Square's terrain feather.
inline constexpr std::array<StartSite, 2> kTwinSkyscraperBlockSites{{
    {"Pinatty Twin Towers Block A", construction_grid_point(-230, -279),
     kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 30.0f, 12.0f, 2100.0f},
    {"Pinatty Twin Towers Block B", construction_grid_point(-230, -217),
     kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 30.0f, 12.0f, 2100.0f},
}};

inline void add_expansion_part(std::vector<StartPart>& out, const char* name,
                               float x, float z, float bottom, float width,
                               float height, float depth, StartFinish finish,
                               bool solid = false) {
    out.push_back({name, {x, z}, bottom, width, height, depth, finish, solid});
}

inline std::vector<StartPart> bake_steel_frame_annex() {
    std::vector<StartPart> out;
    out.reserve(150);
    const auto add = [&](const char* n, float x, float z, float b, float w,
                         float h, float d, StartFinish f, bool s = false) {
        add_expansion_part(out, n, x, z, b, w, h, d, f, s);
    };
    add("construction expansion site lot", 0, 0, 0, 54, .1f, 30,
        StartFinish::Asphalt);
    add("construction expansion haul pad", -16, 4, .1f, 16, .1f, 10,
        StartFinish::Concrete);
    for (float x : {-26.5f, 26.5f})
        add("construction expansion safety fence", x, 0, .1f, .12f, 2.4f,
            29, StartFinish::RedTrim);
    add("construction expansion safety fence front", -20, -14.4f, .1f, 13,
        2.4f, .12f, StartFinish::RedTrim);
    add("construction expansion safety fence front", 20, -14.4f, .1f, 13,
        2.4f, .12f, StartFinish::RedTrim);
    add("construction expansion safety fence rear", 0, 14.4f, .1f, 54,
        2.4f, .12f, StartFinish::RedTrim);
    add("construction expansion concrete core", 7, 1, .2f, 5, 42, 5,
        StartFinish::Concrete, true);
    for (int floor = 0; floor < 12; ++floor) {
        const float y = 3.7f + static_cast<float>(floor) * kConstructionFloorHeightM;
        add("construction expansion steel floor band", 5, 1, y, 31, .22f,
            21, StartFinish::Steel, true);
        for (float x : {-14.5f, 14.5f})
            for (float z : {-9.5f, 11.5f})
                add("construction expansion steel frame column", 5 + x, z,
                    floor == 0 ? .35f : y - 3.1f, .42f, 3.0f, .42f,
                    StartFinish::Steel, true);
        add("construction expansion steel beam front", 5, -9.5f, y - .4f,
            31, .35f, .35f, StartFinish::Steel, true);
        add("construction expansion steel beam rear", 5, 11.5f, y - .4f,
            31, .35f, .35f, StartFinish::Steel, true);
    }
    add("construction crane base", -18, 5, .2f, 4, .45f, 4,
        StartFinish::Concrete, true);
    add("construction crane mast", -18, 5, .65f, 1.1f, 38, 1.1f,
        StartFinish::Steel, true);
    add("construction crane jib", -3, 5, 38.4f, 29, .55f, .65f,
        StartFinish::Yellow);
    add("construction crane trolley", 8, 5, 38.2f, 1, .7f, 1,
        StartFinish::Steel);
    add("construction crane hoist cable", 8, 5, 27, .08f, 11, .08f,
        StartFinish::Steel);
    add("construction rebar bundle", 18, -7, .2f, 5.5f, .6f, 1,
        StartFinish::RedTrim, true);
    add("construction plywood formwork stack", -21, 8, .2f, 4, 1.2f, 2,
        StartFinish::WarmWall, true);
    add("construction expansion site office", -17, -7, .2f, 8, 2.8f, 5,
        StartFinish::WarmWall, true);
    add("construction expansion warning board", -8, -14.2f, .2f, 5, 2.5f,
        .14f, StartFinish::WarmWall);
    return out;
}

inline std::vector<StartPart> bake_concrete_deck_works() {
    std::vector<StartPart> out;
    out.reserve(110);
    const auto add = [&](const char* n, float x, float z, float b, float w,
                         float h, float d, StartFinish f, bool s = false) {
        add_expansion_part(out, n, x, z, b, w, h, d, f, s);
    };
    add("construction expansion site lot", 0, 0, 0, 54, .1f, 30,
        StartFinish::Asphalt);
    add("construction expansion haul pad", 17, 7, .1f, 14, .1f, 12,
        StartFinish::Concrete);
    add("construction expansion concrete deck slab", 2, 1, .2f, 42, .5f, 25,
        StartFinish::Concrete, true);
    for (int level = 0; level < 4; ++level) {
        const float y = 1.0f + static_cast<float>(level) * 3.5f;
        add("construction deck floor slab", 2, 1, y, 42, .38f, 25,
            StartFinish::Concrete, true);
        for (float x : {-17.f, -5.f, 7.f, 19.f})
            for (float z : {-10.f, 12.f})
                add("construction deck concrete column", x, z, y + .38f,
                    .7f, 3.1f, .7f, StartFinish::Concrete, true);
        add("construction deck edge beam front", 2, -11.5f, y + 3.05f, 42,
            .6f, .6f, StartFinish::Concrete, true);
        add("construction deck edge beam rear", 2, 13.5f, y + 3.05f, 42,
            .6f, .6f, StartFinish::Concrete, true);
    }
    add("construction deck concrete ramp", -17, -2, .2f, 8, 2.8f, 19,
        StartFinish::Concrete, true);
    add("construction deck formwork wall", 21, 2, .2f, .5f, 11, 24,
        StartFinish::WarmWall, true);
    add("construction deck rebar cage", -21, 8, .2f, 3, 2.4f, 3,
        StartFinish::RedTrim, true);
    add("construction deck cement mixer", 16, -11, .2f, 2.5f, 1.4f, 2,
        StartFinish::Yellow, true);
    add("construction deck material pallet", 12, 13, .2f, 4, .8f, 2,
        StartFinish::WarmWall, true);
    for (float x : {-23.f, -19.f, 23.f})
        add("construction street barrier", x, -13.5f, .2f, 2.8f, .9f, .45f,
            StartFinish::RedTrim, true);
    add("construction crane base", -22, 10, .2f, 3.5f, .45f, 3.5f,
        StartFinish::Concrete, true);
    add("construction crane mast", -22, 10, .65f, .9f, 14, .9f,
        StartFinish::Steel, true);
    add("construction crane jib", -14, 10, 14.4f, 17, .45f, .55f,
        StartFinish::Yellow);
    return out;
}

inline std::vector<StartPart> bake_facade_retrofit() {
    std::vector<StartPart> out;
    out.reserve(100);
    const auto add = [&](const char* n, float x, float z, float b, float w,
                         float h, float d, StartFinish f, bool s = false) {
        add_expansion_part(out, n, x, z, b, w, h, d, f, s);
    };
    add("construction expansion site lot", 0, 0, 0, 54, .1f, 30,
        StartFinish::Asphalt);
    add("construction expansion haul pad", -15, 8, .1f, 14, .1f, 10,
        StartFinish::Concrete);
    add("construction retrofit shell front", 3, 2, .2f, 31, 15, .45f,
        StartFinish::WarmWall, true);
    add("construction retrofit shell rear", 3, 13, .2f, 31, 15, .45f,
        StartFinish::Brick, true);
    add("construction retrofit shell west", -12.5f, 7.5f, .2f, .45f, 15, 11,
        StartFinish::WarmWall, true);
    add("construction retrofit shell east", 18.5f, 7.5f, .2f, .45f, 15, 11,
        StartFinish::WarmWall, true);
    add("construction retrofit roof", 3, 7.5f, 15.2f, 31.5f, .4f, 11.5f,
        StartFinish::DarkRoof, true);
    // Street-facing scaffold: distinct uprights, decks, cross rails and a
    // suspended worker platform, rather than a single textured wall.
    for (float x : {-13.f, -5.f, 3.f, 11.f, 19.f}) {
        add("construction retrofit scaffold upright", x, -1.0f, .2f, .22f,
            18, .22f, StartFinish::Steel, true);
        for (int level = 0; level < 4; ++level)
            add("construction retrofit scaffold deck", x, -1.0f,
                3.1f + static_cast<float>(level) * 3.8f, 7.7f, .18f, 1.1f,
                StartFinish::Steel);
    }
    for (int level = 0; level < 4; ++level) {
        const float y = 3.1f + static_cast<float>(level) * 3.8f;
        add("construction retrofit scaffold rail", 3, -1.6f, y + .45f, 32,
            .12f, .12f, StartFinish::RedTrim);
    }
    add("construction retrofit hoist", 17, -2.0f, .2f, 2.2f, 1.4f, 1.8f,
        StartFinish::Yellow, true);
    add("construction retrofit facade panel stack", -18, 10, .2f, 4.5f,
        1.5f, 2.2f, StartFinish::Glass, true);
    add("construction plywood office", -19, -8, .2f, 7, 2.8f, 5,
        StartFinish::WarmWall, true);
    add("construction dumpster", 19, 11, .2f, 4.5f, 1.5f, 2.2f,
        StartFinish::Steel, true);
    for (float x : {-23.f, -19.f, 23.f})
        add("construction traffic barrel", x, -14.6f, .2f, .7f, 1, .7f,
            StartFinish::RedTrim, true);
    return out;
}

inline std::vector<StartPart> bake_demolition_parcel() {
    std::vector<StartPart> out;
    out.reserve(90);
    const auto add = [&](const char* n, float x, float z, float b, float w,
                         float h, float d, StartFinish f, bool s = false) {
        add_expansion_part(out, n, x, z, b, w, h, d, f, s);
    };
    add("construction expansion site lot", 0, 0, 0, 54, .1f, 30,
        StartFinish::Asphalt);
    add("construction expansion haul pad", 13, 7, .1f, 18, .1f, 12,
        StartFinish::Concrete);
    add("construction demolition foundation slab", 1, 2, .1f, 30, .35f, 20,
        StartFinish::Concrete, true);
    add("construction demolition partial wall", -10, 3, .2f, .6f, 8, 18,
        StartFinish::Brick, true);
    add("construction demolition broken slab west", -1, 7, 8.2f, 16, .5f, 8,
        StartFinish::Concrete, true);
    add("construction demolition broken slab east", 13, -1, 4.8f, 12, .45f, 7,
        StartFinish::Concrete, true);
    for (int i = 0; i < 6; ++i) {
        const float x = -18.f + static_cast<float>(i % 3) * 4.5f;
        const float z = 9.f + static_cast<float>(i / 3) * 3.0f;
        add("construction demolition rubble pile", x, z, .2f,
            3.4f - static_cast<float>(i % 2) * .5f, 1.0f + static_cast<float>(i % 3) * .35f,
            2.2f + static_cast<float>(i % 2) * .5f, StartFinish::Concrete, true);
    }
    add("construction demolition excavator body", 16, 8, .2f, 4.5f, 1.8f,
        2.8f, StartFinish::Yellow, true);
    add("construction demolition excavator boom", 13, 5, 1.9f, .8f, 4.5f,
        .8f, StartFinish::Yellow, true);
    add("construction demolition excavator bucket", 10, 2, .2f, 2.5f, 1.2f,
        2.0f, StartFinish::Steel, true);
    add("construction demolition dumpster", 20, -8, .2f, 5, 1.6f, 2.5f,
        StartFinish::Steel, true);
    add("construction demolition dust screen", -1, -14.2f, .2f, 35, 3.2f,
        .12f, StartFinish::RedTrim);
    for (float x : {-23.f, -18.f, 18.f, 23.f})
        add("construction street barrier", x, -14.5f, .2f, 3, .9f, .45f,
            StartFinish::RedTrim, true);
    add("construction demolition warning board", 8, -14.2f, .2f, 6, 2.5f,
        .14f, StartFinish::WarmWall);
    return out;
}

inline std::vector<StartPart> bake_additional_construction_site(
    std::size_t index) {
    switch (index) {
        case 0: return bake_steel_frame_annex();
        case 1: return bake_concrete_deck_works();
        case 2: return bake_facade_retrofit();
        case 3: return bake_demolition_parcel();
        default: return {};
    }
}

inline std::vector<StartPart> bake_twin_skyscraper_block(std::size_t index) {
    if (index >= kTwinSkyscraperBlockSites.size()) return {};
    const int floors_a = index == 0 ? 24 : 28;
    const int floors_b = index == 0 ? 21 : 25;
    std::vector<StartPart> out;
    out.reserve(2300);
    const auto add = [&](const char* n, float x, float z, float b, float w,
                         float h, float d, StartFinish f, bool s = false) {
        add_expansion_part(out, n, x, z, b, w, h, d, f, s);
    };
    add("construction twin block lot", 0, 0, 0, 54, .1f, 30,
        StartFinish::Asphalt);
    add("construction twin block plaza", 0, -11.5f, .1f, 46, .1f, 7,
        StartFinish::Concrete);
    add("construction twin block podium floor", 0, 1, .2f, 46, 7.2f, 28,
        StartFinish::Concrete);
    add("construction twin block podium roof", 0, 1, 7.4f, 46.5f, .35f, 28.5f,
        StartFinish::Steel);
    for (float x : {-22.f, 22.f})
        add("construction twin block podium wall", x, 1, .2f, .4f, 7.2f, 28,
            StartFinish::Steel, true);
    const auto tower = [&](float x, int floors, StartFinish trim) {
        const float base = 7.75f;
        const float height = static_cast<float>(floors) * kConstructionFloorHeightM;
        constexpr int window_bays=5;
        constexpr float front_rear_span=15.3f;
        constexpr float side_span=15.3f;
        constexpr float core_half_width=8.5f;
        constexpr float core_half_depth=8.0f;
        // Offsets are measured from the core face to each layer's centre.
        // With 6 cm glass and 2.5 cm panes, this leaves 25 cm from core to
        // glass and 20.75 cm from glass to pane. Trim projects 12.75 cm
        // beyond the panes, so the lit rectangles stay inside their frame.
        constexpr float glass_centre_from_core=.28f;
        constexpr float pane_centre_from_core=.53f;
        constexpr float mullion_centre_from_core=.58f;
        add("twin tower structural core", x, 1, base,
            2*core_half_width, height, 2*core_half_depth,
            StartFinish::Steel, true);
        for (int floor = 0; floor <= floors; ++floor) {
            const float y = base + static_cast<float>(floor) * kConstructionFloorHeightM;
            add("twin tower floor spandrel", x, 1, y,
                2*(core_half_width+.75f), .30f,
                2*(core_half_depth+.75f), trim);
        }
        add("twin tower front glazing",
            x, 1-core_half_depth-glass_centre_from_core,
            base + .4f, 15.3f,
            height - .7f, .06f, StartFinish::Glass);
        add("twin tower rear glazing",
            x, 1+core_half_depth+glass_centre_from_core,
            base + .4f, 15.3f,
            height - .7f, .06f, StartFinish::Glass);
        add("twin tower west glazing",
            x-core_half_width-glass_centre_from_core,
            1, base + .4f, .06f,
            height - .7f, 15.3f, StartFinish::Glass);
        add("twin tower east glazing",
            x+core_half_width+glass_centre_from_core,
            1, base + .4f, .06f,
            height - .7f, 15.3f, StartFinish::Glass);
        // Give the formerly blank curtain walls a physical five-bay rhythm.
        // The mullions sit in front of the continuous dark glass substrate;
        // the shallow suite panes below are the only pieces that emit.
        for(int division=1;division<window_bays;++division) {
            const float front_x=x-front_rear_span*.5f+
                float(division)*front_rear_span/float(window_bays);
            add("twin tower facade mullion",front_x,
                1-core_half_depth-mullion_centre_from_core,base,.16f,
                height,.18f,trim);
            add("twin tower facade mullion",front_x,
                1+core_half_depth+mullion_centre_from_core,base,.16f,
                height,.18f,trim);
            const float side_z=1-side_span*.5f+
                float(division)*side_span/float(window_bays);
            add("twin tower side mullion",
                x-core_half_width-mullion_centre_from_core,side_z,base,.18f,
                height,.16f,trim);
            add("twin tower side mullion",
                x+core_half_width+mullion_centre_from_core,side_z,base,.18f,
                height,.16f,trim);
        }
        const float front_bay=front_rear_span/float(window_bays);
        const float side_bay=side_span/float(window_bays);
        for(int floor=0;floor<floors;++floor) {
            const float window_bottom=base+float(floor)*kConstructionFloorHeightM+.48f;
            constexpr float window_height=2.48f;
            for(float side:{-1.f,1.f}) {
                const float face_z=1+side*(core_half_depth+pane_centre_from_core);
                for(int bay=0;bay<window_bays;++bay) {
                    const float window_x=x-front_rear_span*.5f+
                        (float(bay)+.5f)*front_bay;
                    add("twin tower office window light",window_x,face_z,
                        window_bottom,front_bay-.40f,window_height,.025f,
                        StartFinish::Glass);
                }
                const float face_x=x+side*(core_half_width+pane_centre_from_core);
                for(int bay=0;bay<window_bays;++bay) {
                    const float window_z=1-side_span*.5f+
                        (float(bay)+.5f)*side_bay;
                    add("twin tower office window light",face_x,window_z,
                        window_bottom,.025f,window_height,side_bay-.40f,
                        StartFinish::Glass);
                }
            }
        }
        for (float side : {-1.f, 1.f}) {
            add("twin tower facade pier",
                x + side * (core_half_width+.55f), 1, base, .42f,
                height, .42f, trim);
            add("twin tower facade pier",
                x, 1 + side * (core_half_depth+.7f), base, .42f,
                height, .42f, trim);
        }
        add("twin tower crown cap", x, 1, base + height, 18.8f, .7f, 17.8f,
            trim);
        add("twin tower rooftop plant room", x, 2, base + height + .7f, 8,
            3.0f, 6, StartFinish::Steel, true);
        add("twin tower rooftop vent", x - 5.2f, 2, base + height + .7f,
            1.7f, 1.1f, 2.2f, StartFinish::Concrete, true);
    };
    tower(-12, floors_a, StartFinish::TealDoor);
    tower(12, floors_b, StartFinish::RedTrim);
    add("construction twin block entrance canopy", 0, -12.8f, 4.2f, 9, .3f,
        3.2f, StartFinish::Steel);
    add("construction twin block plaza planter west", -19, -13.8f, .15f, 5,
        .6f, 1.2f, StartFinish::Concrete, true);
    add("construction twin block plaza planter east", 19, -13.8f, .15f, 5,
        .6f, 1.2f, StartFinish::Concrete, true);
    return out;
}

}  // namespace apricot::city
