#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "city/start_area.h"
#include "road/road_graph.h"
#include "road/ribbon.h"

namespace apricot::city {

// WESTMERE ESTATES occupies the dry bluff between Saltmarsh and the Kessel
// Channel. It is a neighborhood, not an eleventh district: the big lots,
// branching courts, gates and landscaping provide its identity without changing
// police response rules for the surrounding Meadows.
struct LuxuryEstate {
    StartSite site;
    StartFinish wall_finish = StartFinish::WarmWall;
    StartFinish trim_finish = StartFinish::White;
    float wall_height_m = 6.4f;
    bool flat_roof = false;
    bool pool = true;
    uint32_t road_id = 241;
};

inline constexpr std::array<LuxuryEstate, 9> kLuxuryEstates{{
    {{"1 Laurel Court", {-808.99f, -412.64f}, -0.965926f, -0.258819f,
      {0, 0}, 64.0f, 70.0f, 5.2f, 1200.0f},
     StartFinish::WarmWall, StartFinish::White, 6.5f, false, true, 241},
    {{"3 Laurel Court", {-905.15f, -420.80f}, -0.707107f, 0.707107f,
      {0, 0}, 64.0f, 70.0f, 5.9f, 1200.0f},
     StartFinish::Brick, StartFinish::White, 6.8f, false, true, 241},
    {{"5 Laurel Court", {-753.84f, -491.83f}, -0.258819f, -0.965926f,
      {0, 0}, 64.0f, 70.0f, 6.4f, 1200.0f},
     StartFinish::White, StartFinish::TealDoor, 6.3f, true, false, 241},
    {{"1 Cedar Court", {-1097.36f, -583.99f}, -0.258819f, 0.965926f,
      {0, 0}, 64.0f, 70.0f, 4.6f, 1200.0f},
     StartFinish::WarmWall, StartFinish::White, 6.6f, false, true, 242},
    {{"3 Cedar Court", {-1089.20f, -680.15f}, 0.707107f, 0.707107f,
      {0, 0}, 64.0f, 70.0f, 4.6f, 1200.0f},
     StartFinish::Brick, StartFinish::TealDoor, 6.4f, true, true, 242},
    {{"5 Cedar Court", {-1018.17f, -528.84f}, -0.965926f, 0.258819f,
      {0, 0}, 64.0f, 70.0f, 4.8f, 1200.0f},
     StartFinish::White, StartFinish::White, 6.9f, false, false, 242},
    {{"1 Magnolia Court", {-861.01f, -787.36f}, 0.965926f, 0.258819f,
      {0, 0}, 64.0f, 70.0f, 3.7f, 1200.0f},
     StartFinish::WarmWall, StartFinish::White, 6.5f, false, true, 243},
    {{"3 Magnolia Court", {-764.85f, -779.20f}, 0.707107f, -0.707107f,
      {0, 0}, 64.0f, 70.0f, 5.2f, 1200.0f},
     StartFinish::Brick, StartFinish::White, 6.7f, false, true, 243},
    {{"5 Magnolia Court", {-916.16f, -708.17f}, 0.258819f, 0.965926f,
      {0, 0}, 64.0f, 70.0f, 4.9f, 1200.0f},
     StartFinish::White, StartFinish::TealDoor, 6.4f, true, false, 243},
}};

inline constexpr std::array<StartSite, 3> kWestmereCourtGreens{{
    {"Laurel Court Green", {-850.0f, -500.0f}, 1.0f, 0.0f,
     {0, 0}, 42.0f, 42.0f, 6.0f, 1200.0f},
    {"Cedar Court Green", {-1010.0f, -625.0f}, 1.0f, 0.0f,
     {0, 0}, 42.0f, 42.0f, 6.0f, 1200.0f},
    {"Magnolia Court Green", {-820.0f, -700.0f}, 1.0f, 0.0f,
     {0, 0}, 42.0f, 42.0f, 6.0f, 1200.0f},
}};

inline constexpr StartSite kWestmereCommonSite{
    "Westmere Community Club", {-620.0f, -555.0f}, 1.0f, 0.0f,
    {0, 0}, 120.0f, 105.0f, 7.0f, 1200.0f};

inline constexpr StartSite kWestmereGateSite{
    "Westmere Gate", {-655.0f, -438.0f}, -0.83205f, -0.55470f,
    {0, 0}, 34.0f, 30.0f, 6.0f, 1200.0f};

inline glm::vec2 luxury_world(const StartSite& site, glm::vec2 local) {
    return {site.origin.x + site.cos_yaw * local.x + site.sin_yaw * local.y,
            site.origin.z - site.sin_yaw * local.x + site.cos_yaw * local.y};
}

inline float luxury_height(const StartSite& site, GroundSampler ground,
                           float x, float z) {
    const auto world = luxury_world(site, {x, z});
    return ground.at(world.x, world.y) - site.ground_m;
}

inline float luxury_floor_top(std::size_t index, GroundSampler ground) {
    const auto& site = kLuxuryEstates.at(index).site;
    float top = 0.0f;
    for (float x = -22.0f; x <= 24.0f; x += 0.5f) {
        for (float z = -12.0f; z <= 6.0f; z += 0.5f) {
            top = std::max(top, luxury_height(site, ground, x, z));
        }
    }
    return top + 0.16f;
}

inline bool luxury_ground_piece(const StartPart& part) {
    if (!part.name) return false;
    return std::strcmp(part.name, "house luxury interior floor") == 0 ||
           std::strcmp(part.name, "house luxury garage floor") == 0 ||
           std::strcmp(part.name, "house luxury porch floor") == 0 ||
           std::strcmp(part.name, "house luxury front walk") == 0 ||
           std::strcmp(part.name, "house luxury pool terrace") == 0 ||
           std::strcmp(part.name, "westmere common walk") == 0 ||
           std::strcmp(part.name, "westmere pool terrace") == 0 ||
           std::strcmp(part.name, "westmere tennis court") == 0 ||
           std::strcmp(part.name, "westmere pavilion floor") == 0 ||
           std::strcmp(part.name, "westmere gate walk") == 0;
}

// Westmere owns a small, explicit material kit. Keeping this classifier in the
// authored city layer makes the mapping headless-testable and stops generic
// finish names (notably TealDoor) from turning foliage into painted metal.
enum class WestmereMaterialRole : std::uint8_t {
    Default,
    PeachStucco,
    CreamStucco,
    RoofTile,
    Limestone,
    GreenMetal,
    PoolTile,
    TennisSurface,
    WhiteSlats,
    TreeBark,
    Foliage,
    BenchTimber,
    LoungerFabric,
    EntrySign,
};

inline bool westmere_name_is(const StartPart& part, const char* name) {
    return part.name && std::strcmp(part.name, name) == 0;
}

inline bool westmere_name_has(const StartPart& part, const char* text) {
    return part.name && std::strstr(part.name, text) != nullptr;
}

inline WestmereMaterialRole westmere_material_role(const StartPart& part) {
    if (!part.name) return WestmereMaterialRole::Default;

    if (westmere_name_is(part, "westmere streetscape entry sign face"))
        return WestmereMaterialRole::EntrySign;
    if (westmere_name_is(part, "westmere tennis surface"))
        return WestmereMaterialRole::TennisSurface;
    if (westmere_name_has(part, "pool coping"))
        return WestmereMaterialRole::PoolTile;
    if (westmere_name_has(part, "pool fence"))
        return WestmereMaterialRole::WhiteSlats;
    if (westmere_name_has(part, "pool lounger"))
        return WestmereMaterialRole::LoungerFabric;
    if (westmere_name_has(part, "bench seat") ||
        westmere_name_has(part, "bench back") ||
        westmere_name_is(part, "westmere court bench") ||
        westmere_name_is(part, "westmere court bench back") ||
        westmere_name_is(part, "westmere garden bench") ||
        westmere_name_is(part, "westmere garden bench back"))
        return WestmereMaterialRole::BenchTimber;
    if (westmere_name_is(part, "luxury tree trunk") ||
        westmere_name_is(part, "westmere streetscape palm trunk"))
        return WestmereMaterialRole::TreeBark;
    if (westmere_name_is(part, "luxury tree crown") ||
        westmere_name_is(part, "westmere court shrub") ||
        westmere_name_has(part, "ornamental shrub") ||
        westmere_name_has(part, "palm crown") ||
        westmere_name_has(part, "palm frond"))
        return WestmereMaterialRole::Foliage;
    if (westmere_name_has(part, "mailbox") ||
        westmere_name_has(part, "utility cabinet") ||
        westmere_name_has(part, "trash bin"))
        return WestmereMaterialRole::GreenMetal;

    if (westmere_name_is(part, "house luxury main roof") ||
        westmere_name_is(part, "house luxury garage roof") ||
        westmere_name_is(part, "westmere pavilion roof") ||
        westmere_name_is(part, "westmere gatehouse roof"))
        return WestmereMaterialRole::RoofTile;

    if (westmere_name_has(part, "house luxury foundation") ||
        westmere_name_has(part, "house luxury boundary wall") ||
        westmere_name_has(part, "house luxury facade base course") ||
        westmere_name_has(part, "house luxury address plinth") ||
        westmere_name_has(part, "westmere gate pier") ||
        westmere_name_has(part, "entry planter curb") ||
        westmere_name_has(part, "entry sign plinth") ||
        westmere_name_has(part, "entry sign backing") ||
        westmere_name_has(part, "entry sign cap"))
        return WestmereMaterialRole::Limestone;

    const bool estate_wall =
        westmere_name_is(part, "house luxury front wall") ||
        westmere_name_is(part, "house luxury rear wall") ||
        westmere_name_is(part, "house luxury west wall") ||
        westmere_name_is(part, "house luxury east wall") ||
        westmere_name_is(part, "house luxury garage front") ||
        westmere_name_is(part, "house luxury garage rear") ||
        westmere_name_is(part, "house luxury garage west") ||
        westmere_name_is(part, "house luxury garage east") ||
        westmere_name_is(part, "gable end base") ||
        westmere_name_is(part, "gable end wall");
    if (estate_wall && part.finish == StartFinish::WarmWall)
        return WestmereMaterialRole::PeachStucco;
    if (estate_wall && part.finish == StartFinish::White)
        return WestmereMaterialRole::CreamStucco;
    if (westmere_name_is(part, "westmere gatehouse") ||
        westmere_name_is(part, "westmere clubhouse rear wall"))
        return WestmereMaterialRole::CreamStucco;

    return WestmereMaterialRole::Default;
}

inline constexpr float kLuxuryDrivewayCentreX = 17.0f;
inline constexpr float kLuxuryDrivewayWidthM = 6.6f;
inline constexpr float kLuxuryDrivewayStartZ = -11.0f;
inline constexpr float kLuxuryDrivewayStepM = 1.0f;
inline constexpr std::size_t kLuxuryDrivewaySectionCount = 47u;

using LuxuryDrivewayProfile =
    std::array<float, kLuxuryDrivewaySectionCount>;

// Top elevations for a continuous driveway ribbon. The profile remains above
// every sampled terrain cross-section, then takes the least 9%-grade envelope
// so long vehicles never meet a sharp pitch break.
inline LuxuryDrivewayProfile luxury_driveway_profile(std::size_t index,
                                                       GroundSampler ground) {
    const auto& site = kLuxuryEstates.at(index).site;
    const float floor = luxury_floor_top(index, ground);
    LuxuryDrivewayProfile top{};
    for (std::size_t section = 0; section < top.size(); ++section) {
        const float z = kLuxuryDrivewayStartZ +
                        static_cast<float>(section) * kLuxuryDrivewayStepM;
        float y = 0.0f;
        for (float x : {kLuxuryDrivewayCentreX - kLuxuryDrivewayWidthM * 0.5f,
                        kLuxuryDrivewayCentreX,
                        kLuxuryDrivewayCentreX + kLuxuryDrivewayWidthM * 0.5f}) {
            y = std::max(y, luxury_height(site, ground, x, z));
        }
        if (z <= 6.0f) y = std::max(y, floor);
        top[section] = y + 0.04f;
    }
    for (std::size_t i = 0; i < top.size(); ++i) {
        for (std::size_t j = 0; j < top.size(); ++j) {
            top[i] = std::max(
                top[i], top[j] - 0.09f * kLuxuryDrivewayStepM *
                                     std::fabs(static_cast<float>(i) -
                                               static_cast<float>(j)));
        }
    }
    return top;
}

struct LuxuryDrivewayMesh {
    RoadMesh surface;
    RoadMesh edge;
};

inline void luxury_driveway_face(RoadMesh& mesh, glm::vec3 a, glm::vec3 b,
                                  glm::vec3 c, glm::vec3 outward,
                                  glm::vec2 uv_a, glm::vec2 uv_b,
                                  glm::vec2 uv_c) {
    glm::vec3 normal = glm::cross(b - a, c - a);
    if (glm::length(normal) < 1e-7f) return;
    if (glm::dot(normal, outward) < 0.0f) {
        std::swap(b, c);
        std::swap(uv_b, uv_c);
        normal = -normal;
    }
    normal = glm::normalize(normal);
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (const auto& vertex :
         {TerrainVertex{a, normal, uv_a, {1, 0, 0, 0}},
          TerrainVertex{b, normal, uv_b, {1, 0, 0, 0}},
          TerrainVertex{c, normal, uv_c, {1, 0, 0, 0}}}) {
        mesh.vertices.push_back(vertex);
        mesh.bounds.expand(vertex.position);
    }
    mesh.indices.insert(mesh.indices.end(), {base, base + 1u, base + 2u});
}

// One indexed, continuously sloped top replaces the old chain of horizontal
// one-metre boxes. A terrain-following side face hides any raised grade
// envelope without creating another drivable collision surface.
inline LuxuryDrivewayMesh bake_luxury_driveway(std::size_t index,
                                                GroundSampler ground) {
    const auto& site = kLuxuryEstates.at(index).site;
    const auto profile = luxury_driveway_profile(index, ground);
    LuxuryDrivewayMesh out;
    std::array<glm::vec3, kLuxuryDrivewaySectionCount> left{};
    std::array<glm::vec3, kLuxuryDrivewaySectionCount> right{};
    std::array<glm::vec3, kLuxuryDrivewaySectionCount> left_foot{};
    std::array<glm::vec3, kLuxuryDrivewaySectionCount> right_foot{};
    const float half_width = kLuxuryDrivewayWidthM * 0.5f;

    for (std::size_t section = 0; section < profile.size(); ++section) {
        const float z = kLuxuryDrivewayStartZ +
                        static_cast<float>(section) * kLuxuryDrivewayStepM;
        const auto left_xz = luxury_world(
            site, {kLuxuryDrivewayCentreX - half_width, z});
        const auto right_xz = luxury_world(
            site, {kLuxuryDrivewayCentreX + half_width, z});
        left[section] = {left_xz.x, site.ground_m + profile[section],
                         left_xz.y};
        right[section] = {right_xz.x, site.ground_m + profile[section],
                          right_xz.y};
        left_foot[section] = left[section];
        right_foot[section] = right[section];
        left_foot[section].y = std::min(
            left[section].y - 0.04f,
            site.ground_m + luxury_height(
                                site, ground,
                                kLuxuryDrivewayCentreX - half_width, z) +
                0.01f);
        right_foot[section].y = std::min(
            right[section].y - 0.04f,
            site.ground_m + luxury_height(
                                site, ground,
                                kLuxuryDrivewayCentreX + half_width, z) +
                0.01f);
    }

    out.surface.vertices.reserve(profile.size() * 2u);
    for (std::size_t section = 0; section < profile.size(); ++section) {
        const std::size_t before = section == 0 ? 0 : section - 1;
        const std::size_t after =
            section + 1 < profile.size() ? section + 1 : section;
        const glm::vec3 centre_before =
            (left[before] + right[before]) * 0.5f;
        const glm::vec3 centre_after = (left[after] + right[after]) * 0.5f;
        glm::vec3 normal = glm::normalize(glm::cross(
            centre_after - centre_before, right[section] - left[section]));
        if (normal.y < 0.0f) normal = -normal;
        const float v = static_cast<float>(section) *
                        kLuxuryDrivewayStepM / RibbonParams{}.slab_m;
        for (const auto& vertex :
             {TerrainVertex{left[section], normal, {0.0f, v}, {1, 0, 0, 0}},
              TerrainVertex{right[section], normal,
                            {kLuxuryDrivewayWidthM / RibbonParams{}.slab_m, v},
                            {1, 0, 0, 0}}}) {
            out.surface.vertices.push_back(vertex);
            out.surface.bounds.expand(vertex.position);
        }
    }
    for (std::size_t section = 0; section + 1 < profile.size(); ++section) {
        const uint32_t a = static_cast<uint32_t>(section * 2u);
        const uint32_t b = a + 2u;
        out.surface.indices.insert(out.surface.indices.end(),
                                   {a, b, b + 1u, a, b + 1u, a + 1u});

        const float v0 = static_cast<float>(section) * kLuxuryDrivewayStepM;
        const float v1 = v0 + kLuxuryDrivewayStepM;
        const glm::vec3 centre = (left[section] + right[section]) * 0.5f;
        const glm::vec3 left_out = glm::normalize(left[section] - centre);
        const glm::vec3 right_out = -left_out;
        luxury_driveway_face(out.edge, left[section], left_foot[section],
                             left_foot[section + 1], left_out, {v0, 0},
                             {v0, left[section].y - left_foot[section].y},
                             {v1, left[section + 1].y -
                                      left_foot[section + 1].y});
        luxury_driveway_face(out.edge, left[section], left_foot[section + 1],
                             left[section + 1], left_out, {v0, 0},
                             {v1, left[section + 1].y -
                                      left_foot[section + 1].y},
                             {v1, 0});
        luxury_driveway_face(out.edge, right[section],
                             right_foot[section + 1],
                             right_foot[section], right_out, {v0, 0},
                             {v1, right[section + 1].y -
                                      right_foot[section + 1].y},
                             {v0, right[section].y - right_foot[section].y});
        luxury_driveway_face(out.edge, right[section], right[section + 1],
                             right_foot[section + 1], right_out, {v0, 0},
                             {v1, 0},
                             {v1, right[section + 1].y -
                                      right_foot[section + 1].y});
    }
    return out;
}

inline void append_luxury_driveway_mesh(RoadMesh& target,
                                         const RoadMesh& source) {
    const uint32_t base = static_cast<uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(),
                           source.vertices.end());
    for (uint32_t index : source.indices) target.indices.push_back(base + index);
    target.bounds.expand(source.bounds);
}

