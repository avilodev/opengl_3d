#include "render/gl_texture.h"

#include <GLFW/glfw3.h>
#include <stdlib.h>

/* Halves an RGB image, writing the smaller one into `out`. A box filter is the
   whole of it: GL 1.1 has no glGenerateMipmap, and a gun that fills a tenth of
   the screen sampling a 1024-pixel map without mip levels crawls with aliasing
   on every rail tooth. A character across the map is the same problem wearing
   different clothes. */
static void halve(const unsigned char* in, int width, int height, unsigned char* out) {
    int half_width = width > 1 ? width / 2 : 1;
    int half_height = height > 1 ? height / 2 : 1;
    for (int y = 0; y < half_height; ++y) {
        for (int x = 0; x < half_width; ++x) {
            for (int c = 0; c < 3; ++c) {
                int x0 = x * 2, y0 = y * 2;
                int x1 = width > 1 ? x0 + 1 : x0;
                int y1 = height > 1 ? y0 + 1 : y0;
                int sum = in[(y0 * width + x0) * 3 + c] + in[(y0 * width + x1) * 3 + c] +
                          in[(y1 * width + x0) * 3 + c] + in[(y1 * width + x1) * 3 + c];
                out[(y * half_width + x) * 3 + c] = (unsigned char)(sum / 4);
            }
        }
    }
}

unsigned int gl_texture_upload_rgb(const unsigned char* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) return 0;

    unsigned int name = 0;
    glGenTextures(1, &name);
    if (!name) return 0;
    glBindTexture(GL_TEXTURE_2D, name);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, pixels);

    /* Two buffers, ping-ponged, each big enough for the largest level either
       will hold. A chain that could not be allocated is not a failure worth
       refusing the model over - it costs the mip levels, so the filter is
       dropped back to one GL will accept without them. */
    size_t bytes = (size_t)width * (size_t)height * 3;
    unsigned char* buffers[2] = {malloc(bytes), malloc(bytes)};
    if (!buffers[0] || !buffers[1]) {
        free(buffers[0]);
        free(buffers[1]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        return name;
    }

    const unsigned char* source = pixels;
    int level = 0;
    while (width > 1 || height > 1) {
        unsigned char* destination = buffers[level % 2];
        halve(source, width, height, destination);
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        glTexImage2D(GL_TEXTURE_2D, ++level, GL_RGB, width, height, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, destination);
        source = destination;
    }

    free(buffers[0]);
    free(buffers[1]);
    return name;
}
