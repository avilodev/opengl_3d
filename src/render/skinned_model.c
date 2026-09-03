#include "render/skinned_model.h"
#include "render/gl_texture.h"
#include "render/gl_buffers.h"
#include "utils/stb_image.h"

#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>

/* Drawing what skinning produced - the model for its materials, indices and
   UVs, the instance for its vertices. Vertex arrays rather than immediate
   mode: the rig is eight and a half thousand vertices, so a glVertex3f per
   corner would be the frame.

   Everything that can live on the card does. The split follows what changes:
   a vertex's indices and its place in the texture map are the same on every
   frame of every clip, so they are uploaded once with the model and shared by
   every body wearing it, while the positions and normals are the per-frame
   product of skinning and go up per body, per frame, through
   skinned_instance_upload. That last one still crosses the bus every frame -
   there is no way around it while skinning is on the CPU - but it is two
   hundred kilobytes into a buffer the driver can hand back unsynchronised,
   rather than a client array it has to copy out of behind the draw.

   A driver with no buffer objects gets none of this and draws from the
   client-side arrays exactly as before. */

void skinned_model_upload(SkinnedModel* model) {
    if (!model) return;
    gl_buffers_init();
    for (int i = 0; i < model->mesh_count; ++i) {
        SkinnedMesh* mesh = &model->meshes[i];

        /* Indices first: without them on the card the draw has to take the
           client-array path anyway, so a UV buffer beside a client index array
           would be a buffer nothing reads. */
        if (!mesh->index_buffer && mesh->indices && mesh->index_count > 0) {
            mesh->index_buffer = gl_buffer_create_static(
                gl_buffer_index_target(), mesh->indices,
                (size_t)mesh->index_count * sizeof *mesh->indices);
            if (mesh->index_buffer && mesh->uvs)
                mesh->uv_buffer = gl_buffer_create_static(
                    gl_buffer_array_target(), mesh->uvs,
                    (size_t)mesh->vertex_count * 2 * sizeof *mesh->uvs);
        }

        if (!mesh->image || mesh->texture) continue;
        mesh->texture = gl_texture_upload_rgb(mesh->image, mesh->image_width,
                                              mesh->image_height);
        /* The pixels live on the GPU now. Holding the copy would be several
           megabytes of nothing for the rest of the session. */
        stbi_image_free(mesh->image);
        mesh->image = NULL;
    }
}

void skinned_instance_upload(const SkinnedModel* model, SkinnedInstance* instance) {
    if (!model || !instance) return;
    int mesh_count = model->mesh_count < instance->mesh_count
        ? model->mesh_count : instance->mesh_count;
    for (int i = 0; i < mesh_count; ++i) {
        /* No index buffer means this mesh draws from client arrays, and a
           vertex buffer nothing binds is a megabyte of dead storage. */
        if (!model->meshes[i].index_buffer) continue;
        SkinnedMeshVertices* vertices = &instance->meshes[i];
        if (!vertices->vertices || vertices->vertex_count <= 0) continue;

        size_t bytes = (size_t)vertices->vertex_count * SKIN_VERTEX_FLOATS *
                       sizeof *vertices->vertices;
        if (!vertices->vertex_buffer) {
            vertices->vertex_buffer =
                gl_buffer_create_stream(gl_buffer_array_target(), bytes);
            /* Still 0 on a driver without buffers, and the draw reads that as
               the client-array path. Nothing to report. */
            if (!vertices->vertex_buffer) continue;
        }
        gl_buffer_stream(gl_buffer_array_target(), vertices->vertex_buffer,
                         vertices->vertices, bytes);
    }
    gl_buffer_bind(gl_buffer_array_target(), 0);
}

void skinned_instance_free_buffers(SkinnedInstance* instance) {
    if (!instance) return;
    for (int i = 0; i < SKIN_MAX_MESHES; ++i)
        gl_buffer_delete(&instance->meshes[i].vertex_buffer);
}

