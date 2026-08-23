#version 330 core

// Rain streaks. Every quad is built in world space CPU-side (gfx/precip.cpp),
// so the vertex stage is a plain transform and the whole field is one draw.
layout(location = 0) in vec3  a_pos;
layout(location = 1) in vec2  a_uv;     // x: across the width [-1,1]; y: head 0 -> tail 1
layout(location = 2) in float a_alpha;  // per-streak base alpha

uniform mat4 u_view_proj;

out vec2  v_uv;
out float v_alpha;
out vec3  v_world_pos;

void main() {
    v_uv        = a_uv;
    v_alpha     = a_alpha;
    v_world_pos = a_pos;
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
}
