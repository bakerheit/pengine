#version 330 core

// Fullscreen triangle from gl_VertexID alone — no vertex buffer, no attributes.
// Three vertices, not the usual two-triangle quad: a quad has a diagonal seam
// where the two triangles meet, and fragments on it get shaded twice.
//
//   id 0 -> (-1,-1)    id 1 -> (3,-1)    id 2 -> (-1,3)
//
// The oversized corners fall outside the viewport and are clipped for free.
// z = 1 puts it on the far plane so anything opaque drawn afterwards wins.

out vec2 v_ndc;

void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    v_ndc       = p;
    gl_Position = vec4(p, 1.0, 1.0);
}
