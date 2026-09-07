#pragma once

#include <algorithm>
#include <array>
#include <cstring>

#include "city/building_creator.h"

namespace apricot::city {

// Brick arrived with Club Mirage's detail pass. Before it, every masonry
// finish resolved to stucco, so a reused garment warehouse and a Deco hotel
// were painted from the same tin and the reuse story had nowhere to land.
enum class MiandiSurface { Plain, Stucco, Concrete, Metal, Asphalt, Brick };

struct MiandiPresentation {
    MiandiSurface surface = MiandiSurface::Plain;
    std::array<float, 4> tint{1, 1, 1, 1};
    float u_tiles = 1.0f;
    float v_tiles = 1.0f;
};

// A small local paint kit: bodies are sun-faded and trim borrows the adjacent
// building's color. Keep neon separate and keep ordinary surfaces non-emissive.
inline MiandiPresentation miandi_presentation(const BuildingPiece& p,
                                               bool ocean_drive) {
    MiandiPresentation r;
    switch (p.finish) {
        case BuildingFinish::WarmWall:
            r.surface = MiandiSurface::Stucco;
            r.tint = ocean_drive ? std::array<float, 4>{1.f, .73f, .79f, 1.f}
                                 : std::array<float, 4>{1.f, .93f, .78f, 1.f};
            break;
        case BuildingFinish::Brick:
            r.surface = MiandiSurface::Brick;
            r.tint = {.97f, .72f, .60f, 1.f};
            break;
        case BuildingFinish::White:
            r.surface = MiandiSurface::Stucco;
            r.tint = {.95f, 1.f, .97f, 1.f};
            break;
        case BuildingFinish::Concrete:
            r.surface = MiandiSurface::Concrete;
            r.tint = {1.f, .97f, .89f, 1.f};
            break;
        case BuildingFinish::TealDoor:
            r.surface = MiandiSurface::Metal;
            r.tint = {.36f, .79f, .75f, 1.f};
            break;
        case BuildingFinish::RedTrim:
            r.surface = MiandiSurface::Metal;
            r.tint = {.91f, .45f, .53f, 1.f};
            break;
        case BuildingFinish::Yellow:
            r.surface = MiandiSurface::Metal;
            r.tint = {1.f, .85f, .48f, 1.f};
            break;
        case BuildingFinish::Steel:
            r.surface = MiandiSurface::Metal;
            r.tint = {.66f, .73f, .72f, 1.f};
            break;
        case BuildingFinish::DarkRoof:
            r.surface = MiandiSurface::Metal;
            r.tint = {.19f, .24f, .26f, 1.f};
            break;
        case BuildingFinish::Glass:
            r.tint = {.15f, .34f, .41f, 1.f};
            break;
        case BuildingFinish::Asphalt:
            r.surface = MiandiSurface::Asphalt;
            r.tint = {1.f, 1.f, 1.f, 1.f};
            break;
        case BuildingFinish::PoolWater:
            r.tint = {.08f, .56f, .64f, 1.f};
            break;
    }
    const bool flat = p.height_m < .35f;
    const float tile_m = r.surface == MiandiSurface::Asphalt ? 6.f
                       : r.surface == MiandiSurface::Brick   ? 1.8f
                                                             : 2.4f;
    r.u_tiles = (flat ? p.width_m : std::max(p.width_m, p.depth_m)) / tile_m;
    r.v_tiles = (flat ? p.depth_m : p.height_m) / tile_m;
    return r;
}

inline bool miandi_ground_piece(const BuildingPiece& p) {
    if (p.solid || !p.name || p.bottom_m < 0.f || p.bottom_m > .25f ||
        p.height_m <= 0.f || p.height_m > .35f ||
        p.pitch_deg != 0.f || p.roll_deg != 0.f) return false;
    if (p.finish == BuildingFinish::Glass ||
        p.finish == BuildingFinish::PoolWater) return false;
    for (const char* token : {"walk", "floor", "threshold", "terrace",
                              "court", "paving", "tile", "lot", "lane"})
        if (std::strstr(p.name, token)) return true;
    return false;
}

// Small venue-specific paint choices. No global exposure/material changes,
// random weathering or emissive wall paint; neon is handled separately.
inline std::array<float, 4> miandi_venue_tint(
    const BuildingPiece& p, std::array<float, 4> base) {
    if (!p.name || std::strstr(p.name, "miandi neon")) return base;
    const auto has = [&](const char* name) { return std::strstr(p.name, name); };
    if (p.finish == BuildingFinish::WarmWall) {
        if (has("Bellmar")) return {1.f, .73f, .79f, 1.f};
        if (has("Palmera")) return {1.f, .92f, .72f, 1.f};
        if (has("Candela")) return {.96f, .66f, .47f, 1.f};
    }
    if (p.finish == BuildingFinish::White && has("Maravelle"))
        return {.87f, .98f, .95f, 1.f};
    if (p.finish == BuildingFinish::Yellow && has("Tropico"))
        return {1.f, .91f, .72f, 1.f};
    if (p.finish == BuildingFinish::Glass && has("Mirage"))
        return {.065f, .10f, .15f, 1.f};
    return base;
}

}  // namespace apricot::city
