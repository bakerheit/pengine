#version 330 core

// Straight alpha blend, NOT additive: rain is a faint translucent streak, not a
// glow. Additive rain over a bright sky is invisible and over a dark one looks
// like sparks.
in vec2  v_uv;
in float v_alpha;
in vec3  v_world_pos;

uniform vec3  u_color;
uniform float u_opacity;   // whole-field scale; 0 draws nothing

// Rain lives in the same world as everything else, so it fades into the same
// fog. Distance is measured from the camera position, matching lighting.glsl.
uniform vec3  u_cam_pos;
uniform float u_fog_start;
uniform float u_fog_end;
uniform float u_fog_density;

out vec4 frag_color;

void main() {
    // Soft across the width, tapering along the length, so a streak fades out
    // instead of ending in a hard rectangle.
    float edge  = smoothstep(0.0, 1.0, 1.0 - abs(v_uv.x));
    float along = 1.0 - smoothstep(0.5, 1.0, v_uv.y);

    float a = v_alpha * u_opacity * edge * along;

    if (u_fog_end > u_fog_start && u_fog_density > 0.0) {
        float d = length(u_cam_pos - v_world_pos);
        float f = clamp((d - u_fog_start) / (u_fog_end - u_fog_start), 0.0, 1.0);
        a *= 1.0 - f * u_fog_density;
    }

    if (a <= 0.0) discard;
    frag_color = vec4(u_color, a);
}
