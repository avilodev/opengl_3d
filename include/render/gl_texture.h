#ifndef RENDER_GL_TEXTURE_H
#define RENDER_GL_TEXTURE_H

/* Getting decoded pixels onto the GPU, in one place because both model readers
   need it: a prop's base colour map and a character's skin are the same problem
   once the JSON is behind them.

   Needs a current GL context, which is why this is not in the loaders - those
   run headlessly, and the decode/upload split is what lets them. */

/* Uploads an RGB image with a full mip chain, returning the GL name, or 0.
   The caller still owns `pixels` and can free them as soon as this returns. */
unsigned int gl_texture_upload_rgb(const unsigned char* pixels, int width, int height);

#endif
