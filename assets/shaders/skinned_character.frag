#version 330 core

#include "lighting.glsl"

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;

uniform sampler2D u_diffuse;
uniform vec4 u_tint;

out vec4 frag_color;

void main() {
    vec4 source = texture(u_diffuse, v_uv);
    if (source.a < 0.05) discard;
    vec3 albedo = source.rgb * u_tint.rgb;
    vec3 lit = apply_lighting(albedo, v_normal, v_world_pos);
    frag_color = vec4(apply_fog(lit, v_world_pos), source.a * u_tint.a);
}
