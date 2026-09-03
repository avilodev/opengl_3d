#include "render/static_model.h"
#include "render/gl_texture.h"
#include "render/gl_buffers.h"
#include "utils/stb_image.h"

#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>

/* Whether a part's material is in a list. NULL never matches, which is what
   lets one test serve both filters: a hidden list of NULL hides nothing, and an
   only list of NULL keeps nothing. */
static int named(const StaticMesh* mesh, const char* const* list) {
    if (!list) return 0;
    for (int i = 0; list[i]; ++i)
        if (strcmp(mesh->material, list[i]) == 0) return 1;
    return 0;
}

/* The one rule both draws obey: skip a part named in `hidden`, and when `only`
   is given, skip everything NOT named in it. */
static int is_skipped(const StaticMesh* mesh, const char* const* hidden,
                      const char* const* only) {
    if (named(mesh, hidden)) return 1;
    if (only && !named(mesh, only)) return 1;
    return 0;
}


/* Putting a prop on the screen. Vertex arrays for the same reason the rig uses
   them: twenty-one thousand triangles through glVertex3f would be the frame.

   Those arrays live on the card when the driver has buffer objects. A prop is
   the clearest case for it there is - the rifle is twenty-five thousand
   vertices that have not changed since they were read out of the file, and
   drawing it from client memory handed the driver eight hundred kilobytes to
   copy on every frame it was on screen, twice over whenever the body was
   carrying one and the viewmodel was up. Uploaded once, the same draw costs a
   bind. Without buffer objects the client-side arrays are still there and the
   draw is what it always was. */

/* Packs one mesh's separate arrays into the interleaved layout the card
   reads, and hands it over. Leaves the buffers at 0 on any failure, which the
   draw reads as "use the client arrays". */
static void upload_geometry(StaticMesh* mesh) {
    if (mesh->vertex_buffer || !mesh->positions || !mesh->normals) return;
    if (!gl_buffers_available() || mesh->vertex_count <= 0) return;

    mesh->vertex_stride = mesh->uvs ? 8 : 6;
    size_t floats = (size_t)mesh->vertex_count * (size_t)mesh->vertex_stride;
    float* packed = malloc(floats * sizeof *packed);
    /* A prop that cannot be packed is not worth refusing to draw. */
    if (!packed) return;

    for (int v = 0; v < mesh->vertex_count; ++v) {
        float* out = packed + (size_t)v * (size_t)mesh->vertex_stride;
        memcpy(out, mesh->positions + (size_t)v * 3, sizeof(float) * 3);
        memcpy(out + 3, mesh->normals + (size_t)v * 3, sizeof(float) * 3);
        if (mesh->uvs) memcpy(out + 6, mesh->uvs + (size_t)v * 2, sizeof(float) * 2);
    }

    mesh->vertex_buffer = gl_buffer_create_static(gl_buffer_array_target(),
                                                  packed, floats * sizeof *packed);
    free(packed);
    if (!mesh->vertex_buffer) return;

    mesh->index_buffer = gl_buffer_create_static(
        gl_buffer_index_target(), mesh->indices,
        (size_t)mesh->index_count * sizeof *mesh->indices);
    /* Vertices on the card and indices not is a state the draw has no path
       for, so give the whole mesh back to the client arrays. */
    if (!mesh->index_buffer) gl_buffer_delete(&mesh->vertex_buffer);
}

void static_model_upload(StaticModel* model) {
    if (!model) return;
    gl_buffers_init();
    for (int i = 0; i < model->mesh_count; ++i) {
        StaticMesh* mesh = &model->meshes[i];
        upload_geometry(mesh);
        if (!mesh->image || mesh->texture) continue;
        mesh->texture = gl_texture_upload_rgb(mesh->image, mesh->image_width,
                                              mesh->image_height);
        /* The pixels live on the GPU now. Holding the copy would be four
           megabytes of nothing for the rest of the session. */
        stbi_image_free(mesh->image);
        mesh->image = NULL;
    }
}

