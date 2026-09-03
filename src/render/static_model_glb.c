#include "render/static_model.h"
#include "render/glb.h"
#include "utils/stb_image.h"

#include <stdlib.h>
#include <string.h>

/* Reading a static prop out of a binary glTF. The container work - bounds
   checking, accessor spans - is render/glb.h; what is here is the meaning laid
   over it, which for a prop is very little: triangles, a colour, and a base
   colour map.

   Same rule as the rigged reader next door. Anything that would put geometry
   in the wrong place is refused, and anything that only costs fidelity is
   ignored. A node with its own transform is the first kind, because the
   vertices would be drawn where the file did not put them; a normal map is the
   second, because there is no shader here to sample one. */

/* Everything a half-built model owns. Nothing has reached the GPU at this
   point - upload is a separate call - so this is all of it, and the public
   static_model_free lives with the GL code that does have a texture to
   delete. */
static void release(StaticModel* model) {
    for (int i = 0; i < STATIC_MAX_MESHES; ++i) {
        StaticMesh* mesh = &model->meshes[i];
        free(mesh->positions);
        free(mesh->normals);
        free(mesh->uvs);
        free(mesh->indices);
        if (mesh->image) stbi_image_free(mesh->image);
    }
    memset(model, 0, sizeof *model);
}

static int read_int_member(JsonSlice object, const char* key, int* out) {
    JsonSlice value;
    return json_member(object, key, &value) && json_int(value, out);
}

/* ---- materials ---------------------------------------------------------- */

static void read_material(const Glb* glb, JsonSlice primitive, StaticMesh* mesh) {
    JsonSlice materials, material, pbr, factor, value;
    int index = 0, texture = 0;

    mesh->base_color[0] = mesh->base_color[1] = mesh->base_color[2] = 1.0f;
    /* Opaque unless the file says otherwise, which is what a material with no
       fourth component means and what every prop before this one was. */
    mesh->opacity = 1.0f;
    mesh->material[0] = '\0';
    if (!read_int_member(primitive, "material", &index)) return;
    if (!json_member(glb->root, "materials", &materials) ||
        !json_array_at(materials, index, &material)) return;
    /* Kept because it is the only stable handle on which PART of a prop this
       primitive is: indices shuffle when an asset is re-exported and names do
       not. Read before the pbr block, since a material may be named without
       having one. */
    if (json_member(material, "name", &value))
        json_string(value, mesh->material, sizeof mesh->material);
    if (!json_member(material, "pbrMetallicRoughness", &pbr)) return;

    if (json_member(pbr, "baseColorFactor", &factor)) {
        for (int i = 0; i < 3; ++i)
            if (json_array_at(factor, i, &value)) json_float(value, &mesh->base_color[i]);
        if (json_array_at(factor, 3, &value)) json_float(value, &mesh->opacity);
    }

    if (json_member(pbr, "baseColorTexture", &value) &&
        read_int_member(value, "index", &texture))
        glb_decode_image(glb, texture, &mesh->image,
                         &mesh->image_width, &mesh->image_height);
}

/* ---- geometry ----------------------------------------------------------- */

/* Reads one accessor's worth of floats into a fresh array. */
static float* read_attribute(const Glb* glb, JsonSlice attributes, const char* name,
                             const char* type, int components, int* count,
                             char* error, int error_size) {
    JsonSlice value;
    int index = 0;
    GlbAccessor accessor;
    if (!json_member(attributes, name, &value) || !json_int(value, &index)) {
        glb_fail(error, error_size, "primitive has no %s", name);
        return NULL;
    }
    if (!glb_accessor(glb, index, type, components, &accessor, error, error_size))
        return NULL;

    float* out = malloc(sizeof *out * (size_t)accessor.count * (size_t)components);
    if (!out) {
        glb_fail(error, error_size, "out of memory for %d %s", accessor.count, name);
        return NULL;
    }
    if (!glb_read_floats(&accessor, out, error, error_size)) {
        free(out);
        return NULL;
    }
    *count = accessor.count;
    return out;
}

/* POSITION carries the file's own bounds, so the box comes free rather than
   from a second pass over every vertex. */
static void grow_bounds(StaticModel* model, const float* positions, int count) {
    for (int v = 0; v < count; ++v)
        for (int c = 0; c < 3; ++c) {
            float value = positions[v * 3 + c];
            if (value < model->min[c]) model->min[c] = value;
            if (value > model->max[c]) model->max[c] = value;
        }
}

