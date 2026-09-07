#version 330 core

#include "lighting.glsl"

// Straight alpha blend, NOT additive. Rain stays a faint streak while snow gets
// a broad, readable flake silhouette instead of looking like pale rain.
in vec2  v_uv;
in float v_alpha;
in vec3  v_world_pos;

uniform vec3  u_color;
uniform float u_opacity;   // whole-field scale; 0 draws nothing
uniform int u_type;        // 0 rain, 1 snow, 2 blizzard

out vec4 frag_color;

void main() {
    float coverage;
    if (u_type == 0) {
        float edge  = smoothstep(0.0, 1.0, 1.0 - abs(v_uv.x));
        float along = 1.0 - smoothstep(0.5, 1.0, v_uv.y);
        coverage = edge * along;
    } else {
        float radius = length(v_uv);
        float core = 1.0 - smoothstep(0.18, 0.42, radius);
        float plus_dist = min(abs(v_uv.x), abs(v_uv.y));
        float diag_dist = min(abs(v_uv.x - v_uv.y), abs(v_uv.x + v_uv.y)) * 0.7071;
        float arms = 1.0 - smoothstep(0.09, 0.20, min(plus_dist, diag_dist));
        arms *= 1.0 - smoothstep(0.58, 1.0, radius);
        coverage = max(core, arms);
    }

    float a = v_alpha * u_opacity * coverage;

    if (u_fog_end > u_fog_start && u_fog_density > 0.0) {
        float d = length(u_cam_pos - v_world_pos);
        float f = clamp((d - u_fog_start) / (u_fog_end - u_fog_start), 0.0, 1.0);
        f = f * f * (3.0 - 2.0 * f);
        a *= 1.0 - f * u_fog_density;
    }

    if (a <= 0.0) discard;

    vec3 color = u_color;
    if (u_type != 0) {
        // Camera-facing flakes use that face as their light normal. Car,
        // traffic and canopy spots now catch the flakes inside their cones,
        // while snow outside a night-time beam stays properly dark.
        vec3 to_camera = u_cam_pos - v_world_pos;
        float camera_distance = length(to_camera);
        vec3 flake_normal = camera_distance > 0.001
            ? to_camera / camera_distance
            : vec3(0.0, 1.0, 0.0);
        color = apply_lighting(color, flake_normal, v_world_pos, 0.12);
    }
    frag_color = vec4(color, a);
}
