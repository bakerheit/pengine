#pragma once

#include <array>
#include <glm/gtc/quaternion.hpp>
#include "app/traffic_visual_layout.h"
#include "gfx/primitives.h"
#include "traffic/ambient.h"

namespace apricot {

// Chassis metres, forward -Z. The broad blade fits the traffic footprint;
// these same dimensions are used for the road-clearing sweep.
inline constexpr float kSnowplowBladeHalfWidth = kSnowplowBladeWidthM * .5f;
inline constexpr float kSnowplowBladeOffset = kSnowplowBladeForwardM;
inline constexpr std::array<glm::vec4, 4> kSnowplowPartColors{
    glm::vec4{.96f,.29f,.045f,1.f}, glm::vec4{.10f,.12f,.14f,1.f},
    glm::vec4{.58f,.64f,.66f,1.f}, glm::vec4{.92f,.92f,.82f,1.f}};

namespace snowplow_mesh_detail {
inline void append(MeshData& out, const MeshData& source, glm::vec3 offset,
                   glm::mat3 rotation = glm::mat3{1.f}) {
    const auto base = static_cast<uint32_t>(out.vertices.size());
    for (auto vertex : source.vertices) {
        vertex.position = offset + rotation * vertex.position;
        vertex.normal = rotation * vertex.normal;
        vertex.material_weights = glm::vec4{0.f};
        out.bounds.expand(vertex.position);
        out.vertices.push_back(vertex);
    }
    for (auto index : source.indices) out.indices.push_back(base + index);
}
inline void box(MeshData& out, glm::vec3 offset, glm::vec3 size) {
    append(out, make_box(size * .5f), offset);
}
}

// Orange cab/hopper/blade, dark chassis/windows, silver hardware and white
// service stripes. Four batches keep dozens of details cheap across the fleet.
inline std::array<MeshData, 4> make_snowplow_meshes() {
    using namespace snowplow_mesh_detail;
    std::array<MeshData, 4> out;
    auto& paint=out[0]; auto& dark=out[1]; auto& metal=out[2]; auto& white=out[3];
    box(dark,{0,.58f,.10f},{2.08f,.26f,5.65f});
    box(paint,{0,1.00f,-1.83f},{2.20f,.66f,1.65f});
    box(paint,{0,1.77f,-1.37f},{2.12f,1.10f,1.20f});
    box(paint,{0,2.37f,-1.37f},{2.23f,.15f,1.32f});
    const std::size_t windshield_begin = dark.vertices.size();
    box(dark,{0,1.97f,-1.981f},{1.93f,.61f,.024f});
    // Only the outward front face receives windshield accumulation and wipers.
    // Store pane coordinates separately, preserving the existing texture UVs.
    for (std::size_t i = windshield_begin; i < dark.vertices.size(); ++i) {
        auto& vertex = dark.vertices[i];
        if (vertex.normal.z > -.99f) continue;
        vertex.material_weights = {
            glm::clamp((vertex.position.x + .965f) / 1.93f, 0.f, 1.f),
            glm::clamp((vertex.position.y - (1.97f - .305f)) / .61f, 0.f, 1.f),
            1.93f / .61f, -1.f};
    }
    box(paint,{0,1.97f,-2.005f},{.06f,.64f,.035f});
    for(float side:{-1.f,1.f}) {
        box(dark,{side*1.07f,1.98f,-1.34f},{.026f,.59f,1.03f});
        box(metal,{side*1.13f,.66f,-1.17f},{.23f,.12f,1.0f});
        box(metal,{side*1.18f,1.82f,-1.98f},{.16f,.32f,.12f});
        box(white,{side*1.066f,1.50f,-1.34f},{.028f,.13f,1.03f});
        box(metal,{side*.99f,1.57f,-.94f},{.14f,.035f,.055f});
    }
    // Open salt hopper with a pale granular load, rolled upper lips and ribs.
    box(dark,{0,.95f,1.18f},{2.0f,.24f,3.18f});
    box(paint,{0,1.19f,2.76f},{2.20f,.63f,.16f});
    box(paint,{0,1.48f,-.39f},{2.20f,1.07f,.15f});
    for(float side:{-1.f,1.f}) {
        box(paint,{side*1.035f,1.52f,1.18f},{.17f,1.12f,3.25f});
        box(metal,{side*1.035f,2.105f,1.18f},{.21f,.07f,3.25f});
        box(white,{side*1.128f,1.70f,1.18f},{.025f,.14f,3.02f});
        for(float z:{-.18f,.65f,1.48f,2.40f})
            box(paint,{side*1.145f,1.50f,z},{.085f,1.1f,.075f});
    }
    box(white,{0,1.71f,1.20f},{1.82f,.20f,2.90f});
    box(metal,{0,.76f,2.92f},{1.92f,.20f,.15f});
    box(dark,{0,.47f,2.75f},{.65f,.35f,.45f});
    // Curved mouldboard faces forward; its low cutting edge sits on the road.
    for(int band=0;band<5;++band) {
        const float y=.13f+static_cast<float>(band)*.15f;
        const float z=-kSnowplowBladeOffset-.24f+
            .34f*std::pow((y-.13f)/.60f,2.f);
        const float tilt=glm::radians(-8.f-static_cast<float>(band)*10.f);
        append(paint,make_box({kSnowplowBladeHalfWidth,.095f,.055f}),
               {0,y,z},glm::mat3_cast(glm::angleAxis(tilt,glm::vec3{1,0,0})));
    }
    box(metal,{0,.075f,-3.215f},{2.7f,.10f,.13f});
    for(float x:{-.80f,.80f}) {
        box(dark,{x,.46f,-2.70f},{.16f,.18f,.72f});
        box(metal,{x,.62f,-2.76f},{.065f,.065f,.54f});
    }
    // Tall blade markers show its width from the driver's seat.
    for(float side:{-1.f,1.f}) {
        box(metal,{side*1.30f,.91f,-2.90f},{.035f,.70f,.035f});
        box(paint,{side*1.30f,1.30f,-2.90f},{.065f,.13f,.065f});
    }
    box(dark,{0,1.04f,-2.672f},{1.2f,.36f,.032f});
    for(float y:{.93f,1.04f,1.15f})
        box(metal,{0,y,-2.696f},{1.14f,.023f,.022f});
    box(dark,{0,2.49f,-1.35f},{1.65f,.09f,.27f});
    return out;
}

inline TrafficVisualLayout make_snowplow_visual_layout(const AABB& body) {
    TrafficVisualLayout out;
    out.placed_body_bounds=body;
    out.wheel_radius=.46f;
    out.wheel_centres={glm::vec3{-1.04f,.46f,-1.87f},
        glm::vec3{1.04f,.46f,-1.87f},glm::vec3{-1.04f,.46f,1.96f},
        glm::vec3{1.04f,.46f,1.96f}};
    return out;
}

inline Transform snowplow_beacon_transform(std::size_t side) {
    Transform out;
    out.position={side==0u ? -.65f : .65f,2.64f,-1.35f};
    out.scale={.24f,.23f,.24f};
    return out;
}

inline float snowplow_beacon_power(int64_t step, std::size_t side) {
    // Alternating double flashes keyed solely to the simulation step.
    const auto phase=(static_cast<uint64_t>(step)+side*54u)%108u;
    return phase<13u || (phase>=21u && phase<34u) ? 1.f : .08f;
}

} // namespace apricot
