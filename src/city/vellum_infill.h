#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "city/start_area.h"

namespace apricot::city {

// Small finished buildings for the empty west and south edges of Vellum Row.
// They deliberately stay non-enterable: the street doors are solid pieces,
// while the shallow lobby slabs give the facade a believable threshold and a
// stable support surface. World can render every returned piece and use the
// same `solid` records for collision, so the visible massing cannot drift away
// from the blocking geometry.
enum class VellumInfillRoofline : uint8_t {
    FlatParapet,
    Stepped,
    Gabled,
};

struct VellumInfillParcel {
    StartSite site;
    int floors = 4;
    int setback_floors = 0;
    int facade_bays = 4;
    float building_width_m = 40.0f;
    float building_depth_m = 27.0f;
    float building_centre_z_m = 1.0f;
    StartFinish facade_finish = StartFinish::Brick;
    StartFinish side_finish = StartFinish::WarmWall;
    StartFinish trim_finish = StartFinish::TealDoor;
    VellumInfillRoofline roofline = VellumInfillRoofline::FlatParapet;
};

constexpr Vec2 vellum_infill_grid_point(float east, float south) {
    return {70.0f + kGridCos * east + kGridSin * south,
            -40.0f - kGridSin * east + kGridCos * south};
}

// Twelve occupied cells form an L around the genuinely vacant west and south
// edges plus two holes in the older central fabric. The skipped west-column cells are intentional:
// (-322,-31) is the steel-frame annex and (-322,155) is the demolition site.
// Every lot remains within the standard 92 x 62 m Vellum block.
inline constexpr std::array<VellumInfillParcel, 12> kVellumInfillParcels{{
    {{"Briar Needleworks", vellum_infill_grid_point(-322.0f, -279.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     4, 0, 4, 42.0f, 27.0f, 0.5f, StartFinish::WarmWall,
     StartFinish::Brick, StartFinish::TealDoor,
     VellumInfillRoofline::Gabled},
    {{"Briar Cold Storage", vellum_infill_grid_point(-322.0f, -217.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     6, 0, 5, 46.0f, 29.0f, 1.0f, StartFinish::Brick,
     StartFinish::Concrete, StartFinish::Steel,
     VellumInfillRoofline::FlatParapet},
    {{"West Vellum Mercantile", vellum_infill_grid_point(-322.0f, -155.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     8, 2, 4, 40.0f, 30.0f, 1.0f, StartFinish::WarmWall,
     StartFinish::Brick, StartFinish::RedTrim,
     VellumInfillRoofline::Stepped},
    {{"Foundry Court Apartments", vellum_infill_grid_point(-322.0f, -93.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     5, 0, 5, 45.0f, 26.0f, 0.5f, StartFinish::Brick,
     StartFinish::WarmWall, StartFinish::White,
     VellumInfillRoofline::FlatParapet},
    {{"Copperleaf House", vellum_infill_grid_point(-322.0f, 31.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 30.0f, 12.0f, 1500.0f},
     7, 1, 4, 38.0f, 22.0f, 0.0f, StartFinish::WarmWall,
     StartFinish::Concrete, StartFinish::TealDoor,
     VellumInfillRoofline::Stepped},
    {{"Vellum Printworks", vellum_infill_grid_point(-322.0f, 93.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     9, 0, 5, 48.0f, 29.0f, 1.0f, StartFinish::Brick,
     StartFinish::Steel, StartFinish::Yellow,
     VellumInfillRoofline::FlatParapet},
    {{"Mercer Arcade", vellum_infill_grid_point(-230.0f, 155.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     6, 0, 4, 44.0f, 28.0f, 0.5f, StartFinish::WarmWall,
     StartFinish::Brick, StartFinish::RedTrim,
     VellumInfillRoofline::Gabled},
    {{"Bellweather Rooms", vellum_infill_grid_point(-138.0f, 155.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     4, 0, 3, 40.0f, 25.0f, 0.5f, StartFinish::Brick,
     StartFinish::WarmWall, StartFinish::White,
     VellumInfillRoofline::Gabled},
    {{"Rookery House", vellum_infill_grid_point(-46.0f, 155.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     8, 2, 5, 46.0f, 30.0f, 1.0f, StartFinish::Concrete,
     StartFinish::Brick, StartFinish::TealDoor,
     VellumInfillRoofline::Stepped},
    {{"Juniper Market Flats", vellum_infill_grid_point(46.0f, 155.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     7, 0, 4, 42.0f, 27.0f, 0.5f, StartFinish::WarmWall,
     StartFinish::Brick, StartFinish::RedTrim,
     VellumInfillRoofline::FlatParapet},
    {{"Mercer Textile Exchange", vellum_infill_grid_point(-230.0f, -155.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     6, 1, 5, 46.0f, 29.0f, 1.0f, StartFinish::Brick,
     StartFinish::Concrete, StartFinish::Yellow,
     VellumInfillRoofline::Stepped},
    {{"Bellweather Pharmacy Offices", vellum_infill_grid_point(-230.0f, 93.0f),
      kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1500.0f},
     5, 0, 4, 43.0f, 27.0f, 0.5f, StartFinish::WarmWall,
     StartFinish::Brick, StartFinish::White,
     VellumInfillRoofline::Gabled},
}};

inline constexpr float kVellumInfillFloorHeightM = 3.15f;

inline float vellum_infill_eave_height(const VellumInfillParcel& parcel) {
    return static_cast<float>(parcel.floors) * kVellumInfillFloorHeightM;
}

inline bool vellum_infill_ground_piece(const StartPart& part) {
    return part.name != nullptr &&
           (std::strcmp(part.name, "infill lot") == 0 ||
            std::strcmp(part.name, "infill entrance walk") == 0 ||
            std::strcmp(part.name, "infill lobby floor") == 0);
}

inline std::vector<StartPart> bake_vellum_infill(std::size_t index) {
    const auto& parcel = kVellumInfillParcels.at(index);
    const float width = parcel.building_width_m;
    const float depth = parcel.building_depth_m;
    const float centre_z = parcel.building_centre_z_m;
    const float half_width = width * 0.5f;
    const float half_depth = depth * 0.5f;
    const float front_z = centre_z - half_depth;
    const float rear_z = centre_z + half_depth;
    const int main_floors = parcel.floors - parcel.setback_floors;
    const float main_top =
        static_cast<float>(main_floors) * kVellumInfillFloorHeightM;
    const float total_top = vellum_infill_eave_height(parcel);
    const bool stepped = parcel.roofline == VellumInfillRoofline::Stepped;
    const bool gabled = parcel.roofline == VellumInfillRoofline::Gabled;
    const float upper_width = stepped ? width - 6.0f : width;
    const float upper_depth = stepped ? depth - 5.0f : depth;
    const float upper_centre_z = stepped ? centre_z + 1.0f : centre_z;

    const BuildingWall walls[] = {
        {"infill front wall", {-half_width, front_z},
         {half_width, front_z}, 0.20f, main_top - 0.20f, 0.30f,
         parcel.facade_finish},
        {"infill east wall", {half_width, front_z},
         {half_width, rear_z}, 0.20f, main_top - 0.20f, 0.30f,
         parcel.side_finish},
        {"infill rear wall", {half_width, rear_z},
         {-half_width, rear_z}, 0.20f, main_top - 0.20f, 0.30f,
         parcel.side_finish},
        {"infill west wall", {-half_width, rear_z},
         {-half_width, front_z}, 0.20f, main_top - 0.20f, 0.30f,
         parcel.side_finish},
    };

    const float roof_rise = gabled ? 2.0f +
        static_cast<float>(index % 2u) * 0.35f : 0.0f;
    const BuildingRoof roof{
        gabled ? "infill gabled roof" : "infill parapet roof",
        {0.0f, upper_centre_z}, total_top, upper_width, upper_depth,
        roof_rise, 0.28f, gabled ? 0.45f : 0.25f,
        gabled ? 0.0f : 0.60f + static_cast<float>(index % 3u) * 0.12f,
        gabled ? RoofStyle::Gable : RoofStyle::Flat,
        index % 2u == 0u ? RidgeAxis::AlongX : RidgeAxis::AlongZ,
        StartFinish::DarkRoof, parcel.trim_finish, 0.0f,
        parcel.side_finish};
    const BuildingPlan plan{"infill parcel shell", walls, 4u, &roof, 1u};

    std::vector<StartPart> out;
    out.reserve(150u);
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float part_width, float height, float part_depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, part_width, height, part_depth,
                       finish, solid});
    };

    add("infill lot", 0.0f, 0.0f, 0.0f, parcel.site.lot_width_m, 0.10f,
        parcel.site.lot_depth_m,
        index % 3u == 0u ? StartFinish::Concrete : StartFinish::Asphalt);
    const float walk_front = -parcel.site.lot_depth_m * 0.5f + 0.25f;
    const float walk_rear = front_z - 0.20f;
    add("infill entrance walk", 0.0f, (walk_front + walk_rear) * 0.5f,
        0.10f, 4.4f, 0.10f, walk_rear - walk_front,
        StartFinish::Concrete);
    add("infill lobby floor", 0.0f, front_z + 1.15f, 0.10f, 4.2f, 0.12f,
        2.0f, index % 2u == 0u ? StartFinish::WarmWall
                               : StartFinish::Concrete);

    auto shell = bake_building(plan);
    out.insert(out.end(), shell.begin(), shell.end());

    // A stepped parcel has a smaller solid upper mass on a visible terrace.
    // This is still a StartPart, so its collision and its rendered silhouette
    // remain one record.
    if (stepped) {
        add("infill setback terrace", 0.0f, centre_z, main_top - 0.04f,
            width + 0.40f, 0.22f, depth + 0.40f, StartFinish::Concrete);
        add("infill upper setback mass", 0.0f, upper_centre_z, main_top,
            upper_width,
            static_cast<float>(parcel.setback_floors) *
                kVellumInfillFloorHeightM,
            upper_depth, parcel.side_finish, true);
        add("infill upper facade field", 0.0f,
            upper_centre_z - upper_depth * 0.5f - 0.07f, main_top,
            upper_width - 0.50f,
            static_cast<float>(parcel.setback_floors) *
                kVellumInfillFloorHeightM,
            0.10f, parcel.facade_finish);
    }

    // Street level: recessed-looking glazing, a sealed door, an awning, and
    // small sidewalk-scale fixtures. The wall behind the glass remains solid,
    // making these finished background buildings intentionally non-enterable.
    add("infill storefront kickplate", 0.0f, front_z - 0.18f, 0.20f,
        width - 1.0f, 0.52f, 0.10f, parcel.trim_finish);
    const int panes_per_side = parcel.facade_bays >= 5 ? 3 : 2;
    const float side_span = half_width - 3.1f;
    const float pane_width = (side_span - 0.70f) /
                             static_cast<float>(panes_per_side);
    for (float side : {-1.0f, 1.0f}) {
        for (int pane = 0; pane < panes_per_side; ++pane) {
            const float distance = 3.1f +
                (static_cast<float>(pane) + 0.5f) * pane_width;
            add("infill shopfront glass", side * distance,
                front_z - 0.205f, 0.72f, pane_width - 0.18f, 2.28f, 0.055f,
                StartFinish::Glass);
            add("infill shopfront mullion",
                side * (3.1f + static_cast<float>(pane) * pane_width),
                front_z - 0.24f, 0.68f, 0.10f, 2.40f, 0.10f,
                parcel.trim_finish);
        }
    }
    add("infill entrance door", 0.0f, front_z - 0.25f, 0.20f, 2.25f,
        2.82f, 0.16f, StartFinish::Glass, true);
    for (float side : {-1.0f, 1.0f})
        add("infill entrance jamb", side * 1.20f, front_z - 0.27f, 0.20f,
            0.15f, 3.0f, 0.18f, parcel.trim_finish);
    add("infill entrance head", 0.0f, front_z - 0.27f, 3.02f, 2.55f,
        0.18f, 0.18f, parcel.trim_finish);
    add("infill storefront header", 0.0f, front_z - 0.20f, 3.15f,
        width - 0.70f, 0.32f, 0.14f, parcel.trim_finish);
    add("infill shop sign panel", 0.0f, front_z - 0.30f, 3.50f,
        9.0f + static_cast<float>(index % 3u) * 1.5f, 0.82f, 0.10f,
        index % 2u == 0u ? StartFinish::White : StartFinish::Yellow);
    add("infill entrance canopy", 0.0f, front_z - 1.20f, 2.82f, 5.2f,
        0.18f, 2.1f, parcel.trim_finish);
    for (float side : {-1.0f, 1.0f}) {
        add("infill canopy tie", side * 2.15f, front_z - 0.72f, 2.15f,
            0.10f, 0.70f, 0.10f, StartFinish::Steel);
        add("infill entry planter", side * 3.75f, front_z - 2.0f, 0.10f,
            1.35f, 0.58f, 1.25f, StartFinish::Concrete, true);
        add("infill entry lamp", side * 1.60f, front_z - 0.33f, 2.22f,
            0.16f, 0.32f, 0.13f, StartFinish::Yellow);
    }

    // Upper floors use a real repeated bay rhythm rather than a single giant
    // glass slab. Setback floors switch to their smaller face automatically.
    for (int floor = 1; floor < parcel.floors; ++floor) {
        const bool upper = stepped && floor >= main_floors;
        const float floor_width = upper ? upper_width : width;
        const float floor_depth = upper ? upper_depth : depth;
        const float floor_centre_z = upper ? upper_centre_z : centre_z;
        const float floor_front = floor_centre_z - floor_depth * 0.5f;
        const float floor_rear = floor_centre_z + floor_depth * 0.5f;
        const float sill_y = static_cast<float>(floor) *
                                 kVellumInfillFloorHeightM +
                             0.72f;
        const float usable_width = floor_width - 4.0f;
        const float bay_width = usable_width /
                                static_cast<float>(parcel.facade_bays);
        for (int bay = 0; bay < parcel.facade_bays; ++bay) {
            const float x = -usable_width * 0.5f +
                (static_cast<float>(bay) + 0.5f) * bay_width;
            add("infill apartment window", x, floor_front - 0.18f, sill_y,
                std::min(2.65f, bay_width - 0.45f), 1.48f, 0.055f,
                StartFinish::Glass);
        }
        add("infill facade floor band", 0.0f, floor_front - 0.20f,
            static_cast<float>(floor) * kVellumInfillFloorHeightM + 0.42f,
            floor_width + 0.20f, 0.14f, 0.16f, parcel.trim_finish);
        for (float side : {-1.0f, 1.0f})
            add("infill side apartment window",
                side * (floor_width * 0.5f + 0.18f), floor_centre_z, sill_y,
                0.055f, 1.42f, std::min(2.5f, floor_depth - 4.0f),
                StartFinish::Glass);
        if (floor % 2 == 0) {
            for (float side : {-1.0f, 1.0f})
                add("infill rear apartment window", side * floor_width * 0.22f,
                    floor_rear + 0.18f, sill_y, 2.15f, 1.42f, 0.055f,
                    StartFinish::Glass);
        }
    }

    // Rear alleys get service access and utility clutter, kept within the lot
    // so the road and sidewalk clearances remain the parcel's true footprint.
    const float service_z = rear_z + 0.18f;
    const float service_x = -width * 0.24f;
    add("infill rear service door", service_x, service_z, 0.20f, 2.7f,
        2.75f, 0.16f, StartFinish::Steel, true);
    add("infill rear loading step", service_x, rear_z + 0.82f, 0.10f, 4.0f,
        0.28f, 1.45f, StartFinish::Concrete, true);
    add("infill rear service canopy", service_x, rear_z + 0.72f, 3.0f,
        4.6f, 0.20f, 1.5f, parcel.trim_finish);
    add("infill rear utility cabinet", width * 0.25f, rear_z + 0.62f, 0.12f,
        1.5f, 1.8f, 0.80f, StartFinish::Steel, true);
    for (float side : {-1.0f, 1.0f}) {
        add("infill rear refuse bin", width * 0.25f + side * 1.2f,
            rear_z + 1.02f, 0.12f, 0.92f, 1.08f, 1.08f,
            index % 2u == 0u ? StartFinish::DarkRoof : StartFinish::Steel,
            true);
        add("infill rear downpipe", side * (half_width - 0.65f),
            rear_z + 0.22f, 0.20f, 0.12f, main_top - 0.55f, 0.12f,
            StartFinish::Steel);
    }
    if (parcel.floors >= 6) {
        add("infill rear fire escape landing", width * 0.22f,
            rear_z + 0.70f, 6.42f, 4.0f, 0.20f, 1.25f,
            StartFinish::Steel, true);
        for (float side : {-1.0f, 1.0f})
            add("infill rear fire escape rail", width * 0.22f + side * 1.9f,
                rear_z + 0.70f, 6.62f, 0.10f, 1.05f, 1.20f,
                StartFinish::Steel);
        add("infill rear fire escape ladder", width * 0.22f + 1.85f,
            rear_z + 0.66f, 0.35f, 0.12f, 6.05f, 0.12f,
            StartFinish::Steel);
    }

    // Three rooftop silhouettes repeat across the row without duplicating one
    // prefab: water tank, paired HVAC, or a compact stair/vent house.
    if (index % 3u == 0u) {
        add("infill rooftop tank base", -upper_width * 0.20f,
            upper_centre_z + 1.0f, total_top + 0.28f, 3.4f, 0.45f, 3.4f,
            StartFinish::Steel, true);
        add("infill rooftop water tank", -upper_width * 0.20f,
            upper_centre_z + 1.0f, total_top + 0.73f, 2.7f, 2.4f, 2.7f,
            StartFinish::DarkRoof, true);
        add("infill rooftop tank cap", -upper_width * 0.20f,
            upper_centre_z + 1.0f, total_top + 3.13f, 3.0f, 0.20f, 3.0f,
            parcel.trim_finish);
    } else if (index % 3u == 1u) {
        for (float side : {-1.0f, 1.0f}) {
            add("infill rooftop hvac", side * 4.0f, upper_centre_z + 1.0f,
                total_top + 0.28f, 3.0f, 1.25f, 2.3f,
                StartFinish::Steel, true);
            add("infill rooftop hvac grille", side * 4.0f,
                upper_centre_z - 0.18f, total_top + 0.55f, 2.45f, 0.72f,
                0.055f, StartFinish::DarkRoof);
        }
    } else {
        add("infill rooftop stair house", 0.0f, upper_centre_z + 1.3f,
            total_top + 0.28f, 6.0f, 2.8f, 4.6f, parcel.side_finish, true);
        add("infill rooftop stair house cap", 0.0f,
            upper_centre_z + 1.3f, total_top + 3.08f, 6.4f, 0.24f, 5.0f,
            StartFinish::DarkRoof);
        add("infill rooftop vent stack", 5.0f, upper_centre_z + 1.3f,
            total_top + 0.28f, 0.7f, 2.2f, 0.7f, StartFinish::Steel, true);
    }

    if (gabled) {
        const bool along_x = roof.ridge == RidgeAxis::AlongX;
        for (float side : {-1.0f, 1.0f}) {
            StartPart end{"infill gable end wall", {0.0f, centre_z},
                          total_top, along_x ? depth : width, roof_rise, 0.30f,
                          parcel.side_finish, false};
            if (along_x) {
                end.centre.x = side * half_width;
                end.yaw_deg = 90.0f;
            } else {
                end.centre.z = centre_z + side * half_depth;
            }
            end.shape = BuildingPieceShape::GablePrism;
            out.push_back(end);
        }
    }

    return out;
}

}  // namespace apricot::city
