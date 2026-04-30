#version 330 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_tangent;

uniform mat4 u_model;
uniform mat4 u_view_proj;
uniform mat3 u_normal_mat;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;

void main() {
    vec4 world_pos  = u_model * vec4(a_pos, 1.0);
    v_world_pos     = world_pos.xyz;
    v_normal        = normalize(u_normal_mat * a_normal);
    v_uv            = a_uv;
    gl_Position     = u_view_proj * world_pos;
}
