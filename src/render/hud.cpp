#include "render/hud.h"

namespace pengine {

bool Hud::init(const std::string& assets_root) {
    return minimap_.init(assets_root)
        && speedometer_.init(assets_root)
        && crosshair_.init(assets_root)
        && wanted_stars_.init(assets_root);
}

void Hud::shutdown() {
    minimap_.shutdown();
    speedometer_.shutdown();
    crosshair_.shutdown();
    wanted_stars_.shutdown();
}

void Hud::render(const State& s) {
    {
        Minimap::DrawState ms;
        ms.player_pos_world = s.player_pos_world;
        ms.player_yaw_deg   = s.player_yaw_deg;
        ms.viewport_size_px = s.viewport_size_px;
        minimap_.draw(ms);
    }

    if (s.show_speedometer) {
        Speedometer::DrawState ss;
        ss.speed_kmh        = s.speed_kmh;
        ss.viewport_size_px = s.viewport_size_px;
        speedometer_.draw(ss);
    }

    if (s.show_crosshair) {
        Crosshair::DrawState cs;
        cs.viewport_size_px = s.viewport_size_px;
        crosshair_.draw(cs);
    }

    {
        WantedStars::DrawState ws;
        ws.wanted_level     = s.wanted_level;
        ws.health           = s.health;
        ws.armor            = 0.f;
        ws.money            = 0;
        ws.hour             = 12;
        ws.minute           = 0;
        ws.viewport_size_px = s.viewport_size_px;
        wanted_stars_.draw(ws);
    }
}

} // namespace pengine
