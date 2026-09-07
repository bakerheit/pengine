#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_color;
layout(location = 4) in float a_alpha;
layout(location = 5) in float a_surface;

uniform mat4 u_view_proj;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_color;
out float v_alpha;
flat out float v_surface;

void main() {
    v_world_pos = a_position;
    v_normal = a_normal;
    v_uv = a_uv;
    v_color = a_color;
    v_alpha = a_alpha;
    v_surface = a_surface;
    gl_Position = u_view_proj * vec4(a_position, 1.0);
}
