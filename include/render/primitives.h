#ifndef RENDER_PRIMITIVES_H
#define RENDER_PRIMITIVES_H

/* Immediate-mode building blocks shared by everything that draws in 3D. These
   are the only place raw cube geometry is emitted, so a future buffer-backed
   renderer replaces this file rather than every caller. */

/* Unit cube centred on the origin, spanning -1..1 with outward normals. */
void primitives_draw_cube(void);

/* Axis-aligned box of the given full extents, centred on the origin. */
void primitives_draw_box(float width, float height, float depth);

#endif
