#include "city/building_creator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace apricot {
namespace city {
namespace {

constexpr float kEps = 1e-4f;
constexpr float kFrameWidthM = 0.11f;
constexpr float kGlassDepthM = 0.045f;

struct Hole {
    const BuildingOpening* opening = nullptr;
    float u0 = 0.0f;
    float u1 = 0.0f;
    float y0 = 0.0f;
    float y1 = 0.0f;
};

struct WallBasis {
    Vec2 a{};
    Vec2 dir{};
    float length = 0.0f;
    float yaw_deg = 0.0f;
};

WallBasis wall_basis(const BuildingWall& wall) {
    WallBasis out;
    out.a = wall.a;
    const float dx = wall.b.x - wall.a.x;
    const float dz = wall.b.z - wall.a.z;
    out.length = std::sqrt(dx * dx + dz * dz);
    if (out.length <= kEps) return out;
    out.dir = {dx / out.length, dz / out.length};
    // A unit box's long axis is +X. GLM's +Y rotation maps it to
    // (cos(yaw), -sin(yaw)) in XZ.
    out.yaw_deg = std::atan2(-out.dir.z, out.dir.x) * 57.2957795131f;
    return out;
}

BuildingPiece piece_on_wall(const WallBasis& basis, const char* name,
                            float u0, float u1, float y0,
                            float y1, float depth, BuildingFinish finish,
                            bool solid) {
    const float um = (u0 + u1) * 0.5f;
    BuildingPiece out;
    out.name = name;
    out.centre = {basis.a.x + basis.dir.x * um,
                  basis.a.z + basis.dir.z * um};
    out.bottom_m = y0;
    out.width_m = u1 - u0;
    out.height_m = y1 - y0;
    out.depth_m = depth;
    out.finish = finish;
    out.solid = solid;
    out.yaw_deg = basis.yaw_deg;
    return out;
}

std::vector<Hole> resolve_holes(const BuildingWall& wall,
                                const WallBasis& basis) {
    std::vector<Hole> holes;
    holes.reserve(wall.opening_count);
    for (std::size_t i = 0; i < wall.opening_count; ++i) {
        const BuildingOpening& op = wall.openings[i];
        const float half = std::max(0.0f, op.width_m) * 0.5f;
        const float u0 = std::max(0.0f, std::min(op.center_m - half,
                                                basis.length));
        const float u1 = std::max(0.0f, std::min(op.center_m + half,
                                                basis.length));
        const float y0 = std::max(wall.bottom_m,
                                  wall.bottom_m + std::max(0.0f, op.sill_m));
        const float y1 = std::min(wall.bottom_m + wall.height_m,
                                  y0 + std::max(0.0f, op.height_m));
        if (u1 - u0 <= kEps || y1 - y0 <= kEps) continue;
        holes.push_back({&op, u0, u1, y0, y1});
    }
    std::sort(holes.begin(), holes.end(), [](const Hole& a, const Hole& b) {
        if (a.u0 != b.u0) return a.u0 < b.u0;
        return a.y0 < b.y0;
    });
    return holes;
}

void emit_wall_shell(const BuildingWall& wall, const WallBasis& basis,
                     const std::vector<Hole>& holes,
                     std::vector<BuildingPiece>& out) {
    std::vector<float> cuts{0.0f, basis.length};
    cuts.reserve(holes.size() * 2u + 2u);
    for (const Hole& h : holes) {
        cuts.push_back(h.u0);
        cuts.push_back(h.u1);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](float a, float b) {
                   return std::fabs(a - b) <= kEps;
               }),
               cuts.end());

    std::vector<std::pair<float, float>> covered;
    for (std::size_t c = 0; c + 1u < cuts.size(); ++c) {
        const float u0 = cuts[c];
        const float u1 = cuts[c + 1u];
        if (u1 - u0 <= kEps) continue;
        const float mid = (u0 + u1) * 0.5f;

        covered.clear();
        for (const Hole& h : holes) {
            if (h.u0 < mid && mid < h.u1) covered.emplace_back(h.y0, h.y1);
        }
        std::sort(covered.begin(), covered.end());

        float y = wall.bottom_m;
        const float top = wall.bottom_m + wall.height_m;
        for (const auto& span : covered) {
            if (span.first - y > kEps) {
                out.push_back(piece_on_wall(basis, wall.name, u0, u1, y,
                                            span.first, wall.thickness_m,
                                            wall.finish, true));
            }
            y = std::max(y, span.second);
        }
        if (top - y > kEps) {
            out.push_back(piece_on_wall(basis, wall.name, u0, u1, y, top,
                                        wall.thickness_m, wall.finish, true));
        }
    }
}

