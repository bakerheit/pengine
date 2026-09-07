#pragma once

#include <vector>

#include "city/construction_site.h"

namespace apricot::city {

// A low-rise rental yard on the next south block from the high-rise build.
// The exact construction_grid_point(138, -279) parcel is intentionally used:
// it is clear of the construction yard at south -217 and the Northline Tower
// at east 230, south -279, while keeping the service apron on the same street
// edge as the active build.
struct ConstructionNeighborEquipment {
    StartSite site;
    float building_width_m = 0.0f;
    float building_depth_m = 0.0f;
    float building_height_m = 0.0f;
};

inline constexpr ConstructionNeighborEquipment kConstructionNeighborEquipment{
    {"Vellum Equipment & Site Services", construction_grid_point(138, -279),
     kGridCos, kGridSin, {0.0f, 0.0f}, 52.0f, 30.0f, 12.0f, 1200.0f},
    20.0f, 12.0f, 5.6f};

inline std::vector<StartPart> bake_construction_neighbor_equipment() {
    std::vector<StartPart> out;
    out.reserve(80);
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    // Lot, truck apron and the public walk. The south edge remains open for
    // a delivery van to swing in from the street.
    add("equipment rental lot", 0.0f, 0.0f, 0.0f, 52.0f, 0.10f, 30.0f,
        StartFinish::Asphalt);
    add("equipment service apron", -10.0f, -8.5f, 0.10f, 18.0f, 0.10f,
        8.0f, StartFinish::Concrete);
    add("equipment rental entrance walk", 8.0f, -9.0f, 0.10f, 5.0f, 0.10f,
        9.0f, StartFinish::Concrete);

    // The office is a compact shell with a real front-door gap. The paired
    // front pieces leave the centre readable without pretending the shell is
    // an inaccessible solid block.
    add("equipment office floor", 8.0f, 2.0f, 0.20f, 20.0f, 0.12f, 12.0f,
        StartFinish::Concrete, true);
    add("equipment office front west", 2.2f, -4.0f, 0.32f, 8.4f, 5.0f,
        0.24f, StartFinish::WarmWall, true);
    add("equipment office front east", 13.8f, -4.0f, 0.32f, 8.4f, 5.0f,
        0.24f, StartFinish::WarmWall, true);
    add("equipment office side west", -2.0f, 2.0f, 0.32f, 0.24f, 5.0f,
        12.0f, StartFinish::Brick, true);
    add("equipment office side east", 18.0f, 2.0f, 0.32f, 0.24f, 5.0f,
        12.0f, StartFinish::Brick, true);
    add("equipment office rear", 8.0f, 8.0f, 0.32f, 20.0f, 5.0f, 0.24f,
        StartFinish::Brick, true);
    add("equipment office roof", 8.0f, 2.0f, 5.32f, 20.4f, 0.28f, 12.4f,
        StartFinish::DarkRoof, true);
    add("equipment office door", 8.0f, -4.14f, 0.45f, 2.8f, 3.15f, 0.08f,
        StartFinish::TealDoor);
    add("equipment office window west", 0.2f, -4.14f, 1.30f, 2.6f, 2.0f,
        0.06f, StartFinish::Glass);
    add("equipment office window east", 15.8f, -4.14f, 1.30f, 2.6f, 2.0f,
        0.06f, StartFinish::Glass);
    add("equipment office sign band", 8.0f, -4.18f, 4.45f, 8.0f, 0.45f,
        0.10f, StartFinish::RedTrim);

    // An open-front service garage sits beside the office. It gives the yard
    // a practical place for rented lifts and compact machines to be checked.
    add("equipment garage floor", -11.0f, 2.0f, 0.20f, 10.0f, 0.12f, 12.0f,
        StartFinish::Concrete, true);
    add("equipment garage side west", -16.0f, 2.0f, 0.32f, 0.24f, 5.0f,
        12.0f, StartFinish::Brick, true);
    add("equipment garage side east", -6.0f, 2.0f, 0.32f, 0.24f, 5.0f,
        12.0f, StartFinish::Brick, true);
    add("equipment garage rear", -11.0f, 8.0f, 0.32f, 10.0f, 5.0f, 0.24f,
        StartFinish::Brick, true);
    add("equipment garage roof", -11.0f, 2.0f, 5.32f, 10.4f, 0.28f, 12.4f,
        StartFinish::DarkRoof, true);
    add("equipment garage lintel", -11.0f, -3.8f, 4.55f, 10.0f, 0.75f,
        0.24f, StartFinish::Steel, true);
    add("equipment garage bay stripe west", -14.0f, -3.94f, 0.35f, 0.12f,
        4.0f, 0.06f, StartFinish::Yellow);
    add("equipment garage bay stripe east", -8.0f, -3.94f, 0.35f, 0.12f,
        4.0f, 0.06f, StartFinish::Yellow);

    // Fenced storage on the east side holds long rentals and small stock.
    // Fence pieces are visual-only so a worker can walk to the storage gate.
    add("equipment storage pad", 22.0f, 7.0f, 0.10f, 7.0f, 0.10f, 12.0f,
        StartFinish::Concrete);
    add("equipment storage fence west", 18.5f, 7.0f, 0.20f, 0.12f, 2.4f,
        12.0f, StartFinish::Steel);
    add("equipment storage fence east", 25.5f, 7.0f, 0.20f, 0.12f, 2.4f,
        12.0f, StartFinish::Steel);
    add("equipment storage fence rear", 22.0f, 13.0f, 0.20f, 7.0f, 2.4f,
        0.12f, StartFinish::Steel);
    add("equipment storage gate post west", 19.0f, 1.05f, 0.20f, 0.16f,
        2.8f, 0.16f, StartFinish::Steel, true);
    add("equipment storage gate post east", 25.0f, 1.05f, 0.20f, 0.16f,
        2.8f, 0.16f, StartFinish::Steel, true);

    // Rental-yard props: compact, grounded, and deliberately varied in scale.
    add("equipment storage pipe rack", 22.0f, 6.5f, 0.20f, 5.4f, 1.65f,
        0.75f, StartFinish::Steel, true);
    add("equipment storage pipe bundle", 22.0f, 6.5f, 1.90f, 5.1f, 0.35f,
        0.35f, StartFinish::Steel);
    add("equipment rental pallet", 20.0f, 2.4f, 0.20f, 2.2f, 0.18f, 1.8f,
        StartFinish::WarmWall, true);
    add("equipment rental generator", 22.6f, 2.4f, 0.20f, 1.7f, 1.1f,
        1.2f, StartFinish::Yellow, true);
    add("equipment compact loader", -4.2f, -8.8f, 0.20f, 2.5f, 1.55f,
        2.8f, StartFinish::Yellow, true);
    add("equipment loader bucket", -4.2f, -10.55f, 0.45f, 2.9f, 0.75f,
        0.85f, StartFinish::Steel, true);
    add("equipment rental dumpster", -20.8f, 9.6f, 0.20f, 4.0f, 1.35f,
        2.0f, StartFinish::Steel, true);
    add("equipment dumpster lid", -20.8f, 9.6f, 1.58f, 4.2f, 0.10f, 2.2f,
        StartFinish::DarkRoof);
    add("equipment safety cone", -18.0f, -8.0f, 0.20f, 0.55f, 0.85f, 0.55f,
        StartFinish::RedTrim, true);
    add("equipment safety cone", -16.2f, -8.0f, 0.20f, 0.55f, 0.85f, 0.55f,
        StartFinish::RedTrim, true);
    add("equipment yard light pole", 25.0f, -7.0f, 0.20f, 0.16f, 4.5f,
        0.16f, StartFinish::Steel, true);
    add("equipment yard light", 25.0f, -7.0f, 4.85f, 0.65f, 0.30f, 0.45f,
        StartFinish::Yellow);

    return out;
}

}  // namespace apricot::city
