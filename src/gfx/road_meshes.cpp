#include "gfx/road_meshes.h"

#include <utility>

#include "core/asset_root.h"
#include "core/log.h"
#include "core/transform.h"
#include "gfx/primitives.h"
#include "gfx/texture.h"

namespace apricot {
namespace {

// The three lines src/road/README.md asks the host layer for, written once.
//
// RoadMesh and MeshData are structurally identical — the same three members, in
// the same order, over the same TerrainVertex — and are separate types only
// because MeshData lives host-side and dependencies flow sim -> host. This is
// the whole cost of that boundary, and it is a fair one.
//
// It copies rather than moves because the caller's RibbonBake is const: a bake
// is a pure function of its inputs and gutting it on upload would mean the
// second consumer (build_road_collision) got an empty one. The copy is once per
// bake, not per frame.
MeshData to_mesh_data(const RoadMesh& src) {
    MeshData m;
    m.vertices = src.vertices;
    m.indices = src.indices;
    m.bounds = src.bounds;
    return m;
}

// The ribbon baker keeps road collision and visible geometry physically
// aligned. These offsets only pull coplanar-looking fragments toward the
// camera after projection, where a distant road's 6 cm terrain clearance can
// collapse to the same depth-buffer value as the ground underneath it.
//
// Paint gets a second tier so its smaller 18-30 mm lift still resolves over
// the biased asphalt. Kerb walls and structures are real volume and must keep
// ordinary depth behavior.
constexpr Renderer::DepthBias road_depth_bias(RoadLayer layer) {
    switch (layer) {
        case RoadLayer::Carriageway:
        case RoadLayer::Unpaved:
        case RoadLayer::Walk:
        case RoadLayer::Plate:
            return {-1.0f, -1.0f};
        case RoadLayer::Crosswalk:
        case RoadLayer::WhiteMarking:
        case RoadLayer::YellowMarking:
            return {-2.0f, -2.0f};
        case RoadLayer::Kerb:
        case RoadLayer::Structure:
            return {};
    }
    return {};
}

static_assert(road_depth_bias(RoadLayer::Carriageway).enabled());
static_assert(road_depth_bias(RoadLayer::WhiteMarking).units <
              road_depth_bias(RoadLayer::Carriageway).units);
static_assert(!road_depth_bias(RoadLayer::Structure).enabled());

}  // namespace

bool RoadMeshes::init(Renderer& renderer, uint64_t seed) {
    // One material per layer, because the layer split IS the material split —
    // that is what src/road/ribbon.h says the eight layers are for. Kerb risers
    // are their own layer rather than part of the sidewalk precisely so the
    // vertical faces can bind something different from the slabs above them.
    struct LayerLook {
        RoadLayer layer;
        glm::vec3 lo;
        glm::vec3 hi;
        int size;
        int octaves;
        uint64_t salt;
    };
    const LayerLook looks[kRoadLayerCount] = {
        {RoadLayer::Carriageway, {0.16f, 0.16f, 0.17f}, {0.27f, 0.27f, 0.29f},
         256, 4, 0xA5F1A17ull},
        {RoadLayer::Unpaved, {0.29f, 0.23f, 0.16f}, {0.49f, 0.40f, 0.28f}, 128,
         4, 0xD127ull},
        {RoadLayer::Walk, {0.52f, 0.51f, 0.49f}, {0.72f, 0.71f, 0.68f}, 128, 3,
         0x5717Aull},
        {RoadLayer::Kerb, {0.44f, 0.43f, 0.41f}, {0.60f, 0.59f, 0.56f}, 64, 2,
         0x4E12Bull},
        {RoadLayer::Plate, {0.17f, 0.17f, 0.18f}, {0.27f, 0.27f, 0.29f}, 256, 4,
         0x914AEull},
        {RoadLayer::Crosswalk, {0.68f, 0.68f, 0.65f}, {0.92f, 0.92f, 0.88f}, 64,
         3, 0x2EB2Aull},
        {RoadLayer::WhiteMarking, {0.66f, 0.66f, 0.62f},
         {0.92f, 0.92f, 0.86f}, 64, 3, 0xA11CEull},
        {RoadLayer::YellowMarking, {0.68f, 0.49f, 0.05f},
         {0.96f, 0.78f, 0.16f}, 64, 3, 0x7E110ull},
        {RoadLayer::Structure, {0.34f, 0.33f, 0.31f},
         {0.57f, 0.56f, 0.52f}, 64, 3, 0xB21D6Eull},
    };

    for (const LayerLook& look : looks) {
        Texture t;
        const bool asphalt = look.layer == RoadLayer::Carriageway ||
                             look.layer == RoadLayer::Plate;
        const bool paving = look.layer == RoadLayer::Walk;
        // The source pack's lane paint belongs to fixed-width tiles. Apricot
        // draws its own lane paint over variable-width ribbons, so use the
        // pack's asphalt aggregate here and let every bend and junction share
        // the same world-space UVs.
        const bool made = asphalt
            ? t.load_file(asset_path("textures/world/roads/psx-asphalt.png"))
            : (paving
                ? t.load_file(asset_path("textures/world/roads/psx-paving.png"))
                : t.make_noise(look.size, 8, look.octaves, look.lo, look.hi,
                               seed ^ look.salt));
        if (!made) {
            AP_ERROR("road: texture generation failed for the %s layer",
                     road_layer_name(look.layer));
            return false;
        }
        // Aggregate scatters highlights. Keep road paint a little brighter,
        // while rain still raises the lighting environment's wet sheen.
        const bool paint = look.layer == RoadLayer::Crosswalk ||
                           look.layer == RoadLayer::WhiteMarking ||
                           look.layer == RoadLayer::YellowMarking;
        layers_[road_layer_index(look.layer)].material =
            renderer.add_material(std::move(t), false,
                                  asphalt ? 0.18f : (paint ? 0.30f : 1.0f),
                                  road_depth_bias(look.layer));
    }
    return true;
}

bool RoadMeshes::upload(Renderer& renderer, const RibbonBake& bake) {
    // Whatever was here before goes first. Re-uploading over a live handle
    // would strand the old mesh in the renderer's table with nothing pointing
    // at it — the exact leak the streamer's released-mesh list exists to stop,
    // and it would be no less a leak for happening only on a rebake.
    release(renderer);

    for (std::size_t i = 0; i < kRoadLayerCount; ++i) {
        const RoadMesh& src = bake.layers[i];
        if (src.empty()) continue;  // a bake with no crosswalks is normal

        const MeshId id = renderer.add_mesh(to_mesh_data(src));
        if (id == kInvalidId) {
            AP_ERROR("road: the %s layer failed to upload (%zu verts, %zu "
                     "tris); releasing the rest rather than drawing half a road",
                     road_layer_name(static_cast<RoadLayer>(i)),
                     src.vertices.size(), src.triangle_count());
            release(renderer);
            return false;
        }

        layers_[i].mesh = id;
        layers_[i].bounds = src.bounds;
        ++uploaded_layers_;
        triangles_ += src.triangle_count();
        bytes_ += src.vertices.size() * sizeof(TerrainVertex) +
                  src.indices.size() * sizeof(uint32_t);
    }

    AP_INFO("road: uploaded %zu layers, %zu triangles, %.2f MB",
            uploaded_layers_, triangles_,
            static_cast<double>(bytes_) / (1024.0 * 1024.0));
    return true;
}

void RoadMeshes::attach(Scene& scene, float draw_distance) {
    for (Layer& l : layers_) {
        if (l.mesh == kInvalidId || l.node != kInvalidId) continue;

        Renderable r;
        r.mesh = l.mesh;
        r.material = l.material;
        // Ribbon UVs are already in world metres over the layer's own tile
        // size, set by RibbonParams. Scaling them again here would retile the
        // road on top of the tiling the baker chose, and lane markings would
        // stop lining up with the lanes.
        r.uv_scale = glm::vec2{1.0f};

        l.node = scene.create(r, Transform{}, l.bounds);
        if (SceneNode* n = scene.get(l.node)) {
            n->max_draw_distance = draw_distance;
        }
    }
}

void RoadMeshes::detach(Scene& scene) {
    for (Layer& l : layers_) {
        if (l.node == kInvalidId) continue;
        scene.remove(l.node);
        l.node = kInvalidId;
    }
}

void RoadMeshes::release(Renderer& renderer) {
    for (Layer& l : layers_) {
        if (l.mesh == kInvalidId) continue;
        renderer.remove_mesh(l.mesh);
        l.mesh = kInvalidId;
        l.bounds = AABB{};
    }
    uploaded_layers_ = 0;
    triangles_ = 0;
    bytes_ = 0;
}

}  // namespace apricot