inline void append_luxury_driveways(RibbonBake& roads, GroundSampler ground) {
    for (std::size_t index = 0; index < kLuxuryEstates.size(); ++index) {
        const auto driveway = bake_luxury_driveway(index, ground);
        append_luxury_driveway_mesh(roads.layer(RoadLayer::Walk),
                                    driveway.surface);
        append_luxury_driveway_mesh(roads.layer(RoadLayer::Kerb),
                                    driveway.edge);
    }
}

inline std::vector<StartPart> bake_luxury_estate(std::size_t index,
                                                 GroundSampler ground) {
    const auto& estate = kLuxuryEstates.at(index);
    const auto& site = estate.site;
    const float floor = luxury_floor_top(index, ground);
    std::vector<StartPart> out;
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw = 0.0f) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid, 0.0f, yaw});
    };

    // Main two-storey volume plus an attached two-car garage. Foundations are
    // deep on purpose: the natural bluff remains visible between level homes.
    add("house luxury foundation", -6.0f, -3.0f, 0.0f, 31.0f, floor, 18.0f,
        StartFinish::Concrete, true);
    add("house luxury garage foundation", 17.0f, -3.0f, 0.0f, 13.0f, floor,
        18.0f, StartFinish::Concrete);
    add("house luxury interior floor", -6.0f, -3.0f, floor - 0.12f, 31.0f,
        0.12f, 18.0f, StartFinish::WarmWall, true);
    add("house luxury garage floor", 17.0f, -3.0f, floor - 0.12f, 13.0f,
        0.12f, 18.0f, StartFinish::Concrete);

    const BuildingOpening front[] = {
        {"house luxury west lower window", OpeningKind::Window, 6.0f, 3.8f,
         0.95f, 1.75f, StartFinish::Glass, 2, 1, estate.trim_finish},
        {"house luxury main door", OpeningKind::Door, 15.5f, 2.4f,
         0.0f, 2.75f, estate.trim_finish, 0, 0, estate.trim_finish},
        {"house luxury east lower window", OpeningKind::Window, 25.0f, 3.8f,
         0.95f, 1.75f, StartFinish::Glass, 2, 1, estate.trim_finish},
        {"house luxury west upper window", OpeningKind::Window, 6.0f, 3.5f,
         4.05f, 1.55f, StartFinish::Glass, 2, 1, estate.trim_finish},
        {"house luxury centre upper window", OpeningKind::Window, 15.5f, 3.2f,
         4.05f, 1.55f, StartFinish::Glass, 1, 1, estate.trim_finish},
        {"house luxury east upper window", OpeningKind::Window, 25.0f, 3.5f,
         4.05f, 1.55f, StartFinish::Glass, 2, 1, estate.trim_finish},
    };
    const BuildingOpening rear[] = {
        {"house luxury rear lower window", OpeningKind::Window, 6.0f, 4.2f,
         0.95f, 1.75f, StartFinish::Glass, 2, 1, estate.trim_finish},
        {"house luxury garden door", OpeningKind::Door, 15.5f, 2.0f,
         0.0f, 2.55f, estate.trim_finish, 0, 0, estate.trim_finish},
        {"house luxury rear lounge window", OpeningKind::Window, 25.0f, 4.2f,
         0.95f, 1.75f, StartFinish::Glass, 2, 1, estate.trim_finish},
        {"house luxury rear upper window west", OpeningKind::Window, 6.0f,
         3.6f, 4.05f, 1.55f, StartFinish::Glass, 2, 1,
         estate.trim_finish},
        {"house luxury rear upper window east", OpeningKind::Window, 25.0f,
         3.6f, 4.05f, 1.55f, StartFinish::Glass, 2, 1,
         estate.trim_finish},
    };
    const BuildingOpening side[] = {
        {"house luxury side lower window", OpeningKind::Window, 5.0f, 3.4f,
         0.95f, 1.75f, StartFinish::Glass, 2, 1, estate.trim_finish},
        {"house luxury side upper window", OpeningKind::Window, 13.0f, 3.4f,
         4.05f, 1.55f, StartFinish::Glass, 2, 1, estate.trim_finish},
    };
    const BuildingOpening garage[] = {
        {"house luxury double garage door", OpeningKind::Door, 6.5f, 5.7f,
         0.0f, 2.75f, estate.trim_finish, 0, 0, estate.trim_finish},
    };
    const BuildingWall walls[] = {
        {"house luxury front wall", {-21.5f, 6.0f}, {9.5f, 6.0f}, floor,
         estate.wall_height_m, 0.28f, estate.wall_finish, front, 6},
        {"house luxury rear wall", {-21.5f, -12.0f}, {9.5f, -12.0f}, floor,
         estate.wall_height_m, 0.28f, estate.wall_finish, rear, 5},
        {"house luxury west wall", {-21.5f, -12.0f}, {-21.5f, 6.0f}, floor,
         estate.wall_height_m, 0.28f, estate.wall_finish, side, 2},
        {"house luxury east wall", {9.5f, -12.0f}, {9.5f, 6.0f}, floor,
         estate.wall_height_m, 0.28f, estate.wall_finish, side, 2},
        {"house luxury garage front", {10.5f, 6.0f}, {23.5f, 6.0f}, floor,
         3.25f, 0.28f, estate.wall_finish, garage, 1},
        {"house luxury garage rear", {10.5f, -12.0f}, {23.5f, -12.0f}, floor,
         3.25f, 0.28f, estate.wall_finish},
        {"house luxury garage west", {10.5f, -12.0f}, {10.5f, 6.0f}, floor,
         3.25f, 0.28f, estate.wall_finish},
        {"house luxury garage east", {23.5f, -12.0f}, {23.5f, 6.0f}, floor,
         3.25f, 0.28f, estate.wall_finish},
    };
    const BuildingRoof roofs[] = {
        {"house luxury main roof", {-6.0f, -3.0f},
         floor + estate.wall_height_m, 31.0f, 18.0f,
         estate.flat_roof ? 0.0f : 2.35f, 0.24f, 0.65f,
         estate.flat_roof ? 0.72f : 0.0f,
         estate.flat_roof ? RoofStyle::Flat : RoofStyle::Gable,
         index % 2 == 0 ? RidgeAxis::AlongX : RidgeAxis::AlongZ,
         StartFinish::DarkRoof, estate.wall_finish,
         estate.flat_roof ? 0.0f : 0.28f, estate.wall_finish},
        {"house luxury garage roof", {17.0f, -3.0f}, floor + 3.25f,
         13.0f, 18.0f, estate.flat_roof ? 0.0f : 1.25f,
         0.22f, 0.55f, estate.flat_roof ? 0.55f : 0.0f,
         estate.flat_roof ? RoofStyle::Flat : RoofStyle::Gable,
         RidgeAxis::AlongZ, StartFinish::DarkRoof, estate.wall_finish,
         estate.flat_roof ? 0.0f : 0.28f, estate.wall_finish},
    };
    auto shell = bake_building({site.name, walls, 8, roofs, 2});
    out.insert(out.end(), shell.begin(), shell.end());

    // Chunky trim gives the long box facade a readable PS2-era silhouette at
    // driving distance. Every home shares a grounded base and roof cornice,
    // while the three court addresses cycle through distinct facade families.
    add("house luxury facade base course", -14.35f, 6.23f, floor + 0.18f,
        14.1f, 0.55f, 0.18f, estate.trim_finish);
    add("house luxury facade base course", 2.35f, 6.23f, floor + 0.18f,
        14.1f, 0.55f, 0.18f, estate.trim_finish);
    add("house luxury roof trim", -6.0f, 6.31f,
        floor + estate.wall_height_m - 0.16f, 32.0f, 0.32f, 0.24f,
        estate.trim_finish);
    add("house luxury roof trim", -6.0f, -12.31f,
        floor + estate.wall_height_m - 0.16f, 32.0f, 0.32f, 0.24f,
        estate.trim_finish);
    add("house luxury roof trim", -21.81f, -3.0f,
        floor + estate.wall_height_m - 0.16f, 0.24f, 0.32f, 18.6f,
        estate.trim_finish);
    add("house luxury roof trim", 9.81f, -3.0f,
        floor + estate.wall_height_m - 0.16f, 0.24f, 0.32f, 18.6f,
        estate.trim_finish);

    switch (index % 3u) {
        case 0u:
            for (float x : {-20.9f, 8.9f}) {
                add("house luxury stucco pilaster", x, 6.35f, floor + 0.2f,
                    0.72f, estate.wall_height_m - 0.4f, 0.34f,
                    estate.trim_finish);
            }
            add("house luxury entry crown", -6.0f, 6.38f, floor + 2.8f,
                3.4f, 0.34f, 0.38f, estate.trim_finish);
            break;
        case 1u:
            for (float z : {6.34f, -12.34f}) {
                add("house luxury brick string course", -6.0f, z,
                    floor + 3.18f, 31.8f, 0.28f, 0.20f,
                    estate.trim_finish);
            }
            for (float x : {-21.82f, 9.82f}) {
                for (float y : {0.65f, 2.45f, 4.25f}) {
                    add("house luxury brick quoin", x, 6.38f, floor + y,
                        0.70f, 0.70f, 0.40f, estate.trim_finish);
                }
            }
            break;
        default:
            add("house luxury stepped parapet crown", -6.0f, 6.64f,
                floor + estate.wall_height_m + 0.78f, 12.0f, 0.42f, 0.32f,
                estate.trim_finish);
            for (float x : {-11.7f, -0.3f}) {
                add("house luxury flat roof finial", x, 6.65f,
                    floor + estate.wall_height_m + 0.72f, 0.48f, 0.70f,
                    0.48f, estate.trim_finish);
            }
            break;
    }

    // A deep portico and balcony break the large frontage into real scale.
    add("house luxury porch floor", -6.0f, 8.0f, floor - 0.12f, 11.0f,
        0.12f, 4.0f, StartFinish::Concrete, true);
    add("house luxury balcony slab", -6.0f, 7.2f, floor + 3.05f, 11.0f,
        0.20f, 2.4f, StartFinish::Concrete, true);
    for (float x : {-10.8f, -1.2f}) {
        add("house luxury portico column", x, 8.4f, floor, 0.34f, 3.05f,
            0.34f, estate.trim_finish, true);
    }
    for (float x = -10.7f; x <= -1.3f; x += 1.55f) {
        add("house luxury balcony baluster", x, 8.2f, floor + 3.25f,
            0.12f, 0.90f, 0.12f, estate.trim_finish);
    }
    add("house luxury balcony rail", -6.0f, 8.2f, floor + 4.12f, 10.8f,
        0.12f, 0.14f, estate.trim_finish);
    add("house luxury entrance light lens", -9.3f, 6.25f, floor + 2.15f,
        0.20f, 0.34f, 0.14f, StartFinish::Yellow);
    add("house luxury entrance light lens", -2.7f, 6.25f, floor + 2.15f,
        0.20f, 0.34f, 0.14f, StartFinish::Yellow);
    const float address_y = luxury_height(site, ground, -14.0f, 25.0f);
    add("house luxury address plinth", -14.0f, 25.0f, address_y, 2.4f,
        1.25f, 0.65f, StartFinish::Brick, true);
    add("house luxury address plaque", -14.0f, 25.37f, address_y + 0.68f,
        1.55f, 0.42f, 0.08f, StartFinish::Steel);
    const int address_studs = 1 + 2 * static_cast<int>(index % 3u);
    for (int stud = 0; stud < address_studs; ++stud) {
        const float offset = (static_cast<float>(stud) -
                              static_cast<float>(address_studs - 1) * 0.5f) *
                             0.24f;
        add("house luxury address stud", -14.0f + offset, 25.43f,
            address_y + 0.78f, 0.10f, 0.22f, 0.06f, StartFinish::Yellow);
    }
    // Walk tiles still follow the real bluff. The driveway itself is emitted
    // as one continuous triangle ribbon by bake_luxury_driveway().
    for (int step = 0; step < 46; ++step) {
        const float z = -10.5f + static_cast<float>(step);
        if (z >= 8.5f) {
            const float walk_y = luxury_height(site, ground, -6.0f, z);
            add("house luxury front walk", -6.0f, z, walk_y + 0.04f - 0.10f,
                2.4f, 0.10f, 1.02f, StartFinish::Concrete);
        }
    }

    if (estate.pool) {
        float pool_top = 0.0f;
        for (float x = -12.0f; x <= 2.0f; x += 1.0f) {
            for (float z = -28.0f; z <= -18.0f; z += 1.0f) {
                pool_top = std::max(pool_top, luxury_height(site, ground, x, z));
            }
        }
        pool_top += 0.10f;
        add("house luxury pool terrace", -5.0f, -23.0f, 0.0f, 20.0f,
            pool_top, 15.0f, StartFinish::Concrete, true);
        add("house luxury pool water", -5.0f, -23.0f, pool_top + 0.02f,
            14.0f, 0.05f, 8.5f, StartFinish::PoolWater);
        for (float x : {-13.6f, 3.6f}) {
            add("house luxury pool coping", x, -23.0f, pool_top, 1.0f,
                0.14f, 10.5f, StartFinish::White);
        }
        for (float z : {-28.0f, -18.0f}) {
            add("house luxury pool coping", -5.0f, z, pool_top, 18.2f,
                0.14f, 1.0f, StartFinish::White);
        }

        // A broad four-metre gate faces the garden door. Thin rails keep the
        // pool enclosure readable without turning the yard into a cage.
        for (const glm::vec2 post : std::array<glm::vec2, 6>{{
                 {-14.0f, -30.0f}, {4.0f, -30.0f}, {-14.0f, -16.0f},
                 {-7.0f, -16.0f}, {-3.0f, -16.0f}, {4.0f, -16.0f}}}) {
            add("house luxury pool fence post", post.x, post.y,
                pool_top + 0.14f, 0.16f, 1.25f, 0.16f,
                StartFinish::Steel, true);
        }
        for (float lift : {0.48f, 1.08f}) {
            add("house luxury pool fence rail", -5.0f, -30.0f,
                pool_top + lift, 18.0f, 0.10f, 0.10f,
                StartFinish::Steel, true);
            for (float x : {-14.0f, 4.0f}) {
                add("house luxury pool fence rail", x, -23.0f,
                    pool_top + lift, 0.10f, 0.10f, 14.0f,
                    StartFinish::Steel, true);
            }
            for (float x : {-10.5f, 0.5f}) {
                add("house luxury pool fence rail", x, -16.0f,
                    pool_top + lift, 7.0f, 0.10f, 0.10f,
                    StartFinish::Steel, true);
            }
        }
    }

    // Low stone boundaries, clipped at the front for the walk and drive.
    const auto wall = [&](float x, float z, float width, float depth) {
        add("house luxury boundary wall", x, z,
            luxury_height(site, ground, x, z), width, 0.75f, depth,
            estate.wall_finish, true);
    };
    wall(0.0f, -33.0f, 61.0f, 0.35f);
    wall(-30.0f, -6.0f, 0.35f, 54.0f);
    wall(30.0f, -6.0f, 0.35f, 54.0f);
    wall(-20.0f, 30.5f, 19.0f, 0.35f);
    wall(3.0f, 30.5f, 13.0f, 0.35f);
    wall(26.5f, 30.5f, 6.0f, 0.35f);

    // Four deliberately placed mature trees per estate replace random forest
    // scatter and keep every driveway and front door readable.
    for (const glm::vec2 tree : std::array<glm::vec2, 4>{{
             {-25.0f, 20.0f}, {-25.0f, -24.0f}, {25.0f, -24.0f}, {27.0f, 19.0f}}}) {
        if (tree.x > 10.0f && tree.y > 8.0f) continue;
        const float y = luxury_height(site, ground, tree.x, tree.y);
        add("luxury tree trunk", tree.x, tree.y, y, 0.65f, 4.8f, 0.65f,
            StartFinish::WarmWall, true);
        add("luxury tree crown", tree.x, tree.y, y + 4.0f, 4.8f, 4.6f,
            4.8f, StartFinish::TealDoor);
    }
    return out;
}