void skinned_model_free(SkinnedModel* model) {
    if (!model) return;
    for (int i = 0; i < model->mesh_count; ++i) {
        SkinnedMesh* mesh = &model->meshes[i];
        free(mesh->positions);
        free(mesh->normals);
        free(mesh->joints);
        free(mesh->weights);
        free(mesh->indices);
        free(mesh->uvs);
        if (mesh->image) stbi_image_free(mesh->image);
        if (mesh->texture) glDeleteTextures(1, &mesh->texture);
        gl_buffer_delete(&mesh->index_buffer);
        gl_buffer_delete(&mesh->uv_buffer);
    }
    for (int i = 0; i < model->clip_count; ++i) {
        AnimationClip* clip = &model->clips[i];
        for (int j = 0; j < clip->sampler_count; ++j) {
            free(clip->samplers[j].times);
            free(clip->samplers[j].values);
        }
        free(clip->samplers);
        free(clip->channels);
    }
    free(model->clips);
    memset(model, 0, sizeof *model);
}

/* Whether a material's doubleSided flag is allowed to turn culling off.

   It is not, and the flag is why. Blender's glTF exporter writes
   doubleSided:true on every material unless someone deliberately turns it off,
   so both of the mannequin's materials carry it and neither means anything by
   it - the body is a closed mesh whose back faces are behind its own front
   ones. Honouring it rasterised the whole character twice, twenty thousand
   triangles instead of ten, for no pixel anyone could see.

   The flag is still read by the loader, because it is what the file says. If a
   character ever wears something genuinely single-surfaced - a cape, a cloth
   strip, hair cards - this is the switch to turn back on, and the material to
   turn it on for should be the only one that has it. */
#define SKINNED_HONOUR_DOUBLE_SIDED 0

void skinned_model_draw(const SkinnedModel* model, const SkinnedInstance* instance) {
    if (!model || !instance || model->mesh_count == 0) return;

    GLboolean was_normalizing = glIsEnabled(GL_NORMALIZE);
    GLboolean was_texturing = glIsEnabled(GL_TEXTURE_2D);
#if SKINNED_HONOUR_DOUBLE_SIDED
    GLboolean was_culling = glIsEnabled(GL_CULL_FACE);
#endif

    /* The caller scales the rig to the height gameplay expects, and a scaled
       normal lights the surface at the wrong brightness. Skinning leaves unit
       normals behind; this covers the transform stack above them. */
    if (!was_normalizing) glEnable(GL_NORMALIZE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);

    int texturing = 0;
    for (int i = 0; i < model->mesh_count && i < instance->mesh_count; ++i) {
        const SkinnedMesh* mesh = &model->meshes[i];
        const SkinnedMeshVertices* vertices = &instance->meshes[i];
        if (!vertices->vertices || !mesh->indices) continue;

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

        /* All three streams have to agree on where they are read from: the
           draw's last argument is an offset into a bound buffer or an address,
           and there is no mixing the two. So a mesh takes the buffered path
           only if every stream it needs made it onto the card - a UV upload
           that failed while the indices succeeded puts the whole mesh back on
           client arrays rather than handing glTexCoordPointer a null. */
        int buffered = mesh->index_buffer && vertices->vertex_buffer &&
                       (!texturing || mesh->uv_buffer);

#if SKINNED_HONOUR_DOUBLE_SIDED
        int uncull = mesh->double_sided && was_culling;
        if (uncull) glDisable(GL_CULL_FACE);
#endif

        /* GL_MODULATE, which is the default, multiplies the texture by the lit
           vertex colour. A textured material's colour is white, so the map
           comes through as authored; an untextured one has no map to dilute
           and its colour is the whole material. */
        glColor3f(mesh->base_color[0], mesh->base_color[1], mesh->base_color[2]);

        if (buffered) {
            const GLsizei stride = SKIN_VERTEX_FLOATS * (GLsizei)sizeof(float);
            gl_buffer_bind(gl_buffer_array_target(), vertices->vertex_buffer);
            glVertexPointer(3, GL_FLOAT, stride, GL_BUFFER_OFFSET(0));
            glNormalPointer(GL_FLOAT, stride, GL_BUFFER_OFFSET(3 * sizeof(float)));
            if (texturing) {
                /* Its own buffer, because UVs are the model's and the vertices
                   are this body's - they cannot share one without every body
                   carrying a copy of a map that never changes. */
                gl_buffer_bind(gl_buffer_array_target(), mesh->uv_buffer);
                glTexCoordPointer(2, GL_FLOAT, 0, GL_BUFFER_OFFSET(0));
            }
            gl_buffer_bind(gl_buffer_index_target(), mesh->index_buffer);
            glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT,
                           GL_BUFFER_OFFSET(0));
            gl_buffer_bind(gl_buffer_index_target(), 0);
            gl_buffer_bind(gl_buffer_array_target(), 0);
        } else {
            const GLsizei stride = SKIN_VERTEX_FLOATS * (GLsizei)sizeof(float);
            if (texturing) glTexCoordPointer(2, GL_FLOAT, 0, mesh->uvs);
            glVertexPointer(3, GL_FLOAT, stride, vertices->vertices);
            glNormalPointer(GL_FLOAT, stride, vertices->vertices + 3);
            glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT,
                           mesh->indices);
        }

