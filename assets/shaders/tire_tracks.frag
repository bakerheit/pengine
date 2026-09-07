#version 330 core

#include "lighting.glsl"

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_color;
in float v_alpha;
flat in float v_surface;

out vec4 frag_color;

void main() {
    float edge = smoothstep(0.0, 0.38, 1.0 - abs(v_uv.x));
    float ends = 1.0 - smoothstep(0.58, 1.0, abs(v_uv.y));
    float coverage = edge * ends;

    bool snow = v_surface > 1.5;
    if (snow) {
        // A compressed channel with darker shoulders reads as a shallow groove
        // even though this is still a flat decal on the current snow material.
        float shoulder = smoothstep(0.28, 0.58, abs(v_uv.x)) *
                         (1.0 - smoothstep(0.58, 0.91, abs(v_uv.x)));
        float centre = 1.0 - smoothstep(0.0, 0.78, abs(v_uv.x));
        coverage = (0.62 * centre + 0.88 * shoulder) * ends;
        // The track system starts once there is enough snow to compress. Do
        // not make that first useful groove nearly transparent just because
        // the global cover value is still low.
        coverage *= clamp(u_snow_cover * 3.0, 0.45, 1.0);
    } else {
        // New accumulation buries old rubber and dirt instead of leaving black
        // lines magically painted over a fresh snowfall.
        coverage *= 1.0 - clamp(u_snow_cover, 0.0, 1.0);
    }

    float alpha = v_alpha * coverage;
    if (alpha <= 0.002) discard;
    vec3 lit = apply_lighting(v_color, v_normal, v_world_pos,
                              snow ? 0.18 : 0.05);
    frag_color = vec4(apply_fog(lit, v_world_pos), alpha);
}
