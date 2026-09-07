#pragma once

#include <cmath>
#include <cstring>
#include <initializer_list>
#include <vector>

#include "city/building_creator.h"

namespace apricot::city {

// Bent neon glass in the east-facing Y/Z plane. u increases to the viewer's
// right (world -Z). A slim pale core sits in front of the colored glass.
inline void miandi_neon_stroke(std::vector<BuildingPiece>& out, const char* name,
                           float x, float z, Vec2 a, Vec2 b,
                           BuildingFinish finish) {
    const float dy = b.z - a.z, dz = a.x - b.x;
    const float length = std::sqrt(dy * dy + dz * dz);
    if (length < .001f) return;
    const float pitch = std::atan2(dz, dy) * 57.2957795f;
    const Vec2 centre{x, z - (a.x + b.x) * .5f};
    const float bottom = (a.z + b.z - length) * .5f;
    out.push_back({name, centre, bottom, .14f, length, .14f,
                   finish, false, pitch});
    out.push_back({finish == BuildingFinish::Yellow
                       ? "miandi neon amber lettering core"
                       : finish == BuildingFinish::White
                       ? "miandi neon warm-white lettering core"
                       : finish == BuildingFinish::RedTrim
                       ? "miandi neon 80s pink core"
                       : "miandi neon 80s cyan core",
                   {x + .08f, centre.z}, bottom, .055f, length, .055f,
                   finish, false, pitch});
}

// Original single-stroke lettering, with bevels and a slight forward slant.
// Kept as real geometry so hotel names stay sharp from the road at any scale.
inline void miandi_neon_glyph(std::vector<BuildingPiece>& out, const char* name,
                          char letter, float x, float z, float u, float y,
                          float size, BuildingFinish finish) {
    const auto path = [&](std::initializer_list<Vec2> points) {
        if (points.size() < 2) return;
        auto p = points.begin();
        Vec2 a = *p++;
        for (; p != points.end(); ++p) {
            const Vec2 b = *p;
            miandi_neon_stroke(out, name, x, z,
                {u + (a.x + a.z * .10f) * size, y + a.z * size},
                {u + (b.x + b.z * .10f) * size, y + b.z * size}, finish);
            a = b;
        }
    };
    switch (letter) {
        case 'A': path({{0,0},{0,.85f},{.12f,1},{.53f,1},{.65f,.85f},{.65f,0}});
                  path({{0,.48f},{.65f,.48f}}); break;
        case 'B': path({{0,0},{0,1},{.5f,1},{.65f,.86f},{.65f,.64f},{.48f,.5f},{0,.5f}});
                  path({{.48f,.5f},{.65f,.36f},{.65f,.14f},{.5f,0},{0,0}}); break;
        case 'C': path({{.65f,.9f},{.52f,1},{.13f,1},{0,.85f},{0,.15f},{.13f,0},{.52f,0},{.65f,.1f}}); break;
        case 'D': path({{0,0},{0,1},{.42f,1},{.65f,.78f},{.65f,.22f},{.42f,0},{0,0}}); break;
        case 'E': path({{.65f,1},{0,1},{0,0},{.65f,0}});
                  path({{0,.5f},{.52f,.5f}}); break;
        case 'H': path({{0,0},{0,1}}); path({{.65f,0},{.65f,1}});
                  path({{0,.5f},{.65f,.5f}}); break;
        case 'G': path({{.65f,.88f},{.52f,1},{.13f,1},{0,.85f},{0,.15f},{.13f,0},{.52f,0},{.65f,.15f},{.65f,.48f},{.35f,.48f}}); break;
        case 'I': path({{0,1},{.65f,1}}); path({{.325f,1},{.325f,0}});
                  path({{0,0},{.65f,0}}); break;
        case 'L': path({{0,1},{0,0},{.65f,0}}); break;
        case 'M': path({{0,0},{0,1},{.325f,.48f},{.65f,1},{.65f,0}}); break;
        case 'N': path({{0,0},{0,1},{.65f,0},{.65f,1}}); break;
        case 'O': path({{.13f,0},{0,.15f},{0,.85f},{.13f,1},{.52f,1},{.65f,.85f},{.65f,.15f},{.52f,0},{.13f,0}}); break;
        case 'P': path({{0,0},{0,1},{.5f,1},{.65f,.85f},{.65f,.65f},{.5f,.5f},{0,.5f}}); break;
        case 'R': path({{0,0},{0,1},{.5f,1},{.65f,.85f},{.65f,.65f},{.5f,.5f},{0,.5f}});
                  path({{.35f,.5f},{.65f,0}}); break;
        case 'T': path({{0,1},{.65f,1}}); path({{.325f,1},{.325f,0}}); break;
        case 'U': path({{0,1},{0,.15f},{.13f,0},{.52f,0},{.65f,.15f},{.65f,1}}); break;
        case 'V': path({{0,1},{.325f,0},{.65f,1}}); break;
        case 'W': path({{0,1},{.12f,0},{.325f,.45f},{.53f,0},{.65f,1}}); break;
        default: break;  // spaces intentionally leave a gap
    }
}

inline void miandi_neon_name(std::vector<BuildingPiece>& out, const char* name,
                         const char* text, float x, float z, float y,
                         float size, BuildingFinish finish) {
    const float advance = .88f * size;
    const float start = -(static_cast<float>(std::strlen(text)) * advance -
                           .13f * size) * .5f;
    for (std::size_t i = 0; text[i]; ++i)
        miandi_neon_glyph(out, name, text[i], x, z,
                      start + static_cast<float>(i) * advance, y, size, finish);
}

// Rotate east-facing lettering into any authored facade, preserving its
// local left-to-right order. yaw=90 faces north (-Z); yaw=0 faces east (+X).
inline void miandi_neon_facade_name(std::vector<BuildingPiece>& out,
                                    const char* name, const char* text,
                                    Vec2 plane, float y, float size,
                                    BuildingFinish finish, float yaw_deg = 0.f) {
    const auto first = out.size();
    miandi_neon_name(out, name, text, 0.f, 0.f, y, size, finish);
    const float yaw = yaw_deg * .01745329252f;
    const float c = std::cos(yaw), s = std::sin(yaw);
    for (std::size_t i = first; i < out.size(); ++i) {
        auto& p = out[i];
        p.centre = {plane.x + c * p.centre.x + s * p.centre.z,
                    plane.z - s * p.centre.x + c * p.centre.z};
        p.yaw_deg = yaw_deg;
    }
}

}  // namespace apricot::city