void static_model_free(StaticModel* model) {
    if (!model) return;
    for (int i = 0; i < STATIC_MAX_MESHES; ++i) {
        StaticMesh* mesh = &model->meshes[i];
        free(mesh->positions);
        free(mesh->normals);
        free(mesh->uvs);
        free(mesh->indices);
        if (mesh->image) stbi_image_free(mesh->image);
        if (mesh->texture) glDeleteTextures(1, &mesh->texture);
        gl_buffer_delete(&mesh->vertex_buffer);
        gl_buffer_delete(&mesh->index_buffer);
    }
    memset(model, 0, sizeof *model);
}

/* Points GL at one mesh's vertices and draws them.

   On the card, the gl*Pointer calls take byte offsets into the bound buffer
   where they would otherwise take addresses, and the draw takes an offset in
   place of the index array. Off it, the same calls take the load-time arrays.
   The two branches differ only in what those arguments mean, which is why the
   shape below is shared. */
static void draw_mesh(const StaticMesh* mesh, int texturing) {
    if (mesh->vertex_buffer) {
        size_t stride = (size_t)mesh->vertex_stride * sizeof(float);
        gl_buffer_bind(gl_buffer_array_target(), mesh->vertex_buffer);
        gl_buffer_bind(gl_buffer_index_target(), mesh->index_buffer);
        glVertexPointer(3, GL_FLOAT, (GLsizei)stride, GL_BUFFER_OFFSET(0));
        glNormalPointer(GL_FLOAT, (GLsizei)stride,
                        GL_BUFFER_OFFSET(3 * sizeof(float)));
        if (texturing)
            glTexCoordPointer(2, GL_FLOAT, (GLsizei)stride,
                              GL_BUFFER_OFFSET(6 * sizeof(float)));
        glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT,
                       GL_BUFFER_OFFSET(0));
        gl_buffer_bind(gl_buffer_index_target(), 0);
        gl_buffer_bind(gl_buffer_array_target(), 0);
    } else {
        if (texturing) glTexCoordPointer(2, GL_FLOAT, 0, mesh->uvs);
        glVertexPointer(3, GL_FLOAT, 0, mesh->positions);
        glNormalPointer(GL_FLOAT, 0, mesh->normals);
        glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT,
                       mesh->indices);
    }
}

/* One pass over the model, drawing either the solid materials or the
   translucent ones. Returns whether it left texturing on, because the caller
   owns turning it back off. */
static int draw_pass(const StaticModel* model, const char* const* hidden,
                     const char* const* only, int translucent, int texturing) {
    for (int i = 0; i < model->mesh_count; ++i) {
        const StaticMesh* mesh = &model->meshes[i];
        if (!mesh->positions || !mesh->indices) continue;
        if (is_skipped(mesh, hidden, only)) continue;
        if ((mesh->opacity < 1.0f) != (translucent != 0)) continue;

        int wants_texture = mesh->texture && mesh->uvs;
        if (wants_texture != texturing) {
            texturing = wants_texture;
            if (texturing) {
                glEnable(GL_TEXTURE_2D);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            } else {
                glDisable(GL_TEXTURE_2D);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            }
        }
        if (texturing) glBindTexture(GL_TEXTURE_2D, mesh->texture);

        /* GL_MODULATE, which is the default, multiplies the texture by the lit
           vertex colour. A textured material's colour is white, so the map
           comes through as authored; an untextured one has no map to dilute
           and its colour is the whole material. */
        glColor4f(mesh->base_color[0], mesh->base_color[1],
                  mesh->base_color[2], mesh->opacity);
        draw_mesh(mesh, texturing);
    }
    return texturing;
}

