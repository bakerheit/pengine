#pragma once

#include <glad/gl.h>

namespace apricot::gl_state {

// THE ONLY PLACE IN THE ENGINE THAT BINDS.
//
// A redundant glBindTexture / glUseProgram / glBindVertexArray is a driver
// round trip for nothing, and a frame that draws a thousand batches does a lot
// of nothing. This is a cache: bind through here and repeats are dropped.
//
// THE TRAP, and it costs a day every time somebody rediscovers it: GL object
// ids are RECYCLED. Windows drivers hand a freshly deleted id straight back to
// the next glGen*, and the cache then sees "id 7 is already bound", skips the
// bind, and the new texture never arrives — you get black textures on PC while
// macOS, which recycles ids lazily, looks perfect.
//
// So: EVERY glDelete* in this engine must be paired with the matching
// on_*_deleted() call below. There is no way to detect the mistake from in
// here, and it will not reproduce on the machine you develop on.

// Bind if not already bound. `unit` is the texture unit index, not the
// GL_TEXTURE0 enum.
void bind_texture(GLuint unit, GLuint texture);
void use_program(GLuint program);
void bind_vertex_array(GLuint vao);
void bind_buffer(GLenum target, GLuint buffer);

// Invalidate cached state for a deleted object. Call these IMMEDIATELY after
// the corresponding glDelete*, before any chance of the id being reissued.
void on_texture_deleted(GLuint texture);
void on_program_deleted(GLuint program);
void on_vertex_array_deleted(GLuint vao);
void on_buffer_deleted(GLuint buffer);

// Forget everything. Use after any third-party code (the debug UI backend,
// a capture tool) has bound things behind the cache's back, and on context
// recreation.
void invalidate_all();

// Redundant binds skipped since the last reset. Dev instrumentation only.
unsigned int skipped_binds();
void reset_counters();

}  // namespace apricot::gl_state
