#ifndef RENDER_GL_BUFFERS_H
#define RENDER_GL_BUFFERS_H

#include <stddef.h>

/* Vertex buffer objects, resolved at runtime.

   Buffers are GL 1.5, and the context this engine asks for is 2.1, so on any
   real driver they are simply there. They are still resolved through
   glfwGetProcAddress rather than linked, because the import library the build
   links against is opengl32.dll's, which exports GL 1.1 and nothing more - the
   same reason renderer.c resolved them for its UI batcher, which is where this
   module was lifted from so the pointers live in one place instead of two.

   Every entry point is safe to call when resolution failed. Creation returns
   0, and a caller that gets 0 keeps whatever client-side path it had: a
   machine with no buffer objects loses the bus saving, not the frame. */

/* Resolves the entry points. Needs a current context; call once after the
   window is up, before any create. Repeat calls are free. */
void gl_buffers_init(void);

/* Non-zero when buffer objects are usable. Callers branch on this to decide
   whether a pointer they hand GL is an address or an offset. */
int gl_buffers_available(void);

/* GL's own enums, spelled here so no caller has to define them. */
unsigned int gl_buffer_array_target(void);
unsigned int gl_buffer_index_target(void);

/* Geometry that never changes: uploaded once and left on the card. 0 if
   buffers are unavailable or GL declined the name. */
unsigned int gl_buffer_create_static(unsigned int target, const void* data,
                                     size_t bytes);

/* Geometry rewritten every frame. Created empty; fill it with
   gl_buffer_stream. */
unsigned int gl_buffer_create_stream(unsigned int target, size_t bytes);

/* Replaces a stream buffer's contents. Passing the whole buffer rather than a
   sub-range is deliberate: it lets the driver hand back fresh storage instead
   of waiting for the last frame's draw to finish reading the old. */
void gl_buffer_stream(unsigned int target, unsigned int name, const void* data,
                      size_t bytes);

void gl_buffer_bind(unsigned int target, unsigned int name);

/* Deletes and zeroes. Needs a current context, and does nothing on a name of
   0, which is what a buffer-less build has everywhere. */
void gl_buffer_delete(unsigned int* name);

/* The offset form of a client pointer, for the gl*Pointer calls: with a buffer
   bound they take a byte offset in the same argument that otherwise takes an
   address. */
#define GL_BUFFER_OFFSET(bytes) ((const void*)(size_t)(bytes))

#endif
