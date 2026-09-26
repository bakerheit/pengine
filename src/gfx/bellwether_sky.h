#pragma once
#include "gfx/sky_env.h"

namespace apricot {
// Reference palette: blue hour above, a small amber strip at the horizon.
// Keep the real procedural clouds and solar direction, so this is a complete
// environment and still works when the camera turns around.
inline SkyEnv bellwether_dusk_sky(const WeatherParams& weather) {
    SkyEnv e=compute_sky_env(.775f,weather);
    e.sky_top={.038f,.067f,.145f};
    e.sky_bottom={.09f,.135f,.215f};
    e.sun_color={.72f,.29f,.10f};
    e.cloud_color={.12f,.17f,.26f};
    e.cloud_cover=.62f;
    e.dusk_style=1.0f;
    e.ambient={.19f,.21f,.26f};
    e.light_color={.42f,.46f,.58f};
    e.star_intensity=.035f;
    e.specular_strength=.10f;
    e.fog_color={.15f,.17f,.215f};
    return e;
}
} // namespace apricot
