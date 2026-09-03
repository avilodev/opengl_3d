#ifndef RENDER_STATIC_MODEL_H
#define RENDER_STATIC_MODEL_H

/* A prop: triangles, a material colour, and at most one texture per material.
   No skeleton, no clips, no per-frame work - the vertices that come out of the
   file are the vertices that get drawn.

   This is the other half of what glb.c was written to serve. A rigged
   character needs joints, weights and an animation graph; a rifle in the
   player's hands needs none of that, and paying for a Pose and eight thousand
   vertex transforms a frame to draw something rigid would be silly.

   Loading and uploading are separate calls because only the second needs a GL
   context. The loader can be run headlessly, which is where a bad accessor
   index is cheapest to find. */

#define STATIC_MAX_MESHES 16

/* Long enough for the names an exporter actually writes; a longer one is
   truncated rather than refused, since the name is only ever compared. */
#define STATIC_MATERIAL_NAME 48

typedef struct {
    /* The material's name from the file, which is the only handle a caller has
       on WHICH part of a prop this is. A glTF mesh arrives split one primitive
       per material, so a model's material names are its parts list - and the
       one thing that survives a re-export, unlike an index. */
    char material[STATIC_MATERIAL_NAME];
    float* positions;                /* 3 per vertex */
    float* normals;                  /* 3 per vertex */
    float* uvs;                      /* 2 per vertex, NULL when untextured */
    unsigned int* indices;
    int vertex_count;
    int index_count;

    float base_color[3];
    /* Under one, the material is glass: drawn in a second pass, blended, with
       depth writes off. One optic's window is the whole reason this exists,
       and it is reason enough - an opaque pane in front of a sight is not a
       fidelity loss, it is the sight not working. */
    float opacity;
    /* The material's base colour map, decoded but not yet on the GPU. Freed by
       static_model_upload once it is; NULL means this material draws flat. */
    unsigned char* image;
    int image_width;
    int image_height;
    unsigned int texture;            /* GL name, 0 until uploaded */

    /* The same triangles, on the card. A prop's vertices never change, so
       there is no reason to hand them across the bus again every frame: the
       arrays above are the load-time copy and these are what the draw reads.
       Both are 0 when the driver has no buffer objects, which is what puts the
       draw back on the client-side arrays. Positions, normals and UVs are
       interleaved into the one buffer, because a vertex is fetched as a unit
       and three separate streams make the card chase three cache lines for
       it. */
    unsigned int vertex_buffer;
    unsigned int index_buffer;
    int vertex_stride;               /* floats per vertex: 6, or 8 with UVs */
} StaticMesh;

typedef struct {
    StaticMesh meshes[STATIC_MAX_MESHES];
    int mesh_count;
    float min[3], max[3];            /* bounding box, in the file's own units */
} StaticModel;

/* Reads a binary glTF holding one static mesh. Non-zero on success; on failure
   the model is left zeroed and `error` carries a reason worth printing.

   Node transforms are ignored: this expects a file whose vertices are already
   in the space the caller wants to draw them in, which is what
   tools/fbx_to_glb.py produces. A file with a transformed node would draw its
   parts in the wrong places, so one is refused rather than mis-drawn. */
int static_model_load_glb(StaticModel* model, const char* path,
                          char* error, int error_size);

/* Hands the decoded images and the geometry to GL. Needs a current context;
   call once, after loading. Safe to call on a model with no textures, and on
   one whose driver has no buffer objects - that keeps the client-side arrays
   and draws exactly as it did before. */
void static_model_upload(StaticModel* model);

void static_model_free(StaticModel* model);

/* The same two passes, skipping any part whose material is named in `hidden` -
   a NULL-terminated list of names, or NULL for all of it.

   A FILTER rather than a flag on the model, because models are cached and
   shared by path: two weapons naming the same file get the same StaticModel,
   and a hidden bit set by whichever loaded first would silently take the part
   off the other one too. Passing the list at the draw keeps the decision with
   the caller that owns it.

   Parts, not triangles: a glTF mesh arrives split one primitive per material,
   so a material name is the finest cut available without editing the asset, and
   a model's material names are exactly its parts list.

   Both return what the unfiltered versions do. */
void static_model_draw_except(const StaticModel* model,
                              const char* const* hidden);
int static_model_draw_translucent_except(const StaticModel* model,
                                         const char* const* hidden);

/* The complement: draws ONLY the parts named in `only`, and nothing else. What
   this is for is a part that has to move independently of the rest of the prop
   - a magazine leaving the weapon it is drawn from. The caller draws the gun
   without it, pushes a second transform, and draws it here.

   `only` NULL draws nothing, which is the useful reading: a caller with no
   parts to single out has none to draw. */
void static_model_draw_only(const StaticModel* model, const char* const* only);

/* Draws the model about its own origin, with the current transform. Solid
   materials first and translucent ones after, so the glass has something
   behind it to blend against. Leaves texturing, culling, blending and lighting
   as it found them. */
void static_model_draw(const StaticModel* model);

/* Just the translucent materials, geometry only - no texturing, no colour, no
   blending, and whatever the caller has set for depth and stencil left alone.
   Non-zero when there was any to draw.

   This exists so a caller can use the glass as a shape rather than as a
   surface: stencilled, one optic's window becomes the aperture its reticle is
   seen through. Drawing the whole model for that would mask to the entire
   sight, hood included, which is the opposite of a window. */
int static_model_draw_translucent(const StaticModel* model);

#endif
