#version 330 core

// Minimap vertex shader. Inputs are in minimap-local coordinates where the
// circle has radius 1 and (0,0) is the centre. The shader places the local
// quad onto the screen at u_screen_centre_px with u_radius_px.
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec3 a_color;

uniform vec2  u_screen_centre_px;
uniform float u_radius_px;
uniform vec2  u_viewport_px;

out vec2 v_local;
out vec3 v_color;

void main() {
    // a_pos uses minimap-local coords with +Y up (the natural orientation
    // for "north on top"). Pixel space has +Y down, so flip when computing
    // the on-screen position.
    vec2 pixel = u_screen_centre_px + vec2(a_pos.x, -a_pos.y) * u_radius_px;
    vec2 ndc   = pixel / u_viewport_px * 2.0 - 1.0;
    ndc.y      = -ndc.y;  // pixel space top-left → NDC bottom-left
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_local = a_pos;
    v_color = a_color;
}