inline std::vector<StartPart> bake_westmere_court_green(
    std::size_t index, GroundSampler ground) {
    const auto& site = kWestmereCourtGreens.at(index);
    std::vector<StartPart> out;
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false,
                         float yaw = 0.0f) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid, 0.0f, yaw});
    };
    const auto at = [&](float x, float z) {
        return luxury_height(site, ground, x, z);
    };

    // The bulb is a real planted island, not an empty grass hole. A loose
    // shrub ring leaves sight lines through the court while mature trees make
    // each turnaround legible from the neighborhood spine.
    for (const glm::vec2 shrub : std::array<glm::vec2, 12>{{
             {0, -11}, {5.5f, -9.5f}, {9.5f, -5.5f}, {11, 0},
             {9.5f, 5.5f}, {5.5f, 9.5f}, {0, 11}, {-5.5f, 9.5f},
             {-9.5f, 5.5f}, {-11, 0}, {-9.5f, -5.5f}, {-5.5f, -9.5f}}}) {
        add("westmere court shrub", shrub.x, shrub.y,
            at(shrub.x, shrub.y), 3.0f, 0.8f, 1.7f,
            StartFinish::TealDoor);
    }
    const std::array<std::array<glm::vec2, 4>, 3> tree_layouts{{
        {{{-5.0f, -3.0f}, {5.5f, -2.0f}, {-2.0f, 5.0f}, {5.0f, 6.0f}}},
        {{{-6.0f, -5.0f}, {4.0f, -6.0f}, {-5.0f, 5.5f}, {5.5f, 4.0f}}},
        {{{-4.0f, -6.0f}, {6.0f, -4.0f}, {-6.0f, 4.0f}, {4.0f, 6.0f}}},
    }};
    for (const glm::vec2 tree : tree_layouts[index]) {
        const float y = at(tree.x, tree.y);
        add("luxury tree trunk", tree.x, tree.y, y, 0.6f, 4.7f, 0.6f,
            StartFinish::WarmWall, true);
        add("luxury tree crown", tree.x, tree.y, y + 3.9f, 4.8f, 4.5f,
            4.8f, StartFinish::TealDoor);
    }
    for (float z : {-15.0f, 15.0f}) {
        const float y = at(0.0f, z);
        add("westmere court bench", 0.0f, z, y + 0.45f, 3.2f, 0.14f,
            0.8f, StartFinish::WarmWall, true);
        add("westmere court bench back", 0.0f, z + (z < 0.0f ? -0.3f : 0.3f),
            y + 0.58f, 3.2f, 0.55f, 0.14f, StartFinish::WarmWall);
    }
    for (float x : {-15.0f, 15.0f}) {
        const float y = at(x, 0.0f);
        add("luxury light pole", x, 0.0f, y, 0.22f, 4.0f, 0.22f,
            StartFinish::Steel, true);
        add("luxury light lens", x, 0.0f, y + 3.95f, 0.52f, 0.28f,
            0.52f, StartFinish::Yellow);
    }
    return out;
}

