#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "gfx/primitives.h"

namespace apricot {

// The molotov's bottle, cooked out of the supplied Quequis House GLB by
// tools/cook_molotov_bottle.py. GENERATED — edit the cooker, not this file.
//
// Source mesh `Soda_01`, one of the eight lathe-turned bottles on the
// Quequis kitchen shelves and the only one with a contour rather than a
// straight cylinder: it bulges at the base, pulls in to a waist and bulges
// again at the shoulder. That profile is what reads as thick glass at throwing
// distance, and it is why this mesh is named rather than picked.
//
// Emitted with the source node's uniform scale applied and NOTHING else. The
// quarter-turn it had on its shelf and its position in the restaurant are
// dropped, the axis is recentred on x = z = 0 and the base sits at y = 0,
// because a thrown prop owns its own origin. Source metre scale is retained:
// 0.328 m tall, 0.102 m across the body.
//
// UNTEXTURED ON PURPOSE. The source material is a printed soda label whose
// image belongs to the private archive under the ignored assets/models/ tree;
// cooking it to a tracked path would push supplied asset content into git. The
// silhouette was the part that was wanted. The caller paints it bottle green.
inline constexpr float kMolotovBottleSourceHeightM = 0.32835f;
inline constexpr float kMolotovBottleSourceRadiusM = 0.05081f;

struct MolotovBottleVertex {
    float px, py, pz;
    float nx, ny, nz;
};

inline constexpr std::array<MolotovBottleVertex, 102> kMolotovBottleVertices{{
    {-0.00000f,0.10494f,-0.04504f, 0.0000f,0.0793f,-0.9969f},
    {-0.00000f,0.19449f,-0.04504f, 0.0000f,-0.0746f,-0.9972f},
    {0.03901f,0.10494f,-0.02252f, 0.8633f,0.0793f,-0.4984f},
    {0.03901f,0.10494f,-0.02252f, 0.8633f,0.0793f,-0.4984f},
    {0.03901f,0.19449f,-0.02252f, 0.8636f,-0.0746f,-0.4986f},
    {0.03901f,0.19449f,-0.02252f, 0.8636f,-0.0746f,-0.4986f},
    {0.03901f,0.10494f,0.02252f, 0.8633f,0.0793f,0.4984f},
    {0.03901f,0.10494f,0.02252f, 0.8633f,0.0793f,0.4984f},
    {0.03901f,0.19449f,0.02252f, 0.8636f,-0.0746f,0.4986f},
    {0.03901f,0.19449f,0.02252f, 0.8636f,-0.0746f,0.4986f},
    {-0.00000f,0.10494f,0.04504f, 0.0000f,0.0793f,0.9969f},
    {-0.00000f,0.19449f,0.04504f, 0.0000f,-0.0746f,0.9972f},
    {-0.03901f,0.10494f,0.02252f, -0.8633f,0.0793f,0.4984f},
    {-0.03901f,0.10494f,0.02252f, -0.8633f,0.0793f,0.4984f},
    {-0.03901f,0.19449f,0.02252f, -0.8636f,-0.0746f,0.4986f},
    {-0.03901f,0.19449f,0.02252f, -0.8636f,-0.0746f,0.4986f},
    {-0.03901f,0.10494f,-0.02252f, -0.8633f,0.0793f,-0.4984f},
    {-0.03901f,0.10494f,-0.02252f, -0.8633f,0.0793f,-0.4984f},
    {-0.03901f,0.19449f,-0.02252f, -0.8636f,-0.0746f,-0.4986f},
    {-0.03901f,0.19449f,-0.02252f, -0.8636f,-0.0746f,-0.4986f},
    {0.04289f,0.22501f,-0.02476f, 0.8660f,0.0113f,-0.5000f},
    {0.04289f,0.22501f,-0.02476f, 0.8660f,0.0113f,-0.5000f},
    {-0.00000f,0.22501f,-0.04952f, 0.0000f,0.0113f,-0.9999f},
    {0.04289f,0.22501f,0.02476f, 0.8660f,0.0113f,0.5000f},
    {0.04289f,0.22501f,0.02476f, 0.8660f,0.0113f,0.5000f},
    {-0.00000f,0.22501f,0.04952f, 0.0000f,0.0113f,0.9999f},
    {-0.04289f,0.22501f,0.02476f, -0.8660f,0.0113f,0.5000f},
    {-0.04289f,0.22501f,0.02476f, -0.8660f,0.0113f,0.5000f},
    {-0.04289f,0.22501f,-0.02476f, -0.8660f,0.0113f,-0.5000f},
    {-0.04289f,0.22501f,-0.02476f, -0.8660f,0.0113f,-0.5000f},
    {0.03614f,0.27052f,-0.02087f, 0.7967f,0.3919f,-0.4600f},
    {0.03614f,0.27052f,-0.02087f, 0.7967f,0.3919f,-0.4600f},
    {-0.00000f,0.27052f,-0.04173f, 0.0000f,0.3919f,-0.9200f},
    {0.03614f,0.27052f,0.02087f, 0.7967f,0.3919f,0.4600f},
    {0.03614f,0.27052f,0.02087f, 0.7967f,0.3919f,0.4600f},
    {-0.00000f,0.27052f,0.04173f, 0.0000f,0.3919f,0.9200f},
    {-0.03614f,0.27052f,0.02087f, -0.7967f,0.3919f,0.4600f},
    {-0.03614f,0.27052f,0.02087f, -0.7967f,0.3919f,0.4600f},
    {-0.03614f,0.27052f,-0.02087f, -0.7967f,0.3919f,-0.4600f},
    {-0.03614f,0.27052f,-0.02087f, -0.7967f,0.3919f,-0.4600f},
    {0.00844f,0.30854f,-0.00487f, 0.7997f,0.3839f,-0.4617f},
    {0.00844f,0.30854f,-0.00487f, 0.7997f,0.3839f,-0.4617f},
    {-0.00000f,0.30854f,-0.00975f, 0.0000f,0.3839f,-0.9234f},
    {0.00844f,0.30854f,0.00487f, 0.7997f,0.3839f,0.4617f},
    {0.00844f,0.30854f,0.00487f, 0.7997f,0.3839f,0.4617f},
    {-0.00000f,0.30854f,0.00975f, 0.0000f,0.3839f,0.9234f},
    {-0.00844f,0.30854f,0.00487f, -0.7997f,0.3839f,0.4617f},
    {-0.00844f,0.30854f,0.00487f, -0.7997f,0.3839f,0.4617f},
    {-0.00844f,0.30854f,-0.00487f, -0.7997f,0.3839f,-0.4617f},
    {-0.00844f,0.30854f,-0.00487f, -0.7997f,0.3839f,-0.4617f},
    {0.00844f,0.32835f,-0.00487f, 0.6862f,0.6100f,-0.3962f},
    {0.00844f,0.32835f,-0.00487f, 0.6862f,0.6100f,-0.3962f},
    {0.00844f,0.32835f,-0.00487f, 0.6862f,0.6100f,-0.3962f},
    {0.00000f,0.32835f,-0.00975f, 0.0000f,0.6100f,-0.7924f},
    {0.00000f,0.32835f,-0.00975f, 0.0000f,0.6100f,-0.7924f},
    {0.00844f,0.32835f,0.00487f, 0.6862f,0.6100f,0.3962f},
    {0.00844f,0.32835f,0.00487f, 0.6862f,0.6100f,0.3962f},
    {0.00844f,0.32835f,0.00487f, 0.6862f,0.6100f,0.3962f},
    {0.00000f,0.32835f,0.00975f, 0.0000f,0.6100f,0.7924f},
    {0.00000f,0.32835f,0.00975f, 0.0000f,0.6100f,0.7924f},
    {-0.00844f,0.32835f,0.00487f, -0.6862f,0.6100f,0.3962f},
    {-0.00844f,0.32835f,0.00487f, -0.6862f,0.6100f,0.3962f},
    {-0.00844f,0.32835f,0.00487f, -0.6862f,0.6100f,0.3962f},
    {-0.00844f,0.32835f,-0.00487f, -0.6862f,0.6100f,-0.3962f},
    {-0.00844f,0.32835f,-0.00487f, -0.6862f,0.6100f,-0.3962f},
    {-0.00844f,0.32835f,-0.00487f, -0.6862f,0.6100f,-0.3962f},
    {-0.00000f,0.06800f,-0.05081f, 0.0000f,0.0260f,-0.9997f},
    {0.04400f,0.06800f,-0.02540f, 0.8657f,0.0260f,-0.4998f},
    {0.04400f,0.06800f,-0.02540f, 0.8657f,0.0260f,-0.4998f},
    {0.04400f,0.06800f,0.02540f, 0.8657f,0.0260f,0.4998f},
    {0.04400f,0.06800f,0.02540f, 0.8657f,0.0260f,0.4998f},
    {-0.00000f,0.06800f,0.05081f, 0.0000f,0.0260f,0.9997f},
    {-0.04400f,0.06800f,0.02540f, -0.8657f,0.0260f,0.4998f},
    {-0.04400f,0.06800f,0.02540f, -0.8657f,0.0260f,0.4998f},
    {-0.04400f,0.06800f,-0.02540f, -0.8657f,0.0260f,-0.4998f},
    {-0.04400f,0.06800f,-0.02540f, -0.8657f,0.0260f,-0.4998f},
    {0.00000f,0.02195f,-0.04616f, 0.0000f,-0.2638f,-0.9646f},
    {0.03997f,0.02195f,-0.02308f, 0.8354f,-0.2638f,-0.4823f},
    {0.03997f,0.02195f,-0.02308f, 0.8354f,-0.2638f,-0.4823f},
    {0.03997f,0.02195f,0.02308f, 0.8354f,-0.2638f,0.4823f},
    {0.03997f,0.02195f,0.02308f, 0.8354f,-0.2638f,0.4823f},
    {0.00000f,0.02195f,0.04616f, 0.0000f,-0.2638f,0.9646f},
    {-0.03997f,0.02195f,0.02308f, -0.8354f,-0.2638f,0.4823f},
    {-0.03997f,0.02195f,0.02308f, -0.8354f,-0.2638f,0.4823f},
    {-0.03997f,0.02195f,-0.02308f, -0.8354f,-0.2638f,-0.4823f},
    {-0.03997f,0.02195f,-0.02308f, -0.8354f,-0.2638f,-0.4823f},
    {-0.00000f,0.00000f,-0.03521f, 0.0000f,-0.7765f,-0.6301f},
    {-0.00000f,0.00000f,-0.03521f, 0.0000f,-0.7765f,-0.6301f},
    {0.03049f,0.00000f,-0.01760f, 0.5457f,-0.7765f,-0.3151f},
    {0.03049f,0.00000f,-0.01760f, 0.5457f,-0.7765f,-0.3151f},
    {0.03049f,0.00000f,-0.01760f, 0.5457f,-0.7765f,-0.3151f},
    {0.03049f,0.00000f,0.01760f, 0.5457f,-0.7765f,0.3151f},
    {0.03049f,0.00000f,0.01760f, 0.5457f,-0.7765f,0.3151f},
    {0.03049f,0.00000f,0.01760f, 0.5457f,-0.7765f,0.3151f},
    {-0.00000f,0.00000f,0.03521f, 0.0000f,-0.7765f,0.6301f},
    {-0.00000f,0.00000f,0.03521f, 0.0000f,-0.7765f,0.6301f},
    {-0.03049f,0.00000f,0.01760f, -0.5457f,-0.7765f,0.3151f},
    {-0.03049f,0.00000f,0.01760f, -0.5457f,-0.7765f,0.3151f},
    {-0.03049f,0.00000f,0.01760f, -0.5457f,-0.7765f,0.3151f},
    {-0.03049f,0.00000f,-0.01760f, -0.5457f,-0.7765f,-0.3151f},
    {-0.03049f,0.00000f,-0.01760f, -0.5457f,-0.7765f,-0.3151f},
    {-0.03049f,0.00000f,-0.01760f, -0.5457f,-0.7765f,-0.3151f},
}};

inline constexpr std::array<uint32_t, 312> kMolotovBottleIndices{{
    0, 1, 4, 0, 4, 2, 3, 5, 9, 3, 9, 7,
    6, 8, 11, 6, 11, 10, 10, 11, 14, 10, 14, 12,
    9, 5, 21, 9, 21, 24, 13, 15, 19, 13, 19, 17,
    16, 18, 1, 16, 1, 0, 10, 12, 72, 10, 72, 71,
    29, 27, 37, 29, 37, 39, 19, 15, 27, 19, 27, 29,
    4, 1, 22, 4, 22, 20, 11, 8, 23, 11, 23, 25,
    1, 18, 28, 1, 28, 22, 14, 11, 25, 14, 25, 26,
    39, 37, 47, 39, 47, 49, 25, 23, 33, 25, 33, 35,
    20, 22, 32, 20, 32, 30, 22, 28, 38, 22, 38, 32,
    26, 25, 35, 26, 35, 36, 24, 21, 31, 24, 31, 34,
    44, 41, 51, 44, 51, 56, 35, 33, 43, 35, 43, 45,
    30, 32, 42, 30, 42, 40, 32, 38, 48, 32, 48, 42,
    36, 35, 45, 36, 45, 46, 34, 31, 41, 34, 41, 44,
    57, 52, 54, 54, 65, 62, 62, 59, 57, 54, 62, 57,
    49, 47, 61, 49, 61, 64, 45, 43, 55, 45, 55, 58,
    40, 42, 53, 40, 53, 50, 42, 48, 63, 42, 63, 53,
    46, 45, 58, 46, 58, 60, 73, 75, 85, 73, 85, 83,
    3, 7, 70, 3, 70, 68, 13, 17, 75, 13, 75, 73,
    0, 2, 67, 0, 67, 66, 6, 10, 71, 6, 71, 69,
    16, 0, 66, 16, 66, 74, 84, 76, 86, 84, 86, 99,
    69, 71, 81, 69, 81, 79, 66, 67, 77, 66, 77, 76,
    74, 66, 76, 74, 76, 84, 71, 72, 82, 71, 82, 81,
    68, 70, 80, 68, 80, 78, 101, 87, 90, 90, 93, 95,
    95, 98, 101, 90, 95, 101, 81, 82, 96, 81, 96, 94,
    78, 80, 92, 78, 92, 89, 83, 85, 100, 83, 100, 97,
    79, 81, 94, 79, 94, 91, 76, 77, 88, 76, 88, 86,
}};

// Upload-ready geometry, base at the origin and the neck up local +Y. Scale it
// by the caller: the cooked bottle keeps the source's metre scale, which is a
// litre bottle, and a molotov is a smaller one. app/molotov_visual.cpp does the
// rescaling and says what it picked and why.
inline MeshData make_molotov_bottle() {
    MeshData mesh;
    mesh.vertices.reserve(kMolotovBottleVertices.size());
    mesh.indices.assign(kMolotovBottleIndices.begin(), kMolotovBottleIndices.end());
    // These primitives are not terrain, and the vertex shader zeroes the splat
    // weights for every non-terrain material anyway; the channel is picked
    // rather than left uninitialised for the same reason make_box() picks one.
    constexpr glm::vec4 solid{1.f, 0.f, 0.f, 0.f};
    for (const auto& v : kMolotovBottleVertices) {
        const glm::vec3 position{v.px, v.py, v.pz};
        // The lathe's own UVs addressed the source label atlas, which is not
        // cooked. A cylindrical unwrap keeps the field meaningful for any
        // later tiling material without pretending the old atlas still exists.
        const glm::vec2 uv{
            .5f + std::atan2(position.z, position.x) / (2.f * 3.14159265f),
            position.y / kMolotovBottleSourceHeightM};
        mesh.vertices.push_back({position, glm::vec3{v.nx, v.ny, v.nz}, uv, solid});
        mesh.bounds.expand(position);
    }
    return mesh;
}

}  // namespace apricot
