#version 330 core

// Procedural sky: vertical gradient, sun disc and glow, stars, drifting cloud.
// Everything derives from the per-pixel world-space VIEW DIRECTION, which is
// reconstructed from a rotation-only inverse view-projection — the sky is
// infinitely far away, so the camera's position must not enter into it. Feed it
// the full inverse and the horizon slides around as you drive.

in  vec2 v_ndc;
out vec4 frag_color;

uniform mat4  u_inv_view_rot_proj;
uniform vec3  u_sun_dir;
uniform vec3  u_sun_color;
uniform vec3  u_sky_top;
uniform vec3  u_sky_bottom;
uniform vec3  u_cloud_color;
uniform float u_time;             // seconds; cloud drift and star twinkle
uniform float u_star_intensity;   // 0 by day, 1 at night
uniform float u_cloud_cover;      // 0..1

// --- cheap hash / value noise. No textures, so the sky costs zero VRAM. ------
float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i),                hash21(i + vec2(1.0, 0.0)), f.x),
               mix(hash21(i + vec2(0.0,1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; ++i) {
        v += amp * vnoise(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return v;
}

void main() {
    vec4 world = u_inv_view_rot_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 dir   = normalize(world.xyz / world.w);
    float up   = clamp(dir.y, -1.0, 1.0);

    // Base gradient. The pow() biases the blend toward the horizon so most of
    // the visible sky is the interesting part rather than flat zenith blue.
    float t   = pow(clamp(up * 0.5 + 0.5, 0.0, 1.0), 0.55);
    vec3  col = mix(u_sky_bottom, u_sky_top, t);

    // A thick cloud deck must swallow the sun, or heavy overcast reads as a
    // bright disc punching through grey — the tell that weather is painted on
    // top of the sky rather than part of it.
    float sun_vis = 1.0 - smoothstep(0.55, 0.85, u_cloud_cover);

    // Sun: a tight disc plus a wide warm glow.
    float sun_d = max(dot(dir, u_sun_dir), 0.0);
    float disc  = smoothstep(0.9994, 0.9997, sun_d);
    float glow  = pow(sun_d, 256.0) * 0.6 + pow(sun_d, 8.0) * 0.15;
    col += u_sun_color * (disc * 1.4 + glow) * sun_vis;

    // Horizon warming while the sun is low, only along the sun's azimuth.
    float horizon = pow(max(1.0 - abs(up), 0.0), 6.0);
    float low_sun = smoothstep(0.35, 0.0, abs(u_sun_dir.y));
    float azimuth = smoothstep(0.0, 0.6,
        dot(normalize(dir.xz + vec2(1e-5)), normalize(u_sun_dir.xz + vec2(1e-5))) * 0.5 + 0.5);
    col += u_sun_color * horizon * low_sun * azimuth * 0.35 * sun_vis;

    // Stars, upper hemisphere only, on a coarse dome projection.
    if (u_star_intensity > 0.001 && up > 0.02) {
        vec2  suv  = dir.xz / (up + 0.3) * 60.0;
        vec2  cell = floor(suv);
        float star = hash21(cell);
        if (star > 0.985) {                       // ~1.5% of cells hold a star
            vec2  fp      = fract(suv) - 0.5;
            float point   = smoothstep(0.09, 0.0, length(fp));
            float twinkle = 0.6 + 0.4 * sin(u_time * 3.0 + star * 100.0);
            col += vec3(point * twinkle) * u_star_intensity
                   * smoothstep(0.02, 0.2, up);   // fade out toward the horizon
        }
    }

    // Cloud deck: fbm on the dome, drifting. Faded near the horizon where the
    // dome projection stretches into streaks.
    if (up > 0.0) {
        vec2  cuv    = dir.xz / (up + 0.15) * 1.5 + vec2(u_time * 0.006, u_time * 0.003);
        float n      = fbm(cuv);
        float thresh = mix(0.68, 0.30, u_cloud_cover);
        float clouds = smoothstep(thresh, thresh + 0.22, n) * smoothstep(0.0, 0.25, up);
        col = mix(col, u_cloud_color, clouds * 0.9);
    }

    frag_color = vec4(col, 1.0);
}
