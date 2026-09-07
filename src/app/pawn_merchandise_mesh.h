#pragma once

#include <array>
#include "gfx/primitives.h"

namespace apricot {

// Unit-sized acoustic body: scale with BuildingPiece width/height/depth. The
// soundboard has one continuous front UV map; the silhouette is geometry,
// with a narrow waist, separate upper/lower bouts and real side thickness.
inline MeshData make_pawn_guitar_body() {
    constexpr std::array<glm::vec2, 20> outline{{
        {.10f,.50f},{-.10f,.50f},{-.30f,.44f},{-.37f,.30f},
        {-.35f,.16f},{-.24f,.06f},{-.30f,-.03f},{-.44f,-.14f},
        {-.50f,-.30f},{-.46f,-.43f},{-.28f,-.50f},{.28f,-.50f},
        {.46f,-.43f},{.50f,-.30f},{.44f,-.14f},{.30f,-.03f},
        {.24f,.06f},{.35f,.16f},{.37f,.30f},{.30f,.44f},
    }};
    MeshData mesh;
    constexpr glm::vec4 weight{1,0,0,0};
    const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c,
                              glm::vec3 normal, bool soundboard) {
        const auto base = static_cast<uint32_t>(mesh.vertices.size());
        for (const auto& p : {a,b,c}) {
            const glm::vec2 uv = soundboard ? glm::vec2{p.x+.5f,p.y+.5f} :
                glm::vec2{.025f,p.y+.5f};
            mesh.vertices.push_back({p,normal,uv,weight});
            mesh.bounds.expand(p);
        }
        mesh.indices.insert(mesh.indices.end(), {base,base+1u,base+2u});
    };
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const auto a = outline[i];
        const auto b = outline[(i+1u)%outline.size()];
        const glm::vec3 af{a,.5f},bf{b,.5f},ab{a,-.5f},bb{b,-.5f};
        triangle({0,0,.5f},af,bf,{0,0,1},true);
        triangle({0,0,-.5f},bb,ab,{0,0,-1},false);
        const auto normal = glm::normalize(glm::vec3{b.y-a.y,a.x-b.x,0});
        triangle(af,ab,bb,normal,false);
        triangle(af,bb,bf,normal,false);
    }
    return mesh;
}
}  // namespace apricot
