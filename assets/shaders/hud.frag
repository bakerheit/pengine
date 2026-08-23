#version 330 core

// ONE draw call for the entire HUD.
//
// There is no mode uniform and no branch. Solid panels point their UVs at a
// block of the glyph atlas that is deliberately filled with 1.0, so a filled
// rectangle and a letter are the same operation: per-vertex colour times atlas
// coverage. A mode uniform would mean a flush every time the HUD alternated
// between a panel and a label, which is every single widget.
in vec2 v_uv;
in vec4 v_color;

uniform sampler2D u_atlas;   // R8 coverage

out vec4 frag_color;

void main() {
    float coverage = texture(u_atlas, v_uv).r;
    float a = v_color.a * coverage;
    if (a <= 0.0) discard;   // keeps the depth-less overlay from writing dead fragments
    frag_color = vec4(v_color.rgb, a);
}