#if SKINNED_HONOUR_DOUBLE_SIDED
        if (uncull) glEnable(GL_CULL_FACE);
#endif
    }

    if (texturing) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (!was_normalizing) glDisable(GL_NORMALIZE);
    if (was_texturing) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
}

/* ---- a cut of the rig -------------------------------------------------- */

/* Building the cut is arithmetic and lives in skinned_pose.c beside the rest of
   the headless half. Only the draw is here, because only the draw needs GL. */

void skinned_model_draw_subset(const SkinnedModel* model,
                               const SkinnedInstance* instance,
                               const SkinnedSubset* subset) {
    if (!model || !instance || !subset || model->mesh_count == 0) return;

    GLboolean was_normalizing = glIsEnabled(GL_NORMALIZE);
    GLboolean was_texturing = glIsEnabled(GL_TEXTURE_2D);

    if (!was_normalizing) glEnable(GL_NORMALIZE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);

    int texturing = 0;
    for (int i = 0; i < model->mesh_count && i < instance->mesh_count; ++i) {
        const SkinnedMesh* mesh = &model->meshes[i];
        const SkinnedMeshVertices* vertices = &instance->meshes[i];
        if (!subset->indices[i] || subset->index_count[i] <= 0) continue;
        if (!vertices->vertices) continue;

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

        glColor3f(mesh->base_color[0], mesh->base_color[1], mesh->base_color[2]);

        /* No buffer is bound for any stream: the indices are ours and in
           process memory, and GL cannot take those from here while an element
           buffer is bound. Binding zero on both targets first, because a mesh
           drawn before this one may have left one bound. */
        gl_buffer_bind(gl_buffer_array_target(), 0);
        gl_buffer_bind(gl_buffer_index_target(), 0);
        const GLsizei stride = SKIN_VERTEX_FLOATS * (GLsizei)sizeof(float);
        if (texturing) glTexCoordPointer(2, GL_FLOAT, 0, mesh->uvs);
        glVertexPointer(3, GL_FLOAT, stride, vertices->vertices);
        glNormalPointer(GL_FLOAT, stride, vertices->vertices + 3);
        glDrawElements(GL_TRIANGLES, subset->index_count[i], GL_UNSIGNED_INT,
                       subset->indices[i]);
    }

    if (texturing) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    if (!was_normalizing) glDisable(GL_NORMALIZE);
    if (was_texturing) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
}
