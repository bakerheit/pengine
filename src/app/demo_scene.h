#pragma once

#include <cstdint>
#include <vector>

#include "gfx/renderer.h"
#include "physics/terrain_collider.h"
#include "scene/scene.h"

namespace apricot {

// PLACEHOLDER CONTENT, and it is meant to be deleted.
//
// A ground plane and a field of boxes, enough to prove the renderer draws a
// lit, batched, fogged world under a moving sky. The terrain agent owns real
// terrain and lands separately; when it does, the ground half of this goes and
// the boxes become scatter props or go too.
//
// The ground is a flat grid DISPLACED by TerrainCollider::height(), not a flat
// sheet. That is one extra line and it is the difference between a car that
// drives on the ground and a car that hovers twenty metres above a blue plane
// while everything is technically working.
struct DemoScene {
    MeshId ground_mesh = kInvalidId;
    MeshId box_mesh = kInvalidId;

    MaterialId ground_material = kInvalidId;
    MaterialId car_material = kInvalidId;
    std::vector<MaterialId> box_materials;

    NodeId ground_node = kInvalidId;
    NodeId car_node = kInvalidId;
    std::vector<NodeId> box_nodes;

    // Half-extents of the car box, so the app can sit it on its wheels rather
    // than half-buried.
    glm::vec3 car_half{0.9f, 0.6f, 2.0f};
};

// Build the whole demo world. `box_count` boxes are scattered over a disc of
// `field_radius` metres, sharing ONE mesh and a handful of materials so that
// after Scene::cull's batch-key sort they collapse into a handful of instanced
// draws. That collapse is the thing the debug overlay's A/B toggle measures, so
// the count wants to be high enough that the difference is obvious.
//
// Returns false (having logged why) if any GPU resource failed; the caller must
// not render a half-built scene.
bool build_demo_scene(Scene& scene, Renderer& renderer,
                      const TerrainCollider& collider, uint64_t seed,
                      int box_count, float field_radius, DemoScene& out);

}  // namespace apricot
