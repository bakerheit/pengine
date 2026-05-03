#version 330 core

// Minimap fragment shader. Discards pixels outside the unit circle (the
// minimap rim) so geometry can be authored as full quads and clip naturally.
// A thin border ring is drawn near the rim to give the minimap an edge.
in  vec2 v_local;
in  vec3 v_color;
out vec4 frag;

uniform float u_clip_radius;  // hard discard outside this radius
uniform float u_rim_radius;   // start darkening at this radius (rim border)

void main() {
    float r = length(v_local);
    if (r > u_clip_radius) discard;

    // Darken near the rim to read as a border. Compass labels (drawn in a
    // second pass with u_rim_radius set very large) skip this entirely.
    float rim_t = smoothstep(u_rim_radius - 0.05, u_rim_radius, r);
    vec3  col   = mix(v_color, vec3(0.05, 0.05, 0.07), rim_t);
    frag = vec4(col, 1.0);
}