void emit_opening_dressing(const BuildingWall& wall, const WallBasis& basis,
                           const Hole& h, std::vector<BuildingPiece>& out) {
    const BuildingOpening& op = *h.opening;
    const float frame_depth = wall.thickness_m + 0.045f;
    const float fw = std::min(kFrameWidthM, (h.u1 - h.u0) * 0.18f);
    const float fh = std::min(kFrameWidthM, (h.y1 - h.y0) * 0.18f);
    const BuildingFinish frame_finish = op.frame_finish;

    // Jambs and head are separate so the source opening stays a real hole.
    out.push_back(piece_on_wall(basis, op.name, h.u0, h.u0 + fw,
                                h.y0, h.y1, frame_depth, frame_finish, false));
    out.push_back(piece_on_wall(basis, op.name, h.u1 - fw, h.u1,
                                h.y0, h.y1, frame_depth, frame_finish, false));
    out.push_back(piece_on_wall(basis, op.name, h.u0, h.u1,
                                h.y1 - fh, h.y1, frame_depth, frame_finish,
                                false));

    if (op.kind == OpeningKind::Door) {
        // The current driving pilot has no door system yet. Keep a thin leaf in
        // the real cutout so the facade reads correctly without returning to a
        // solid building-sized block.
        if (op.leaf) {
            out.push_back(piece_on_wall(basis, op.name, h.u0 + fw,
                                        h.u1 - fw, h.y0, h.y1 - fh,
                                        kGlassDepthM, op.finish, false));
        }
        return;
    }

    // A proper inset pane plus a sill and style-driven mullions.
    out.push_back(piece_on_wall(basis, op.name, h.u0 + fw,
                                h.u1 - fw, h.y0 + fh, h.y1 - fh,
                                kGlassDepthM, op.finish, false));
    out.push_back(piece_on_wall(basis, op.name, h.u0, h.u1,
                                h.y0, h.y0 + fh, frame_depth,
                                frame_finish, false));

    for (uint8_t i = 1; i <= op.vertical_bars; ++i) {
        const float t = static_cast<float>(i) /
                        static_cast<float>(op.vertical_bars + 1u);
        const float u = h.u0 + (h.u1 - h.u0) * t;
        out.push_back(piece_on_wall(basis, op.name, u - fw * 0.35f,
                                    u + fw * 0.35f, h.y0, h.y1,
                                    frame_depth, frame_finish, false));
    }
    for (uint8_t i = 1; i <= op.horizontal_bars; ++i) {
        const float t = static_cast<float>(i) /
                        static_cast<float>(op.horizontal_bars + 1u);
        const float y = h.y0 + (h.y1 - h.y0) * t;
        out.push_back(piece_on_wall(basis, op.name, h.u0, h.u1,
                                    y - fh * 0.35f, y + fh * 0.35f,
                                    frame_depth, frame_finish, false));
    }
}

void emit_flat_roof(const BuildingRoof& roof,
                    std::vector<BuildingPiece>& out) {
    const float w = roof.width_m + roof.overhang_m * 2.0f;
    const float d = roof.depth_m + roof.overhang_m * 2.0f;
    out.push_back({roof.name, roof.centre, roof.bottom_m, w,
                   roof.thickness_m, d, roof.finish, false});
    if (roof.parapet_m <= kEps) return;

    const float t = std::max(0.18f, roof.thickness_m);
    const float y = roof.bottom_m + roof.thickness_m;
    out.push_back({roof.name, {roof.centre.x, roof.centre.z - d * 0.5f}, y,
                   w, roof.parapet_m, t, roof.parapet_finish, false});
    out.push_back({roof.name, {roof.centre.x, roof.centre.z + d * 0.5f}, y,
                   w, roof.parapet_m, t, roof.parapet_finish, false});
    out.push_back({roof.name, {roof.centre.x - w * 0.5f, roof.centre.z}, y,
                   t, roof.parapet_m, d, roof.parapet_finish, false});
    out.push_back({roof.name, {roof.centre.x + w * 0.5f, roof.centre.z}, y,
                   t, roof.parapet_m, d, roof.parapet_finish, false});
}

