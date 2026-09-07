#version 330 core

// Fullscreen triangle. The texture coordinates use the same bottom-left GL
// convention as Texture::load_file(), which flips source PNG rows on upload.
out vec2 v_uv;

void main() {
    const vec2 positions[3] = vec2[3](
        vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    const vec2 uvs[3] = vec2[3](
        vec2(0.0, 0.0), vec2(2.0, 0.0), vec2(0.0, 2.0));
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    v_uv = uvs[gl_VertexID];
}
