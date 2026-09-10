#pragma once

#include <vector>

#include "city/construction_site.h"

namespace apricot::city {

// A compact supplier on the block just north of the active Pinatty build. It
// reads as a working materials yard without competing with the unfinished
// tower: low depot mass, a truck apron, open storage racks, and a few parking
// bays all fit inside one modest parcel.
inline constexpr StartSite kConstructionNeighborMaterialsSite{
    "Pinatty Materials Depot", construction_grid_point(138.0f, -155.0f),
    kGridCos, kGridSin, {0.0f, 0.0f}, 36.0f, 28.0f, 12.0f, 1300.0f};

inline std::vector<StartPart> bake_construction_neighbor_materials() {
    std::vector<StartPart> out;
    out.reserve(64);
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    add("materials depot lot", 0.0f, 0.0f, 0.0f, 36.0f, 0.10f, 28.0f,
        StartFinish::Asphalt);
    add("materials loading apron", 6.5f, -5.0f, 0.10f, 17.0f, 0.10f,
        7.0f, StartFinish::Concrete);
    add("materials parking strip", 8.0f, -10.8f, 0.10f, 14.0f, 0.10f,
        4.0f, StartFinish::Asphalt);

    // Small depot shell, with a real front entry gap for the parent runtime
    // to dress as a door later. The loading side stays low and readable.
    add("materials depot floor", -8.0f, 5.5f, 0.15f, 16.0f, 0.15f, 10.0f,
        StartFinish::Concrete, true);
    add("materials depot front west wall", -11.3f, 0.55f, 0.30f, 9.4f,
        4.5f, 0.26f, StartFinish::WarmWall, true);
    add("materials depot front east wall", -2.7f, 0.55f, 0.30f, 5.4f,
        4.5f, 0.26f, StartFinish::WarmWall, true);
    add("materials depot rear wall", -8.0f, 10.45f, 0.30f, 16.0f, 4.5f,
        0.26f, StartFinish::Brick, true);
    add("materials depot west wall", -15.9f, 5.5f, 0.30f, 0.26f, 4.5f,
        10.0f, StartFinish::Brick, true);
    add("materials depot east wall", -0.1f, 5.5f, 0.30f, 0.26f, 4.5f,
        10.0f, StartFinish::Brick, true);
    add("materials depot roof", -8.0f, 5.5f, 4.80f, 16.6f, 0.22f, 10.6f,
        StartFinish::DarkRoof, true);
    add("materials depot door", -5.0f, 0.38f, 0.32f, 2.1f, 3.2f, 0.10f,
        StartFinish::TealDoor);

    // A low loading dock and two open rack runs make the supplier distinct
    // from a generic shop while leaving the apron clear for a truck approach.
    add("materials loading dock", 0.5f, 0.15f, 0.30f, 5.0f, 0.55f, 1.35f,
        StartFinish::Concrete, true);
    add("materials rack west frame", 7.5f, 5.2f, 0.25f, 6.2f, 2.5f, 0.32f,
        StartFinish::Steel, true);
    add("materials rack west shelf", 7.5f, 5.2f, 1.15f, 6.2f, 0.18f, 1.5f,
        StartFinish::Steel, true);
    add("materials rack east frame", 7.5f, 8.1f, 0.25f, 6.2f, 2.5f, 0.32f,
        StartFinish::Steel, true);
    add("materials rack east shelf", 7.5f, 8.1f, 1.15f, 6.2f, 0.18f, 1.5f,
        StartFinish::Steel, true);
    add("materials rebar pallet", 7.5f, 5.2f, 1.34f, 5.3f, 0.40f, 1.15f,
        StartFinish::RedTrim);
    add("materials pipe pallet", 7.5f, 8.1f, 1.34f, 5.3f, 0.40f, 1.15f,
        StartFinish::Steel);

    // Concrete supply props sit at the rear edge, away from the public route.
    add("materials aggregate silo west", 12.8f, 11.0f, 0.20f, 2.7f, 4.2f,
        2.7f, StartFinish::Concrete, true);
    add("materials aggregate silo east", 15.8f, 11.0f, 0.20f, 2.7f, 4.2f,
        2.7f, StartFinish::Steel, true);
    add("materials cement hopper", 14.3f, 8.8f, 0.20f, 3.8f, 1.8f, 2.2f,
        StartFinish::Yellow, true);
    add("materials mixer", 3.5f, 8.8f, 0.20f, 2.4f, 1.45f, 2.0f,
        StartFinish::Yellow, true);
    add("materials pallet cement bags", 3.5f, 11.0f, 0.20f, 2.4f, 0.75f,
        1.7f, StartFinish::White, true);

    // Three short parking stops and a simple supplier sign finish the street
    // edge without turning the whole lot into visual noise.
    for (float x : {3.5f, 8.0f, 12.5f}) {
        add("materials parking stop", x, -8.85f, 0.20f, 0.22f, 0.18f, 3.0f,
            StartFinish::Concrete, true);
    }
    add("materials supplier sign", -12.0f, -12.4f, 0.20f, 4.2f, 2.0f,
        0.16f, StartFinish::RedTrim, true);
    add("materials supplier sign post", -12.0f, -12.4f, 2.20f, 0.14f, 2.2f,
        0.14f, StartFinish::Steel, true);
    return out;
}

}  // namespace apricot::city
