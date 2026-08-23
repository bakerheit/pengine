#version 330 core

// Opaque world geometry. All of the actual lighting lives in the shared
// include, so this file is the material and nothing else.
#include "lighting.glsl"

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_tint;

uniform sampler2D u_diffuse;

out vec4 frag_color;

void main() {
    vec3 albedo = texture(u_diffuse, v_uv).rgb * v_tint.rgb;
    vec3 lit    = apply_lighting(albedo, v_normal, v_world_pos);
    frag_color  = vec4(apply_fog(lit, v_world_pos), v_tint.a);
}
