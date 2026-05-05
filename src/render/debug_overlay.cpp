#include "render/debug_overlay.h"

#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "game/traffic.h"
#include "game/vehicle.h"
#include "physics/world_collision.h"
#include "render/camera.h"
#include "render/debug_draw.h"
#include "world/cell_coord.h"
#include "world/heightmap.h"
#include "world/road_grid.h"

namespace pengine::debug_overlay {

void render(const RenderState& s, DebugDraw& debug_draw,
            const Camera& camera, const glm::mat4& view_proj,
            const WorldCollision& world_col,
            const TrafficSystem& traffic) {
    debug_draw.clear();

    // Forward raycast (only really useful in DebugFly; harmless otherwise).
    if (s.in_debug_fly) {
        glm::vec3 ray_origin = camera.position;
        glm::vec3 ray_dir    = camera.forward();
        RayHit    hit        = world_col.raycast(ray_origin, ray_dir, 200.f);
        if (hit.hit) {
            debug_draw.line(ray_origin, hit.position);
            debug_draw.cross(hit.position, 0.4f);
        } else {
            debug_draw.line(ray_origin, ray_origin + ray_dir * 200.f);
        }
    }

    // Wheel contact markers (player car only; AI cars are kinematic).
    if (auto* pc = traffic.player_car()) {
        for (const Wheel& w : pc->vehicle.wheels()) {
            if (w.grounded) debug_draw.cross(w.contact_world, 0.2f);
        }
    }

    // Enter-vehicle prompt: ring around the targeted car.
    if (s.show_enter_prompt) {
        debug_draw.cylinder_xz(s.enter_prompt_base,
                                s.enter_prompt_radius, 0.05f, 32);
    }

    if (s.in_debug_fly)
        traffic.debug_draw(debug_draw);

    debug_draw.flush(view_proj, glm::vec3{1.f, 0.85f, 0.2f});
}

void log_world_area(const glm::vec3& pos, const glm::vec3& fwd,
                    const char* mode_label) {
    static int counter = 0;
    ++counter;

    CellCoord cell = world_to_cell(pos.x, pos.z, 256.f);

    int   ns_k = static_cast<int>(std::round(pos.x / ROAD_PITCH));
    int   ew_k = static_cast<int>(std::round(pos.z / ROAD_PITCH));
    float ns_x = static_cast<float>(ns_k) * ROAD_PITCH;
    float ew_z = static_cast<float>(ew_k) * ROAD_PITCH;

    float h_raw_p    = Heightmap::raw_sample(pos.x, pos.z);
    float h_carved_p = Heightmap::sample(pos.x, pos.z);

    PE_INFO("===== DEBUG #%d =====", counter);
    PE_INFO("Player    pos=(%.2f, %.2f, %.2f)  cell=(%d, %d)  mode=%s",
            pos.x, pos.y, pos.z, cell.x, cell.z, mode_label);
    PE_INFO("Forward   (%.3f, %.3f, %.3f)  bearing=%.1f deg",
            fwd.x, fwd.y, fwd.z,
            glm::degrees(std::atan2(fwd.x, -fwd.z)));
    PE_INFO("Heightmap raw=%.3f  carved=%.3f  delta=%.3f",
            h_raw_p, h_carved_p, h_carved_p - h_raw_p);
    PE_INFO("Nearest   NS road x=%.0f (%+.2fm)   EW road z=%.0f (%+.2fm)",
            ns_x, pos.x - ns_x, ew_z, pos.z - ew_z);

    // 5x5 carved heightmap grid centred on the position, 4 m spacing.
    PE_INFO("Carved heightmap grid (5x5, 4m, dz row x dx col):");
    for (int dz = -2; dz <= 2; ++dz) {
        char line[256] = {};
        int  n = 0;
        for (int dx = -2; dx <= 2; ++dx) {
            float qx = pos.x + static_cast<float>(dx) * 4.f;
            float qz = pos.z + static_cast<float>(dz) * 4.f;
            n += std::snprintf(line + n,
                               sizeof(line) - static_cast<std::size_t>(n),
                               " %7.2f", Heightmap::sample(qx, qz));
        }
        PE_INFO("  z%+3d:%s", dz * 4, line);
    }

    PE_INFO("NS road slab samples at x=%.0f, z = pos_z (-20 .. +20):", ns_x);
    for (int i = -5; i <= 5; ++i) {
        float qz       = pos.z + static_cast<float>(i) * 4.f;
        float carved   = Heightmap::sample(ns_x, qz);
        float raw      = Heightmap::raw_sample(ns_x, qz);
        PE_INFO("  z=%-9.2f  carved=%-7.3f  raw=%-7.3f  delta=%+.3f",
                qz, carved, raw, carved - raw);
    }

    PE_INFO("EW road slab samples at z=%.0f, x = pos_x (-20 .. +20):", ew_z);
    for (int i = -5; i <= 5; ++i) {
        float qx       = pos.x + static_cast<float>(i) * 4.f;
        float carved   = Heightmap::sample(qx, ew_z);
        float raw      = Heightmap::raw_sample(qx, ew_z);
        PE_INFO("  x=%-9.2f  carved=%-7.3f  raw=%-7.3f  delta=%+.3f",
                qx, carved, raw, carved - raw);
    }

    PE_INFO("Cell IPL  assets/world/cells/cell_%d_%d.ipl", cell.x, cell.z);
    PE_INFO("===== END #%d =====", counter);
}

} // namespace pengine::debug_overlay
