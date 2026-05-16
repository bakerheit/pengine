#pragma once
//
// Private to the traffic subsystem. Include ONLY from src/game/traffic_*.cpp.
//
// This header exists because TrafficSystem::Assets needs to cross translation
// units within the traffic subsystem: init() in traffic.cpp constructs it,
// spawning code reads per-model dimensions, the AI drive code reads
// ride_height_at_rest during recovery, and the visual sync code reads mesh
// handles and wheel mount positions. Anon-namespace types can't cross TUs,
// so Assets gets a header — but a private one, so engine code outside the
// traffic subsystem stays insulated from this implementation detail.
//
// Note on the "doesn't transitively include traffic.h" check: this header
// MUST include traffic.h because Assets is a nested type of TrafficSystem.
// The encapsulation works in the opposite direction — external consumers of
// the public API include traffic.h alone and see only the forward-declared
// `struct Assets;`. Internal consumers include this header and see the
// definition. Don't include traffic_internal.h from outside src/game/.

#include <vector>

#include <glm/glm.hpp>

#include "game/traffic.h"
#include "render/mesh.h"
#include "render/texture.h"

namespace pengine {

// =============================================================================
// Shared constants used across traffic_*.cpp files.
// Single-consumer constants live in their owning .cpp file's anon namespace;
// only multi-consumer ones land here.
// =============================================================================

// Number of lanes a fresh AI route covers ahead of its starting cell.
// Used by spawn (initial route construction) and drive (route extension).
inline constexpr int ROUTE_LANES = 8;



// =============================================================================
// Assets — loaded once at init, shared across every Car.
// =============================================================================
struct TrafficSystem::Assets {
    // Wheel asset is shared across every car model.
    Mesh    wheel_mesh;
    Texture wheel_tex;
    bool    wheel_ok            = false;
    float   wheel_visual_scale  = 1.f;
    float   wheel_visible_radius = 0.f;

    // Per-car-model resources. Indexed by Car::model_id (which corresponds
    // to the slot of CAR_MODELS[]). Each model is loaded independently — if
    // one fails, init() bails for the whole TrafficSystem.
    struct ModelAssets {
        Mesh                 body_mesh;
        std::vector<Texture> paints;          // sized to def.paint_count
        bool                 body_ok = false;

        glm::vec3 body_visual_scale  {1.f};   // uniform scale to fit chassis length
        glm::vec3 body_visual_offset {0.f};   // chassis-local offset for body child
        glm::vec3 visual_aabb_min    {0.f};   // body-local visual AABB (post scale + offset)
        glm::vec3 visual_aabb_max    {0.f};
        glm::vec3 wheel_mount[4]     {};      // chassis-local positions

        // Where the chassis-centre sits above the ground at static rest, for
        // AI cars (kinematic) to line up with player visually. Per-model
        // because chassis dimensions and wheel/suspension tuning differ.
        float ride_height_at_rest = 1.0f;

        // Suspension extension at static rest. AI cars never run wheel
        // raycasts so their Wheel::visual_drop stays zero — wheels would
        // render at mount level (too high). We stamp this value in
        // sync_visuals for AI cars so they match the physics-settled rest:
        //   static_compression = mass * |gravity| / (4 * spring_k)
        //   static_visual_drop = suspension_rest - static_compression
        float static_visual_drop = 0.f;
    };
    std::vector<ModelAssets> models;   // sized to NUM_CAR_MODELS at init()
};

} // namespace pengine
