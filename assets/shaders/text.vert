#version 330 core

layout(location = 0) in vec2 a_pos_px;
layout(location = 1) in vec2 a_uv;

uniform vec2 u_viewport_px;

out vec2 v_uv;

void main() {
    // Pixel coords (origin top-left, +Y down) → NDC.
    vec2 ndc = (a_pos_px / u_viewport_px) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = a_uv;
}
