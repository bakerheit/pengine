#version 330 core

// Each vertex is one live particle. Drawn as GL_POINTS; the fragment
// shader expands gl_PointCoord into a soft round sprite.
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color; // rgba (alpha = remaining life 0..1)
layout(location = 2) in float a_size; // pixel size at 1 m from camera

uniform mat4  u_view_proj;
uniform vec3  u_cam_pos;
uniform float u_size_scale; // multiplies a_size, accounts for viewport DPI

out vec4 v_color;

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);

    // Distance attenuation so sparks shrink with depth instead of
    // staying a fixed pixel size when they're 100 m away.
    float dist = max(0.5, distance(a_pos, u_cam_pos));
    gl_PointSize = max(1.0, a_size * u_size_scale / dist);

    v_color = a_color;
}
