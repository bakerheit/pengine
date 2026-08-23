#version 330 core

// Screen-space HUD. Pixel coordinates, origin TOP-LEFT, +Y down — the same
// convention the glyph layout uses, so a caller never has to flip anything.
layout(location = 0) in vec2 a_pos_px;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

uniform vec2 u_viewport_px;

out vec2 v_uv;
out vec4 v_color;

void main() {
    vec2 ndc = (a_pos_px / u_viewport_px) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv    = a_uv;
    v_color = a_color;
}
