#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "city/map.h"

namespace apricot {
namespace city {

// The headless half of Apricot's Sims-style building creator. Authors place
// wall runs, openings and roofs; bake_building() turns them into the simple
// pieces the current scene/vehicle collider already understand. Keeping the
// document free of renderer types lets a future in-game editor and an offline
// cooker share the exact same source data.
enum class BuildingFinish : uint8_t {
    Asphalt,
    Concrete,
    WarmWall,
    Brick,
    RedTrim,
    DarkRoof,
    Glass,
    TealDoor,
    Steel,
    White,
    Yellow,
    PoolWater,
};

enum class OpeningKind : uint8_t { Door, Window };

struct BuildingOpening {
    const char* name = nullptr;
    OpeningKind kind = OpeningKind::Door;

    // Metres from wall endpoint a to the opening centre.
    float center_m = 0.0f;
    float width_m = 1.0f;
    float sill_m = 0.0f;
    float height_m = 2.1f;
    BuildingFinish finish = BuildingFinish::TealDoor;

    // Windows only. Zero makes a clean single pane; larger values split it in
    // the same style as the alpha building tool's window catalog.
    uint8_t vertical_bars = 0;
    uint8_t horizontal_bars = 0;

    // Door and window framing is normally the district accent colour. A bank
    // or civic building can ask for steel framing without teaching the baker
    // about specific building names.
    BuildingFinish frame_finish = BuildingFinish::RedTrim;

    // Door openings may leave their leaf out. This is useful for an authored
    // permanently-open entrance: the wall/collision gap remains real and a
    // pair of angled fixture leaves can show the open state without a runtime
    // door system.
    bool leaf = true;
};

struct BuildingWall {
    const char* name = nullptr;
    Vec2 a{};
    Vec2 b{};
    float bottom_m = 0.0f;
    float height_m = 3.0f;
    float thickness_m = 0.24f;
    BuildingFinish finish = BuildingFinish::WarmWall;
    const BuildingOpening* openings = nullptr;
    std::size_t opening_count = 0;
};

enum class RoofStyle : uint8_t { Flat, Gable };
enum class RidgeAxis : uint8_t { AlongX, AlongZ };

struct BuildingRoof {
    const char* name = nullptr;
    Vec2 centre{};
    float bottom_m = 3.0f;   // eave or flat-roof underside
    float width_m = 4.0f;
    float depth_m = 4.0f;
    float rise_m = 0.0f;     // ignored by Flat
    float thickness_m = 0.24f;
    float overhang_m = 0.35f;
    float parapet_m = 0.0f;  // Flat only
    RoofStyle style = RoofStyle::Flat;
    RidgeAxis ridge = RidgeAxis::AlongX;
    BuildingFinish finish = BuildingFinish::DarkRoof;
    BuildingFinish parapet_finish = BuildingFinish::Brick;

    // Gable only. Opt in for enclosed buildings; open shelters keep their
    // existing two-panel roof. Ends sit on the wall centre lines, under the
    // overhang, with the same thickness/finish as the supporting walls.
    float gable_end_thickness_m = 0.0f;
    BuildingFinish gable_end_finish = BuildingFinish::WarmWall;
};

enum class BuildingPieceShape : uint8_t { Box, GablePrism };

// Baked pieces use a unit-volume primitive with a local transform, finish and
// collision bit. GablePrism is a triangular end wall in XY, extruded along Z.
struct BuildingPiece {
    const char* name = nullptr;
    Vec2 centre{};
    float bottom_m = 0.0f;
    float width_m = 1.0f;
    float height_m = 1.0f;
    float depth_m = 1.0f;
    BuildingFinish finish = BuildingFinish::Concrete;
    bool solid = false;

    // Local rotation, applied before the whole site's grid rotation.
    float pitch_deg = 0.0f;
    float yaw_deg = 0.0f;
    float roll_deg = 0.0f;
    BuildingPieceShape shape = BuildingPieceShape::Box;
};

// Exterior stair run. The footprint centre and yaw place the whole flight;
// steps rise along local +Z. It is a first-class creator primitive because an
// upper floor with no way up is set dressing, not a building.
struct BuildingStair {
    const char* name = nullptr;
    Vec2 centre{};
    float bottom_m = 0.0f;
    float width_m = 1.2f;
    float run_m = 3.0f;
    float rise_m = 3.0f;
    uint8_t step_count = 10;
    float yaw_deg = 0.0f;
    BuildingFinish finish = BuildingFinish::Concrete;
    bool solid = true;
};

struct BuildingPlan {
    const char* name = nullptr;
    const BuildingWall* walls = nullptr;
    std::size_t wall_count = 0;
    const BuildingRoof* roofs = nullptr;
    std::size_t roof_count = 0;

    // Creator-placed slabs, columns, signs and props. This mirrors the alpha
    // tool's prop records and keeps the complete authored building inside one
    // plan instead of letting World bolt loose boxes on after the bake.
    const BuildingPiece* fixtures = nullptr;
    std::size_t fixture_count = 0;
    const BuildingStair* stairs = nullptr;
    std::size_t stair_count = 0;
};

// Invalid or degenerate records are skipped. Openings are clamped into their
// wall and subtracted in 2D, so stacked openings (like a transom over a door)
// do not accidentally refill each other.
std::vector<BuildingPiece> bake_building(const BuildingPlan& plan);

bool valid_building_plan(const BuildingPlan& plan);

}  // namespace city
}  // namespace apricot
