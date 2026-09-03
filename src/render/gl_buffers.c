#include "render/gl_buffers.h"

#include <GLFW/glfw3.h>

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#endif

typedef ptrdiff_t GLBufferSizeiptr;
typedef void (APIENTRY *GenBuffersFn)(GLsizei, GLuint*);
typedef void (APIENTRY *BindBufferFn)(GLenum, GLuint);
typedef void (APIENTRY *BufferDataFn)(GLenum, GLBufferSizeiptr, const void*, GLenum);
typedef void (APIENTRY *DeleteBuffersFn)(GLsizei, const GLuint*);

static GenBuffersFn p_glGenBuffers;
static BindBufferFn p_glBindBuffer;
static BufferDataFn p_glBufferData;
static DeleteBuffersFn p_glDeleteBuffers;
static int g_available;
static int g_resolved;

void gl_buffers_init(void) {
    if (g_resolved) return;
    g_resolved = 1;
    p_glGenBuffers = (GenBuffersFn)glfwGetProcAddress("glGenBuffers");
    p_glBindBuffer = (BindBufferFn)glfwGetProcAddress("glBindBuffer");
    p_glBufferData = (BufferDataFn)glfwGetProcAddress("glBufferData");
    p_glDeleteBuffers = (DeleteBuffersFn)glfwGetProcAddress("glDeleteBuffers");
    g_available = p_glGenBuffers && p_glBindBuffer && p_glBufferData &&
                  p_glDeleteBuffers;
}

int gl_buffers_available(void) { return g_available; }

unsigned int gl_buffer_array_target(void) { return GL_ARRAY_BUFFER; }
unsigned int gl_buffer_index_target(void) { return GL_ELEMENT_ARRAY_BUFFER; }

/* Creation is one shape for both usages, since the only difference is the hint
   and whether there are bytes to seed it with. */
static unsigned int create(unsigned int target, const void* data, size_t bytes,
                           unsigned int usage) {
    if (!g_available || bytes == 0) return 0;
    GLuint name = 0;
    p_glGenBuffers(1, &name);
    if (!name) return 0;
    p_glBindBuffer(target, name);
    p_glBufferData(target, (GLBufferSizeiptr)bytes, data, usage);
    p_glBindBuffer(target, 0);
    return name;
}

unsigned int gl_buffer_create_static(unsigned int target, const void* data,
                                     size_t bytes) {
    return create(target, data, bytes, GL_STATIC_DRAW);
}

unsigned int gl_buffer_create_stream(unsigned int target, size_t bytes) {
    return create(target, NULL, bytes, GL_STREAM_DRAW);
}

void gl_buffer_stream(unsigned int target, unsigned int name, const void* data,
                      size_t bytes) {
    if (!g_available || !name) return;
    p_glBindBuffer(target, name);
    p_glBufferData(target, (GLBufferSizeiptr)bytes, data, GL_STREAM_DRAW);
}

void gl_buffer_bind(unsigned int target, unsigned int name) {
    if (!g_available) return;
    p_glBindBuffer(target, name);
}

void gl_buffer_delete(unsigned int* name) {
    if (!name || !*name) return;
    if (g_available) p_glDeleteBuffers(1, name);
    *name = 0;
}