void emit_gable_roof(const BuildingRoof& roof,
                     std::vector<BuildingPiece>& out) {
    const float w = roof.width_m + roof.overhang_m * 2.0f;
    const float d = roof.depth_m + roof.overhang_m * 2.0f;
    const float rise = std::max(0.15f, roof.rise_m);

    if (roof.ridge == RidgeAxis::AlongX) {
        const float run = d * 0.5f;
        const float slope = std::sqrt(run * run + rise * rise);
        const float angle = std::atan2(rise, run) * 57.2957795131f;
        BuildingPiece north{roof.name,
                            {roof.centre.x, roof.centre.z - run * 0.5f},
                            roof.bottom_m + rise * 0.5f - roof.thickness_m * 0.5f,
                            w, roof.thickness_m, slope, roof.finish, false};
        north.pitch_deg = -angle;
        BuildingPiece south = north;
        south.centre.z = roof.centre.z + run * 0.5f;
        south.pitch_deg = angle;
        out.push_back(north);
        out.push_back(south);
    } else {
        const float run = w * 0.5f;
        const float slope = std::sqrt(run * run + rise * rise);
        const float angle = std::atan2(rise, run) * 57.2957795131f;
        BuildingPiece west{roof.name,
                           {roof.centre.x - run * 0.5f, roof.centre.z},
                           roof.bottom_m + rise * 0.5f - roof.thickness_m * 0.5f,
                           slope, roof.thickness_m, d, roof.finish, false};
        west.roll_deg = angle;
        BuildingPiece east = west;
        east.centre.x = roof.centre.x + run * 0.5f;
        east.roll_deg = -angle;
        out.push_back(west);
        out.push_back(east);
    }

    if (roof.gable_end_thickness_m <= kEps) return;
    const bool along_x = roof.ridge == RidgeAxis::AlongX;
    const float thickness = roof.gable_end_thickness_m;
    const float run = (along_x ? d : w) * 0.5f;
    // Keep the siding at the walls, not at the outer edge of the overhang.
    // A short rectangular band fills the height gained between eave and
    // wall; the triangular prism then follows the exact pitched roof line.
    const float span = std::min(along_x ? d : w,
        (along_x ? roof.depth_m : roof.width_m) + thickness);
    const float knee = rise * (1.0f - span * 0.5f / run);
    const float wall_overlap = std::min(0.02f,roof.bottom_m);
    for (float sign : {-1.0f, 1.0f}) {
        Vec2 centre = roof.centre;
        if (along_x) centre.x += sign * roof.width_m * 0.5f;
        else centre.z += sign * roof.depth_m * 0.5f;
        BuildingPiece band{"gable end base", centre,
            roof.bottom_m - wall_overlap, span, knee + wall_overlap,
            thickness, roof.gable_end_finish, false};
        band.yaw_deg = along_x ? 90.0f : 0.0f;
        if (band.height_m > kEps) out.push_back(band);
        BuildingPiece end{"gable end wall", centre, roof.bottom_m + knee,
            span, rise - knee, thickness, roof.gable_end_finish, false};
        end.yaw_deg = band.yaw_deg;
        end.shape = BuildingPieceShape::GablePrism;
        out.push_back(end);
    }
}

