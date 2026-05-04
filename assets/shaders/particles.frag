#version 330 core

in vec4 v_color;
out vec4 frag_color;

void main() {
    // Round soft sprite. Discard outside the unit disk so points don't
    // render as visible squares, and fade the edge for a subtle bloom-y
    // look against the additive blend.
    vec2 d = gl_PointCoord - vec2(0.5);
    float r2 = dot(d, d);
    if (r2 > 0.25) discard;
    float falloff = 1.0 - smoothstep(0.0, 0.25, r2);

    // Premultiplied additive: rgb * alpha * falloff, alpha used for the
    // additive contribution (we set blend func to GL_ONE / GL_ONE).
    vec3 rgb = v_color.rgb * v_color.a * falloff;
    frag_color = vec4(rgb, 1.0);
}