static int parse_primitive(const Glb* glb, JsonSlice primitive, StaticMesh* mesh,
                           char* error, int error_size) {
    JsonSlice attributes, value;
    int mode = 4, index = 0;
    GlbAccessor accessor;

    if (json_member(primitive, "mode", &value) && json_int(value, &mode) && mode != 4)
        return glb_fail(error, error_size, "primitive mode %d is not a triangle list", mode);
    if (!json_member(primitive, "attributes", &attributes))
        return glb_fail(error, error_size, "primitive has no attributes");

    int positions = 0, normals = 0, uvs = 0;
    mesh->positions = read_attribute(glb, attributes, "POSITION", "VEC3", 3,
                                     &positions, error, error_size);
    if (!mesh->positions) return 0;
    /* Without normals every triangle would take the light the same way and the
       gun would read as a silhouette, so this is refused rather than faked. */
    mesh->normals = read_attribute(glb, attributes, "NORMAL", "VEC3", 3,
                                   &normals, error, error_size);
    if (!mesh->normals) return 0;
    if (normals != positions)
        return glb_fail(error, error_size, "primitive has %d positions and %d normals",
                        positions, normals);

    /* A material with no map does not need UVs, and a mesh that has them but
       no texture simply ignores them. Neither is an error. */
    JsonSlice unused;
    if (json_member(attributes, "TEXCOORD_0", &unused)) {
        mesh->uvs = read_attribute(glb, attributes, "TEXCOORD_0", "VEC2", 2,
                                   &uvs, error, error_size);
        if (!mesh->uvs) return 0;
        if (uvs != positions)
            return glb_fail(error, error_size,
                            "primitive has %d positions and %d texture coordinates",
                            positions, uvs);
    }
    mesh->vertex_count = positions;

    if (!read_int_member(primitive, "indices", &index) ||
        !glb_accessor(glb, index, "SCALAR", 1, &accessor, error, error_size))
        return glb_fail(error, error_size, "primitive is not indexed");
    if (accessor.count % 3 != 0)
        return glb_fail(error, error_size, "primitive has %d indices, which is not "
                        "a whole number of triangles", accessor.count);

    mesh->indices = malloc(sizeof *mesh->indices * (size_t)accessor.count);
    if (!mesh->indices)
        return glb_fail(error, error_size, "out of memory for %d indices", accessor.count);
    if (!glb_read_uints(&accessor, mesh->indices, (unsigned int)positions,
                        "vertex index", error, error_size))
        return 0;
    mesh->index_count = accessor.count;

    read_material(glb, primitive, mesh);
    return 1;
}

/* A node that moves its mesh would put the geometry somewhere the caller's own
   transform does not account for. The importer bakes transforms away, so one
   surviving means the file is not what this reader expects. */
static int check_nodes_are_flat(const Glb* glb, char* error, int error_size) {
    JsonSlice nodes, node;
    if (!json_member(glb->root, "nodes", &nodes)) return 1;
    int count = json_array_count(nodes);
    for (int i = 0; i < count; ++i) {
        if (!json_array_at(nodes, i, &node)) continue;
        static const char* moves[] = {"matrix", "translation", "rotation", "scale"};
        for (int k = 0; k < 4; ++k) {
            JsonSlice value;
            if (json_member(node, moves[k], &value))
                return glb_fail(error, error_size,
                                "node %d carries a %s; this reader draws vertices "
                                "as the file states them", i, moves[k]);
        }
    }
    return 1;
}

int static_model_load_glb(StaticModel* model, const char* path,
                          char* error, int error_size) {
    if (!model) return glb_fail(error, error_size, "no model");
    memset(model, 0, sizeof *model);
    for (int c = 0; c < 3; ++c) {
        model->min[c] = 1e30f;
        model->max[c] = -1e30f;
    }

    Glb glb;
    if (!glb_open(path, &glb, error, error_size)) return 0;

    int ok = check_nodes_are_flat(&glb, error, error_size);

    JsonSlice meshes, mesh, primitives, primitive;
    if (ok && (!json_member(glb.root, "meshes", &meshes) ||
               !json_array_at(meshes, 0, &mesh) ||
               !json_member(mesh, "primitives", &primitives)))
        ok = glb_fail(error, error_size, "glb has no mesh with primitives");

    int count = ok ? json_array_count(primitives) : 0;
    if (ok && count > STATIC_MAX_MESHES)
        ok = glb_fail(error, error_size, "mesh has %d primitives; the limit is %d",
                      count, STATIC_MAX_MESHES);

    for (int i = 0; ok && i < count; ++i) {
        if (!json_array_at(primitives, i, &primitive)) {
            ok = glb_fail(error, error_size, "primitive %d is malformed", i);
            break;
        }
        StaticMesh* out = &model->meshes[model->mesh_count];
        ok = parse_primitive(&glb, primitive, out, error, error_size);
        if (!ok) break;
        grow_bounds(model, out->positions, out->vertex_count);
        model->mesh_count++;
    }

    glb_close(&glb);
    if (!ok) release(model);
    return ok;
}

