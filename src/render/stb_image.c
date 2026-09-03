/* stb_image's one translation unit.

   Only PNG is compiled in, and only the from-memory entry points: every image
   this project loads arrives as a buffer inside a .glb, never as a path. That
   drops the decoder to a few kilobytes and keeps a malformed JPEG out of a
   file format that cannot contain one.

   Third-party code, so the project's warnings are turned off across it rather
   than the file being edited to satisfy them - a patched vendor file is a
   patch that has to be reapplied at the next update. */

#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-function"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR

#include "utils/stb_image.h"
