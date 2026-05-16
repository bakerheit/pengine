#version 330 core

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_atlas;
uniform vec4      u_color;
uniform int       u_mode;  // 0 = sample atlas (glyphs); 1 = solid fill (backplate)

void main() {
    if (u_mode == 1) {
        frag_color = u_color;
    } else {
        // Atlas stores coverage in the red channel (R8). Output = colour × alpha.
        float a = texture(u_atlas, v_uv).r;
        if (a <= 0.0) discard;
        frag_color = vec4(u_color.rgb, a * u_color.a);
    }
}