inline std::vector<StartPart> bake_westmere_common(GroundSampler ground) {
    const auto& site = kWestmereCommonSite;
    std::vector<StartPart> out;
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish, solid});
    };
    const auto pad_top = [&](float cx, float cz, float width, float depth) {
        float top = 0.0f;
        for (float x = cx - width * 0.5f; x <= cx + width * 0.5f; x += 1.0f) {
            for (float z = cz - depth * 0.5f; z <= cz + depth * 0.5f; z += 1.0f) {
                top = std::max(top, luxury_height(site, ground, x, z));
            }
        }
        return top + 0.08f;
    };

    const float pool = pad_top(-22.0f, -3.0f, 38.0f, 20.0f);
    add("westmere pool terrace", -22.0f, -3.0f, 0.0f, 42.0f, pool,
        24.0f, StartFinish::Concrete, true);
    add("westmere community pool", -22.0f, -3.0f, pool + 0.02f, 31.0f,
        0.05f, 12.0f, StartFinish::PoolWater);
    for (float x : {-38.0f, -6.0f})
        add("westmere pool coping", x, -3.0f, pool + 0.07f, 1.0f,
            0.14f, 14.0f, StartFinish::White);
    for (float z : {-9.5f, 3.5f})
        add("westmere pool coping", -22.0f, z, pool + 0.07f, 33.0f,
            0.14f, 1.0f, StartFinish::White);
    for(float z:{-6.0f,-3.0f,0.0f})
        add("westmere pool lane marker",-22.0f,z,pool+0.072f,29.0f,
            0.025f,0.08f,StartFinish::White);
    for(float z:{-6.2f,0.2f}) {
        add("luxury pool ladder rail",-7.1f,z,pool+0.08f,0.10f,1.0f,
            0.10f,StartFinish::Steel);
        add("luxury pool ladder step",-7.7f,z,pool+0.35f,1.2f,0.09f,
            0.10f,StartFinish::Steel);
    }
    for(float z:{-12.0f,6.0f}) for(float x:{-35.0f,-22.0f,-9.0f}) {
        add("westmere pool lounger",x,z,pool+0.08f,2.4f,0.16f,0.75f,
            StartFinish::White);
        add("westmere pool lounger back",x,z-0.28f,pool+0.20f,2.4f,
            0.65f,0.12f,StartFinish::White);
    }
    for (const glm::vec2 post : std::array<glm::vec2, 6>{{
             {-39.0f, -10.5f}, {-5.0f, -10.5f}, {-39.0f, 4.5f},
             {-5.0f, 4.5f}, {-5.0f, -1.0f}, {-5.0f, 3.0f}}}) {
        add("westmere pool fence post", post.x, post.y, pool + 0.08f,
            0.18f, 1.35f, 0.18f, StartFinish::Steel, true);
    }
    for (float lift : {0.50f, 1.15f}) {
        add("westmere pool fence rail", -22.0f, -10.5f, pool + lift,
            34.0f, 0.10f, 0.10f, StartFinish::Steel, true);
        add("westmere pool fence rail", -39.0f, -3.0f, pool + lift,
            0.10f, 0.10f, 15.0f, StartFinish::Steel, true);
        add("westmere pool fence rail", -22.0f, 4.5f, pool + lift,
            34.0f, 0.10f, 0.10f, StartFinish::Steel, true);
        add("westmere pool fence rail", -5.0f, -5.75f, pool + lift,
            0.10f, 0.10f, 9.5f, StartFinish::Steel, true);
        add("westmere pool fence rail", -5.0f, 3.75f, pool + lift,
            0.10f, 0.10f, 1.5f, StartFinish::Steel, true);
    }

    const float court = pad_top(31.0f, 17.0f, 44.0f, 22.0f);
    add("westmere tennis court", 31.0f, 17.0f, 0.0f, 44.0f, court,
        22.0f, StartFinish::TealDoor, true);
    add("westmere tennis surface", 31.0f, 17.0f, court + 0.01f, 43.6f,
        0.02f, 21.6f, StartFinish::TealDoor);
    add("westmere tennis centre line", 31.0f, 17.0f, court + 0.01f,
        0.08f, 0.025f, 20.0f, StartFinish::White);
    for (float x : {10.0f, 52.0f}) {
        add("westmere tennis baseline", x, 17.0f, court + 0.01f, 0.08f,
            0.025f, 20.0f, StartFinish::White);
    }
    add("westmere tennis net", 31.0f, 17.0f, court + 0.04f, 0.08f, 0.95f,
        20.0f, StartFinish::White);

    const float pavilion = pad_top(25.0f, -27.0f, 18.0f, 14.0f);
    add("westmere pavilion floor", 25.0f, -27.0f, 0.0f, 18.0f, pavilion,
        14.0f, StartFinish::Concrete, true);
    for (float x : {18.0f, 32.0f}) {
        for (float z : {-32.0f, -22.0f}) {
            add("westmere pavilion column", x, z, pavilion, 0.35f, 3.3f,
                0.35f, StartFinish::White, true);
        }
    }
    add("westmere pavilion roof", 25.0f, -27.0f, pavilion + 3.3f, 19.5f,
        0.28f, 15.5f, StartFinish::DarkRoof);
    add("westmere clubhouse rear wall", 25.0f, -31.78f, pavilion, 13.5f,
        2.5f, 0.28f, StartFinish::WarmWall, true);
    add("westmere clubhouse service hatch", 25.0f, -31.60f,
        pavilion + 1.05f, 5.8f, 1.05f, 0.08f, StartFinish::Glass);
    add("westmere clubhouse counter base", 25.0f, -30.85f, pavilion, 7.0f,
        1.02f, 0.65f, StartFinish::Brick, true);
    add("westmere clubhouse counter cap", 25.0f, -30.85f,
        pavilion + 1.02f, 7.5f, 0.16f, 0.90f, StartFinish::White, true);
    for (float z : {-34.82f, -19.18f}) {
        add("westmere clubhouse roof fascia", 25.0f, z, pavilion + 3.16f,
            19.8f, 0.48f, 0.22f, StartFinish::White);
    }
    for (float x : {15.18f, 34.82f}) {
        add("westmere clubhouse roof fascia", x, -27.0f, pavilion + 3.16f,
            0.22f, 0.48f, 15.8f, StartFinish::White);
    }
    for (float z : {-28.75f, -25.25f}) {
        add("westmere clubhouse roof lantern panel", 25.0f, z,
            pavilion + 3.58f, 4.2f, 1.0f, 0.10f, StartFinish::Glass);
    }
    for (float x : {22.9f, 27.1f}) {
        add("westmere clubhouse roof lantern panel", x, -27.0f,
            pavilion + 3.58f, 0.10f, 1.0f, 3.5f, StartFinish::Glass);
    }
    add("westmere clubhouse roof lantern cap", 25.0f, -27.0f,
        pavilion + 4.58f, 5.0f, 0.25f, 4.3f, StartFinish::DarkRoof);
    for (float x : {21.0f, 29.0f}) {
        add("westmere garden bench", x, -27.0f, pavilion + 0.45f, 3.0f,
            0.14f, 0.75f, StartFinish::WarmWall, true);
        add("westmere garden bench back", x, -27.3f, pavilion + 0.58f,
            3.0f, 0.55f, 0.14f, StartFinish::WarmWall);
    }

    // A supported T-walk reaches the street, pool, tennis court and pavilion
    // without laying concrete through the water as the old cross-walk did.
    for (int step = -25; step <= 25; ++step) {
        const float z = static_cast<float>(step) * 2.0f;
        const float y = luxury_height(site, ground, 0.0f, z);
        add("westmere common walk", 0.0f, z, y + 0.04f - 0.10f, 2.8f,
            0.10f, 2.02f, StartFinish::Concrete);
    }
    for (int step = -28; step <= 0; ++step) {
        const float x = static_cast<float>(step) * 2.0f;
        const float y = luxury_height(site, ground, x, 8.0f);
        add("westmere common walk", x, 8.0f, y + 0.04f - 0.10f, 2.02f,
            0.10f, 2.8f, StartFinish::Concrete);
    }
    for (const glm::vec2 tree : std::array<glm::vec2, 10>{{
             {-55, -38}, {-45, 35}, {-28, 42}, {-4, 39}, {10, 36},
             {52, 40}, {57, -5}, {51, -42}, {5, -44}, {-48, -8}}}) {
        const float y = luxury_height(site, ground, tree.x, tree.y);
        add("luxury tree trunk", tree.x, tree.y, y, 0.7f, 5.4f, 0.7f,
            StartFinish::WarmWall, true);
        add("luxury tree crown", tree.x, tree.y, y + 4.4f, 5.4f, 5.0f,
            5.4f, StartFinish::TealDoor);
    }
    for (float x : {-55.0f, -5.0f, 48.0f}) {
        const float y = luxury_height(site, ground, x, 3.0f);
        add("luxury light pole", x, 3.0f, y, 0.22f, 4.2f, 0.22f,
            StartFinish::Steel, true);
        add("luxury light lens", x, 3.0f, y + 4.15f, 0.55f, 0.28f,
            0.55f, StartFinish::Yellow);
    }
    return out;
}