static int has_translucent(const StaticModel* model, const char* const* hidden,
                           const char* const* only) {
    for (int i = 0; i < model->mesh_count; ++i)
        if (model->meshes[i].opacity < 1.0f &&
            !is_skipped(&model->meshes[i], hidden, only))
            return 1;
    return 0;
}

int static_model_draw_translucent_except(const StaticModel* model,
                                         const char* const* hidden) {
    if (!model || model->mesh_count == 0) return 0;
    if (!has_translucent(model, hidden, NULL)) return 0;

    /* No material state is touched: the caller wants the shape, and a mask has
       no colour. Culling off because a window is one surface and has no
       inside - the same reason the blended pass turns it off. */
    GLboolean was_culling = glIsEnabled(GL_CULL_FACE);
    glDisable(GL_CULL_FACE);
    glEnableClientState(GL_VERTEX_ARRAY);
    for (int i = 0; i < model->mesh_count; ++i) {
        const StaticMesh* mesh = &model->meshes[i];
        if (!mesh->positions || !mesh->indices) continue;
        if (is_skipped(mesh, hidden, NULL)) continue;
        if (mesh->opacity >= 1.0f) continue;
        if (mesh->vertex_buffer && mesh->index_buffer) {
            size_t stride = (size_t)mesh->vertex_stride * sizeof(float);
            gl_buffer_bind(gl_buffer_array_target(), mesh->vertex_buffer);
            gl_buffer_bind(gl_buffer_index_target(), mesh->index_buffer);
            glVertexPointer(3, GL_FLOAT, (GLsizei)stride, GL_BUFFER_OFFSET(0));
            glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT,
                           GL_BUFFER_OFFSET(0));
            gl_buffer_bind(gl_buffer_index_target(), 0);
            gl_buffer_bind(gl_buffer_array_target(), 0);
        } else {
            glVertexPointer(3, GL_FLOAT, 0, mesh->positions);
            glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT,
                           mesh->indices);
        }
    }
    glDisableClientState(GL_VERTEX_ARRAY);
    if (was_culling) glEnable(GL_CULL_FACE);
    return 1;
}

static void draw_filtered(const StaticModel* model, const char* const* hidden,
                          const char* const* only) {
    if (!model || model->mesh_count == 0) return;

    GLboolean was_texturing = glIsEnabled(GL_TEXTURE_2D);
    GLboolean was_normalizing = glIsEnabled(GL_NORMALIZE);

    /* The caller may have scaled the transform - a gun in a scaled rig's hand
       does - and a scaled normal lights the surface at the wrong brightness. */
    if (!was_normalizing) glEnable(GL_NORMALIZE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);

    int texturing = draw_pass(model, hidden, only, 0, 0);

    /* The glass, after everything solid, because blending is an operation on
       what is already in the buffer and there has to be something there. Depth
       writes off for the same reason the viewmodel's old glass had them off:
       a pane that wrote depth would occlude whatever it was drawn to reveal.
       Culling off because a window is one surface and has no inside. */
    if (has_translucent(model, hidden, only)) {
        GLboolean was_culling = glIsEnabled(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        texturing = draw_pass(model, hidden, only, 1, texturing);
        if (was_culling) glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    if (texturing) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (!was_normalizing) glDisable(GL_NORMALIZE);
    if (was_texturing) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
}

/* The three public draws, all of them one call into the filtered loop above.
   Written this way rather than as three copies because the pass has real state
   in it - texturing, blending, culling, depth - and three copies is three
   places for that to be got subtly wrong. */
void static_model_draw_except(const StaticModel* model,
                              const char* const* hidden) {
    draw_filtered(model, hidden, NULL);
}

void static_model_draw_only(const StaticModel* model, const char* const* only) {
    if (!only) return;
    draw_filtered(model, NULL, only);
}

void static_model_draw(const StaticModel* model) {
    draw_filtered(model, NULL, NULL);
}

int static_model_draw_translucent(const StaticModel* model) {
    return static_model_draw_translucent_except(model, NULL);
}
