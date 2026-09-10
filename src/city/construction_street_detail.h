#pragma once

#include <vector>

#include "city/construction_site.h"

namespace apricot::city {

// A narrow service strip on the east edge of the Pinatty construction yard.
// It is authored as its own StartSite so the parent can append it separately
// from the high-rise frame, while sharing the yard's six-degree grid basis.
inline constexpr StartSite kConstructionStreetDetailSite{
    "Pinatty Yard Street Detail", kConstructionSite.site.origin,
    kConstructionSite.site.cos_yaw, kConstructionSite.site.sin_yaw,
    {31.0f, 0.0f}, 6.0f, 38.0f, 12.0f, 1200.0f};

inline std::vector<StartPart> bake_construction_street_detail() {
    std::vector<StartPart> out;
    out.reserve(48);

    const auto add = [&](const char* name, float x, float z, float bottom,
                         float width, float height, float depth,
                         StartFinish finish, bool solid = false) {
        out.push_back({name, {x, z}, bottom, width, height, depth, finish,
                       solid});
    };

    // One continuous service apron, plus clearly named surfaces for the
    // worker parking and truck turn pad. These are the support rectangles the
    // parent integration can register alongside the construction yard slab.
    add("construction detail service apron", 31.0f, 0.0f, 0.0f, 5.5f,
        0.10f, 37.0f, StartFinish::Asphalt);
    add("construction detail worker parking pad", 31.0f, -11.0f, 0.10f,
        5.0f, 0.08f, 10.0f, StartFinish::Concrete);
    add("construction detail truck turn pad", 31.0f, 10.0f, 0.10f, 5.0f,
        0.08f, 12.0f, StartFinish::Concrete);

    // Six short parking bay lines make the narrow strip read as a worker
    // staging area instead of an empty sidewalk slab.
    for (float z : {-14.0f, -11.0f, -8.0f}) {
        add("construction detail parking line", 31.0f, z, 0.19f, 4.6f,
            0.025f, 0.10f, StartFinish::White);
    }
    for (float x : {28.5f, 33.5f}) {
        add("construction detail turn pad edge", x, 10.0f, 0.19f, 0.10f,
            0.025f, 11.0f, StartFinish::Yellow);
    }

    // A pair of tall, striped gate markers points trucks toward the yard's
    // service opening without creating a new collision choke point.
    for (float z : {-3.5f, 3.5f}) {
        add("construction truck gate marker", 28.55f, z, 0.10f, 0.34f,
            2.8f, 0.34f, StartFinish::RedTrim, true);
        add("construction truck gate marker cap", 28.55f, z, 2.90f, 0.55f,
            0.18f, 0.55f, StartFinish::Yellow);
    }

    // Wayfinding boards face the east street. The plywood prefix is
    // intentional: the runtime construction material router can apply the
    // generated plywood texture without knowing about this module.
    for (float z : {-16.5f, 16.5f}) {
        add("construction plywood wayfinding board", 31.0f, z, 1.55f,
            0.14f, 1.55f, 3.8f, StartFinish::WarmWall);
        add("construction wayfinding board post", 31.0f, z - 1.45f, 0.10f,
            0.16f, 1.55f, 0.16f, StartFinish::Steel, true);
        add("construction wayfinding board post", 31.0f, z + 1.45f, 0.10f,
            0.16f, 1.55f, 0.16f, StartFinish::Steel, true);
    }

    // Low concrete barriers keep the parking strip visually separated from
    // the active yard while leaving a wide central truck path.
    for (float z : {-17.5f, 0.0f, 17.5f}) {
        add("construction street barrier", 28.65f, z, 0.10f, 0.90f, 0.72f,
            2.7f, StartFinish::Concrete, true);
        add("construction street barrier stripe", 28.18f, z, 0.48f, 0.05f,
            0.34f, 2.72f, StartFinish::Yellow);
    }

    // Two simple work lights cover the parking and turn areas. They are
    // authored as pole and lamp pieces so the parent can later opt the lamps
    // into the runtime light list without changing this geometry package.
    for (float z : {-13.5f, 13.5f}) {
        add("construction detail light pole", 33.0f, z, 0.10f, 0.18f, 4.5f,
            0.18f, StartFinish::Steel, true);
        add("construction detail light head", 33.0f, z, 4.65f, 0.70f, 0.30f,
            0.55f, StartFinish::Yellow);
    }

    // Utility cabinets and a short cable trough give the strip a believable
    // temporary-power edge without blocking the worker route.
    for (float z : {-5.5f, 5.5f}) {
        add("construction utility cabinet", 33.0f, z, 0.10f, 0.95f, 1.35f,
            0.55f, StartFinish::TealDoor, true);
    }
    add("construction utility cable trough", 32.4f, 0.0f, 0.10f, 0.32f,
        0.28f, 16.0f, StartFinish::Steel, true);

    return out;
}

}  // namespace apricot::city