inline std::vector<StartPart> bake_westmere_gate(GroundSampler ground) {
    const auto& site = kWestmereGateSite;
    std::vector<StartPart> out;
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish, solid});
    };
    for (float x : {-8.6f, 8.6f}) {
        const float y = luxury_height(site, ground, x, 0.0f);
        add("westmere gate pier", x, 0.0f, y, 2.3f, 4.4f, 2.3f,
            StartFinish::Brick, true);
        add("westmere gate pier cap", x, 0.0f, y + 4.4f, 2.8f, 0.32f,
            2.8f, StartFinish::White);
        add("luxury light lens", x, 0.0f, y + 4.78f, 0.65f, 0.32f,
            0.65f, StartFinish::Yellow);
    }
    const float guard = luxury_height(site, ground, 13.5f, -5.5f);
    add("westmere gatehouse", 13.5f, -5.5f, guard, 7.0f, 3.1f, 8.0f,
        StartFinish::WarmWall, true);
    add("westmere gatehouse roof", 13.5f, -5.5f, guard + 3.1f, 8.2f,
        0.28f, 9.2f, StartFinish::DarkRoof);
    add("westmere gatehouse window", 9.94f, -5.5f, guard + 1.0f, 0.08f,
        1.45f, 3.8f, StartFinish::Glass);
    for (int step = -6; step <= 6; ++step) {
        const float z = static_cast<float>(step) * 2.0f;
        const float y = luxury_height(site, ground, 0.0f, z);
        add("westmere gate walk", 0.0f, z, y + 0.04f - 0.10f, 2.8f,
            0.10f, 2.02f, StartFinish::Concrete);
    }
    return out;
}

inline bool luxury_neighborhood_contains(float world_x, float world_z,
                                          float margin = 0.0f) {
    const float ex = (world_x + 835.0f) / (315.0f + margin);
    const float ez = (world_z + 610.0f) / (250.0f + margin);
    if (ex * ex + ez * ez <= 1.0f) return true;
    // Keep the gated approach off Marsh Road clear of random forest scatter.
    const glm::vec2 a{-560.0f, -340.0f};
    const glm::vec2 b{-680.0f, -570.0f};
    const glm::vec2 p{world_x, world_z};
    const glm::vec2 d = b - a;
    const float t = std::clamp(glm::dot(p - a, d) / glm::dot(d, d), 0.0f, 1.0f);
    return glm::length(p - (a + d * t)) <= 28.0f + margin;
}

}  // namespace apricot::city
