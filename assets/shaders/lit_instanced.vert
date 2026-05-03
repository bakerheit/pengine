#version 330 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_tangent;
// Per-instance model matrix. mat4 occupies four consecutive locations.
layout(location = 4) in mat4 a_inst_model;

uniform mat4 u_view_proj;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;

void main() {
    vec4 world_pos = a_inst_model * vec4(a_pos, 1.0);
    v_world_pos    = world_pos.xyz;
    // Wheels (the only consumer today) are rotated + translated only — uniform
    // scale, no skew — so mat3(model) is the correct normal matrix and we can
    // skip the per-vertex inverseTranspose.
    v_normal       = normalize(mat3(a_inst_model) * a_normal);
    v_uv           = a_uv;
    gl_Position    = u_view_proj * world_pos;
}