void emit_stair(const BuildingStair& stair,
                std::vector<BuildingPiece>& out) {
    const float step_depth = stair.run_m / static_cast<float>(stair.step_count);
    const float yaw = stair.yaw_deg * 0.017453292519943f;
    const float along_x = std::sin(yaw);
    const float along_z = std::cos(yaw);
    for (uint8_t i = 0; i < stair.step_count; ++i) {
        const float local_z =
            -stair.run_m * 0.5f +
            (static_cast<float>(i) + 0.5f) * step_depth;
        const float height =
            stair.rise_m * static_cast<float>(i + 1u) /
            static_cast<float>(stair.step_count);

        BuildingPiece step;
        step.name = stair.name;
        step.centre = {stair.centre.x + along_x * local_z,
                       stair.centre.z + along_z * local_z};
        step.bottom_m = stair.bottom_m;
        step.width_m = stair.width_m;
        step.height_m = height;
        step.depth_m = step_depth;
        step.finish = stair.finish;
        step.solid = stair.solid;
        step.yaw_deg = stair.yaw_deg;
        out.push_back(step);
    }
}

}  // namespace

bool valid_building_plan(const BuildingPlan& plan) {
    if (plan.name == nullptr) return false;
    if (plan.wall_count > 0u && plan.walls == nullptr) return false;
    if (plan.roof_count > 0u && plan.roofs == nullptr) return false;
    if (plan.fixture_count > 0u && plan.fixtures == nullptr) return false;
    if (plan.stair_count > 0u && plan.stairs == nullptr) return false;
    for (std::size_t i = 0; i < plan.wall_count; ++i) {
        const BuildingWall& wall = plan.walls[i];
        const WallBasis basis = wall_basis(wall);
        if (wall.name == nullptr || basis.length <= kEps ||
            wall.bottom_m < 0.0f || wall.height_m <= kEps ||
            wall.thickness_m <= kEps ||
            (wall.opening_count > 0u && wall.openings == nullptr)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < plan.roof_count; ++i) {
        const BuildingRoof& roof = plan.roofs[i];
        if (roof.name == nullptr || roof.bottom_m < 0.0f ||
            roof.width_m <= kEps || roof.depth_m <= kEps ||
            roof.thickness_m <= kEps || roof.overhang_m < 0.0f ||
            !std::isfinite(roof.gable_end_thickness_m) ||
            roof.gable_end_thickness_m < 0.0f) {
            return false;
        }
    }
    for (std::size_t i = 0; i < plan.fixture_count; ++i) {
        const BuildingPiece& fixture = plan.fixtures[i];
        if (fixture.name == nullptr || fixture.bottom_m < 0.0f ||
            fixture.width_m <= kEps || fixture.height_m <= kEps ||
            fixture.depth_m <= kEps || !std::isfinite(fixture.pitch_deg) ||
            !std::isfinite(fixture.yaw_deg) ||
            !std::isfinite(fixture.roll_deg)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < plan.stair_count; ++i) {
        const BuildingStair& stair = plan.stairs[i];
        if (stair.name == nullptr || stair.bottom_m < 0.0f ||
            stair.width_m <= kEps || stair.run_m <= kEps ||
            stair.rise_m <= kEps || stair.step_count == 0u ||
            !std::isfinite(stair.yaw_deg)) {
            return false;
        }
    }
    return true;
}

std::vector<BuildingPiece> bake_building(const BuildingPlan& plan) {
    std::vector<BuildingPiece> out;
    if (!valid_building_plan(plan)) return out;

    for (std::size_t i = 0; i < plan.wall_count; ++i) {
        const BuildingWall& wall = plan.walls[i];
        const WallBasis basis = wall_basis(wall);
        const std::vector<Hole> holes = resolve_holes(wall, basis);
        emit_wall_shell(wall, basis, holes, out);
        for (const Hole& hole : holes) {
            emit_opening_dressing(wall, basis, hole, out);
        }
    }
    for (std::size_t i = 0; i < plan.roof_count; ++i) {
        const BuildingRoof& roof = plan.roofs[i];
        if (roof.style == RoofStyle::Flat) emit_flat_roof(roof, out);
        else emit_gable_roof(roof, out);
    }
    if (plan.fixture_count > 0u) {
        out.insert(out.end(), plan.fixtures,
                   plan.fixtures + plan.fixture_count);
    }
    for (std::size_t i = 0; i < plan.stair_count; ++i) {
        emit_stair(plan.stairs[i], out);
    }
    return out;
}

}  // namespace city
}  // namespace apricot
