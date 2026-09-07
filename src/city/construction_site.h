#pragma once

#include <vector>

#include "city/start_area.h"

namespace apricot::city {

struct ConstructionSite {
    StartSite site;
    int planned_floor_count = 0;
    int built_floor_count = 0;
    float frame_width_m = 0.0f;
    float frame_depth_m = 0.0f;
};

constexpr Vec2 construction_grid_point(float east, float south) {
    return {70.0f + kGridCos * east + kGridSin * south,
            -40.0f - kGridSin * east + kGridCos * south};
}

// An active high-rise build on the east side of Vellum Row. The frame is
// intentionally open: the concrete core and steel floor bands show a real
// 70% build state instead of a finished tower with construction stickers.
inline constexpr ConstructionSite kConstructionSite{
    {"Vellum District Construction Yard", construction_grid_point(138, -217),
     kGridCos, kGridSin, {0.0f, 0.0f}, 54.0f, 38.0f, 12.0f, 1900.0f},
    20, 14, 30.0f, 22.0f};

inline constexpr float kConstructionFloorHeightM = 3.4f;

inline std::vector<StartPart> bake_construction_site() {
    std::vector<StartPart> out;
    out.reserve(180);
    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    add("construction site lot", 0.0f, 0.0f, 0.0f, 54.0f, 0.10f, 38.0f,
        StartFinish::Asphalt);
    add("construction haul pad", -16.0f, 5.0f, 0.10f, 16.0f, 0.10f, 11.0f,
        StartFinish::Concrete);
    add("construction sidewalk", 0.0f, -20.5f, 0.10f, 8.0f, 0.10f, 3.0f,
        StartFinish::Concrete);

    // Safety mesh fencing leaves a wide front gate for a truck and keeps the
    // frame readable from the street. It is visual-only so the yard remains
    // a useful walk-up landmark rather than a collision trap.
    add("construction fence mesh front west", -20.0f, -18.5f, 0.10f, 13.0f,
        2.4f, 0.12f, StartFinish::RedTrim);
    add("construction fence mesh front east", 20.0f, -18.5f, 0.10f, 13.0f,
        2.4f, 0.12f, StartFinish::RedTrim);
    add("construction fence mesh rear", 0.0f, 18.5f, 0.10f, 54.0f, 2.4f,
        0.12f, StartFinish::RedTrim);
    add("construction fence mesh west", -26.5f, 0.0f, 0.10f, 0.12f, 2.4f,
        37.0f, StartFinish::RedTrim);
    add("construction fence mesh east", 26.5f, 0.0f, 0.10f, 0.12f, 2.4f,
        37.0f, StartFinish::RedTrim);
    add("construction gate frame west", -4.8f, -18.35f, 0.10f, 0.18f, 3.5f,
        0.18f, StartFinish::Steel, true);
    add("construction gate frame east", 4.8f, -18.35f, 0.10f, 0.18f, 3.5f,
        0.18f, StartFinish::Steel, true);

    // The visible frame has exactly 14 of the 20 planned floor bands. Each
    // band has a slab, four columns and perimeter beams, with a small core to
    // sell the elevator/stair spine without turning the floor into a box.
    add("construction concrete core", 6.0f, 0.0f, 0.30f, 5.0f,
        14.0f * kConstructionFloorHeightM, 5.0f, StartFinish::Concrete, true);
    for (int floor = 0; floor < kConstructionSite.built_floor_count; ++floor) {
        // Leave a full ground-floor bay open so a worker can enter from the
        // front gate. The first slab is the top of that bay, not a ceiling at
        // ankle height.
        const float y = 3.82f + static_cast<float>(floor) * kConstructionFloorHeightM;
        add("construction frame floor slab", 6.0f, 0.0f, y,
            kConstructionSite.frame_width_m, 0.18f,
            kConstructionSite.frame_depth_m, StartFinish::Steel, true);
        const float column_y = floor == 0
                                   ? 0.50f
                                   : y - kConstructionFloorHeightM + 0.18f;
        for (float x : {-14.0f, 14.0f}) {
            for (float z : {-10.0f, 10.0f}) {
                add("construction frame column", 6.0f + x, z, column_y,
                    0.42f, kConstructionFloorHeightM - 0.20f, 0.42f,
                    StartFinish::Steel, true);
            }
        }
        const float beam_y = y - 0.42f;
        add("construction frame beam front", 6.0f, -10.0f, beam_y, 30.0f,
            0.34f, 0.34f, StartFinish::Steel, true);
        add("construction frame beam rear", 6.0f, 10.0f, beam_y, 30.0f,
            0.34f, 0.34f, StartFinish::Steel, true);
        add("construction frame beam west", -8.0f, 0.0f, beam_y, 0.34f,
            0.34f, 20.0f, StartFinish::Steel, true);
        add("construction frame beam east", 20.0f, 0.0f, beam_y, 0.34f,
            0.34f, 20.0f, StartFinish::Steel, true);
    }

    // Only some of the built floors have temporary edge protection. The
    // missing rails keep the 70% frame visibly unfinished while the repeated
    // braces and knee rails add a worker-scale rhythm above the yard.
    for (int floor : {2, 5, 8, 11, 13}) {
        const float y = 3.82f + static_cast<float>(floor) * kConstructionFloorHeightM;
        add("construction unfinished floor guardrail front", 6.0f, -10.35f,
            y + 0.18f, 30.0f, 0.95f, 0.12f, StartFinish::Steel, true);
        add("construction unfinished floor guardrail west", -8.15f, 0.0f,
            y + 0.18f, 0.12f, 0.95f, 20.0f, StartFinish::Steel, true);
        add("construction unfinished floor knee brace front", 6.0f, -10.43f,
            y + 0.63f, 30.0f, 0.12f, 0.10f, StartFinish::RedTrim, true);
        add("construction unfinished floor knee brace west", -8.23f, 0.0f,
            y + 0.63f, 0.10f, 0.12f, 20.0f, StartFinish::RedTrim, true);
    }
    // A bright wrap on the unfinished top edge makes the missing six floors
    // legible at street level and catches the generated safety-mesh texture.
    add("construction top safety mesh front", 6.0f, -10.05f, 48.15f, 30.0f,
        2.0f, 0.08f, StartFinish::RedTrim);
    add("construction top safety mesh rear", 6.0f, 10.05f, 48.15f, 30.0f,
        2.0f, 0.08f, StartFinish::RedTrim);

    // Tower crane, with a counter-jib and hanging pallet above the haul pad.
    add("construction crane base", -18.0f, 5.0f, 0.20f, 4.2f, 0.45f, 4.2f,
        StartFinish::Concrete, true);
    add("construction crane mast", -18.0f, 5.0f, 0.65f, 1.15f, 39.0f, 1.15f,
        StartFinish::Steel, true);
    add("construction crane jib", -3.0f, 5.0f, 39.15f, 29.0f, 0.55f, 0.65f,
        StartFinish::Yellow);
    add("construction crane counter jib", -25.0f, 5.0f, 39.15f, 14.0f, 0.55f,
        0.65f, StartFinish::Yellow);
    add("construction crane trolley", 7.0f, 5.0f, 39.0f, 1.1f, 0.65f, 1.1f,
        StartFinish::Steel);
    add("construction crane hoist cable", 7.0f, 5.0f, 29.0f, 0.08f, 10.0f,
        0.08f, StartFinish::Steel);
    add("construction crane hanging load", 7.0f, 5.0f, 28.25f, 1.7f, 1.4f,
        1.3f, StartFinish::RedTrim, true);

    // Temporary site office and plywood warning board.
    add("construction plywood office floor", -17.0f, -7.0f, 0.20f, 8.0f,
        0.12f, 5.5f, StartFinish::WarmWall, true);
    add("construction plywood office wall", -17.0f, -9.65f, 0.32f, 8.0f,
        2.8f, 0.16f, StartFinish::WarmWall, true);
    add("construction plywood office wall", -17.0f, -4.35f, 0.32f, 8.0f,
        2.8f, 0.16f, StartFinish::WarmWall, true);
    add("construction plywood office wall", -20.92f, -7.0f, 0.32f, 0.16f,
        2.8f, 5.5f, StartFinish::WarmWall, true);
    add("construction plywood office wall", -13.08f, -7.0f, 0.32f, 0.16f,
        2.8f, 5.5f, StartFinish::WarmWall, true);
    add("construction plywood office roof", -17.0f, -7.0f, 3.12f, 8.3f,
        0.18f, 5.8f, StartFinish::Steel, true);
    add("construction plywood safety board", -9.0f, -18.25f, 0.20f, 5.5f,
        2.6f, 0.14f, StartFinish::WarmWall, true);

    // Reusable plywood forms wait beside the frame. They sit on the west edge
    // of the yard, well clear of the open gate and the straight walk-up path.
    add("construction plywood formwork panel", -22.0f, 2.0f, 0.20f, 4.2f,
        2.2f, 0.18f, StartFinish::WarmWall, true);
    add("construction plywood formwork panel", -22.0f, 5.0f, 0.20f, 4.2f,
        2.2f, 0.18f, StartFinish::WarmWall, true);
    add("construction plywood formwork stack", -22.0f, 8.0f, 0.20f, 4.4f,
        0.55f, 1.8f, StartFinish::WarmWall, true);

    // Long stock is staged at the rear and east edges so the central haul
    // route stays readable from the gate.
    add("construction steel beam stack", 22.0f, 7.0f, 0.20f, 4.8f, 1.25f,
        1.25f, StartFinish::Steel, true);
    add("construction timber stack", -5.0f, 13.0f, 0.20f, 4.2f, 0.95f, 1.8f,
        StartFinish::WarmWall, true);
    add("construction timber stack cap", -5.0f, 13.0f, 1.22f, 4.4f, 0.12f,
        1.95f, StartFinish::Steel);

    // Temporary power and water hookups give the yard a bit of practical
    // infrastructure without turning the front approach into a snag point.
    add("construction utility hookup cabinet", -24.0f, -5.0f, 0.20f, 1.25f,
        1.85f, 0.70f, StartFinish::Steel, true);
    add("construction utility cable spool", -23.0f, -2.0f, 0.20f, 1.65f,
        0.85f, 1.65f, StartFinish::WarmWall, true);
    add("construction utility conduit", -20.5f, -2.0f, 0.95f, 4.5f, 0.16f,
        0.16f, StartFinish::Yellow);
    add("construction utility water tank", 22.0f, -6.0f, 0.20f, 2.0f, 1.65f,
        1.8f, StartFinish::White, true);

    // Small tools and staging furniture make the site read at pedestrian
    // height instead of as a collection of giant abstract blocks.
    add("construction sawhorse", -10.0f, 13.0f, 0.20f, 1.65f, 1.0f, 0.55f,
        StartFinish::Yellow, true);
    add("construction tool cart", -13.0f, 13.0f, 0.20f, 1.25f, 0.85f, 0.75f,
        StartFinish::Steel, true);
    add("construction hand truck", -15.0f, 13.0f, 0.20f, 0.65f, 1.35f, 0.55f,
        StartFinish::RedTrim, true);
    add("construction break table", -13.0f, 10.0f, 0.20f, 2.0f, 0.78f, 1.0f,
        StartFinish::WarmWall, true);
    add("construction break bench", -13.0f, 8.8f, 0.20f, 1.8f, 0.48f, 0.38f,
        StartFinish::WarmWall, true);

    // Yard props give the site a working rhythm at pedestrian scale.
    add("construction dumpster", 19.0f, 12.0f, 0.20f, 4.4f, 1.45f, 2.2f,
        StartFinish::Steel, true);
    add("construction dumpster lid", 19.0f, 12.0f, 1.68f, 4.6f, 0.10f, 2.35f,
        StartFinish::DarkRoof);
    add("construction rebar bundle", 14.0f, -7.0f, 0.20f, 5.5f, 0.55f, 0.85f,
        StartFinish::RedTrim, true);
    add("construction pipe rack", 14.0f, -10.0f, 0.20f, 5.5f, 1.75f, 1.8f,
        StartFinish::Steel, true);
    add("construction pipe bundle", 14.0f, -10.0f, 1.98f, 5.0f, 0.32f, 0.32f,
        StartFinish::Steel);
    add("construction cement mixer", -9.5f, 8.0f, 0.20f, 2.4f, 1.35f, 2.0f,
        StartFinish::Yellow, true);
    add("construction pallet", -4.5f, 9.5f, 0.20f, 2.4f, 0.18f, 2.0f,
        StartFinish::WarmWall, true);
    add("construction cement bags", -4.5f, 9.5f, 0.38f, 2.0f, 0.55f, 1.6f,
        StartFinish::White);
    add("construction portable toilet", -22.0f, 12.0f, 0.20f, 2.0f, 2.7f, 2.0f,
        StartFinish::TealDoor, true);
    for (float x : {-24.0f, -20.0f, 22.0f}) {
        add("construction traffic barrel", x, -13.0f, 0.20f, 0.72f, 1.0f,
            0.72f, StartFinish::RedTrim, true);
    }
    add("construction floodlight pole", 21.0f, 5.5f, 0.20f, 0.16f, 5.0f,
        0.16f, StartFinish::Steel, true);
    add("construction floodlight", 21.0f, 5.5f, 5.25f, 0.75f, 0.35f, 0.45f,
        StartFinish::Yellow);
    return out;
}

}  // namespace apricot::city
