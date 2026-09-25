#version 330 core

in vec2 v_uv;

uniform sampler2D u_screen;
uniform sampler2D u_logo;
uniform vec2 u_cover;
uniform vec2 u_resolution;
uniform vec4 u_logo_rect;
uniform float u_time;

out vec4 frag_color;

void main() {
    // Cover-crop only the artwork. Logo and UI keep their own fixed aspect.
    vec2 uv = (v_uv - 0.5) * u_cover / 1.015 + 0.5;
    uv.x += sin(u_time * 0.055) * 0.003;
    vec3 color = texture(u_screen, uv).rgb;
    vec2 screen = vec2(v_uv.x, 1.0 - v_uv.y);
    color *= 1.0 - 0.22 * (1.0 - smoothstep(0.1, 0.65, screen.x));

    // Sparse rain streaks in aspect-correct screen space. No flashes or grain
    // over the typography: composite the stable logo after the atmosphere.
    vec2 p = vec2(screen.x * u_resolution.x / u_resolution.y, screen.y);
    float lane = floor((p.x + p.y * 0.12) * 150.0);
    float seed = fract(sin(lane * 127.1) * 43758.5453);
    float streak = fract(p.y * 3.0 - u_time * (0.65 + seed * 0.45) + seed * 17.0);
    float thin = 1.0 - smoothstep(0.02, 0.12, abs(fract((p.x + p.y * 0.12) * 150.0) - 0.5));
    float rain = thin * smoothstep(0.90, 0.94, streak) * (1.0 - smoothstep(0.97, 1.0, streak));
    color += vec3(0.25, 0.27, 0.28) * rain * 0.12 * smoothstep(0.35, 0.8, screen.x);
    color *= smoothstep(0.0, 0.8, u_time);

    // Sample the logo on every pixel and mask afterwards. Fetching inside the
    // rect test puts a mipmapped texture() in non-uniform control flow, where
    // the implicit LOD is undefined: quads straddling the rect edge picked a
    // tiny mip and drew a faint cream hairline along the top and bottom.
    vec2 logo_uv = (screen - u_logo_rect.xy) / u_logo_rect.zw;
    vec4 logo = texture(u_logo, clamp(vec2(logo_uv.x, 1.0 - logo_uv.y), 0.0, 1.0));
    float inside = step(0.0, logo_uv.x) * step(logo_uv.x, 1.0)
                 * step(0.0, logo_uv.y) * step(logo_uv.y, 1.0);
    color = mix(color, logo.rgb, inside * logo.a * smoothstep(0.65, 2.1, u_time));
    frag_color = vec4(color, 1.0);
}
