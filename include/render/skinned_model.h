#ifndef RENDER_SKINNED_MODEL_H
#define RENDER_SKINNED_MODEL_H

#include "utils/mat4.h"

/* A rigged character: skinned meshes, the skeleton they hang off, and the
   animation clips that move it.

   Loading, posing and drawing are three files, split so the first two need no
   GL context and can be exercised headlessly. What holds them together is that
   nothing here knows about the player: this is a mesh that can hold a pose, and
   character_renderer decides which pose that is.

   Skinning happens on the CPU. That is not a preference, it is what a
   fixed-function GL 1.1 renderer leaves available - there is no vertex shader
   to hand joint matrices to. The cost is one matrix blend per vertex per
   frame, and at the eight and a half thousand vertices of this rig that is a
   rounding error against the world pass. */

#define SKIN_MAX_NODES   256
#define SKIN_MAX_JOINTS  128
/* A character built for this engine wears its gear as separate materials -
   body, head, hands, vest, helmet, pouches - and each one is a primitive. Eight
   was enough for the mannequin, which has two; a skin authored to look like
   something needs the room. */
#define SKIN_MAX_MESHES  24
#define SKIN_INFLUENCES  4     /* glTF's JOINTS_0/WEIGHTS_0 is exactly four */
/* Floats per skinned vertex: position then normal. The two are produced
   together and consumed together, so they are interleaved into one array and
   one buffer rather than kept as two of each - it halves the uploads and lets
   the card fetch a whole vertex from one place. */
#define SKIN_VERTEX_FLOATS 6
#define SKIN_NAME_MAX    64

/* A node's local transform, kept as translation/rotation/scale rather than a
   matrix because that is what animation interpolates and what blending
   between two clips has to happen in. Rotation is a quaternion, (x,y,z,w). */
typedef struct {
    float translation[3];
    float rotation[4];
    float scale[3];
} Transform;

/* One skeleton's worth of local transforms: the output of sampling a clip and
   the input to skinning. Ten kilobytes, so it wants to live in a struct or a
   static, not on a deep stack. */
typedef struct {
    Transform local[SKIN_MAX_NODES];
} Pose;

/* Which nodes a layered blend is allowed to move. A layer that aims the upper
   body has to leave the legs to whatever the locomotion clip is doing with
   them, and the boundary between the two is a subtree of the skeleton. */
typedef struct {
    unsigned char node[SKIN_MAX_NODES];
} PoseMask;

typedef struct {
    char name[SKIN_NAME_MAX];
    Transform bind_local[SKIN_MAX_NODES];
    int parent[SKIN_MAX_NODES];      /* -1 for a root */
    int order[SKIN_MAX_NODES];       /* node indices, parents before children */
    char node_names[SKIN_MAX_NODES][SKIN_NAME_MAX];
    int node_count;
    int joint_node[SKIN_MAX_JOINTS]; /* joint index -> node index */
    Mat4 inverse_bind[SKIN_MAX_JOINTS];
    int joint_count;
} Skeleton;

typedef struct {
    float* times;                    /* key_count */
    float* values;                   /* key_count * components */
    int key_count;
    int components;                  /* 3 for translation and scale, 4 for rotation */
} AnimSampler;

enum { ANIM_PATH_TRANSLATION, ANIM_PATH_ROTATION, ANIM_PATH_SCALE };

typedef struct {
    unsigned short node;
    unsigned short sampler;
    unsigned char path;
} AnimChannel;

typedef struct {
    char name[SKIN_NAME_MAX];
    float duration;
    AnimSampler* samplers;
    int sampler_count;
    AnimChannel* channels;
    int channel_count;
} AnimationClip;

typedef struct {
    float* positions;                /* bind pose, 3 per vertex */
    float* normals;
    unsigned char* joints;           /* SKIN_INFLUENCES per vertex */
    float* weights;                  /* SKIN_INFLUENCES per vertex */
    unsigned int* indices;
    int vertex_count;
    int index_count;
    /* Texture coordinates, NULL on a mesh the file gave none. Skinning does not
       touch these: a vertex's place in the map does not move when its bone
       does, so the bind-pose array is what the draw points at every frame. */
    float* uvs;

    float base_color[3];
    int double_sided;

    /* The material's base colour map, decoded but not yet on the GPU. Freed by
       skinned_model_upload once it is; NULL means this mesh draws flat.

       Nothing beyond base colour is read. A normal, roughness or metallic map
       needs a shader to sample it and there is none here, so the way to get
       that detail onto a character is to bake it into the albedo at conversion
       time - which is what tools/fbx_to_glb.py already does with ambient
       occlusion. */
    unsigned char* image;
    int image_width;
    int image_height;
    unsigned int texture;            /* GL name, 0 until uploaded */

    /* The parts of this mesh that skinning never touches, on the card. A
       vertex's indices and its place in the texture map are the same on every
       frame of every clip, so they are uploaded once with the model and shared
       by every body wearing it - only the positions and normals are per body,
       and those live on the instance. 0 when the driver has no buffer
       objects. */
    unsigned int index_buffer;
    unsigned int uv_buffer;
} SkinnedMesh;

typedef struct {
    SkinnedMesh meshes[SKIN_MAX_MESHES];
    int mesh_count;
    Skeleton skeleton;
    AnimationClip* clips;
    int clip_count;
    float min_y, max_y;              /* bind pose height extent, for scaling to fit */
} SkinnedModel;

/* ---- the instance ------------------------------------------------------- */

/* One body's worth of skinned vertices. This is the whole reason a SkinnedModel
   is an asset and not a character: the model holds the bind pose, the skeleton
   and the clips, all of which are read-only once loaded and can be shared by
   every body wearing that rig, and this holds the per-frame product of posing
   it, which cannot. Two characters on one model each need their own, or the
   second skins over the first and both draw the second's pose.

   Sized to the model it was initialised against and useless without it: the
   arrays are parallel to that model's meshes, vertex for vertex. */
typedef struct {
    /* Position and normal interleaved, SKIN_VERTEX_FLOATS per vertex. */
    float* vertices;
    int vertex_count;
    /* The same vertices on the card, rewritten every frame by
       skinned_instance_upload. 0 until the first upload, and 0 for good on a
       driver with no buffer objects, which is what puts the draw back on the
       array above. */
    unsigned int vertex_buffer;
} SkinnedMeshVertices;

typedef struct {
    SkinnedMeshVertices meshes[SKIN_MAX_MESHES];
    int mesh_count;
} SkinnedInstance;

/* ---- loading (src/render/skinned_model_glb.c) --------------------------- */

/* Reads a binary glTF holding one skinned mesh and its skeleton, plus any
   animations it carries. Non-zero on success; on failure the model is left
   zeroed and `error` carries a reason worth printing.

   Unlike the static reader this replaces, node transforms are read rather than
   refused, and a mesh may have several primitives - a rig whose body and
   joints carry different materials is the normal export, not an oddity.
   Base colour maps and their UVs are read too; anything past base colour is
   not, for want of a shader to sample it. */
int skinned_model_load_glb(SkinnedModel* model, const char* path,
                           char* error, int error_size);

/* Appends the clips from another file onto an already loaded model, matching
   its nodes to ours by name. This is what lets a character built from one
   animation library also play the clips from a second: the two files share a
   skeleton, so the meshes in the second are redundant and only its animations
   are taken. Channels naming a node this skeleton does not have are skipped
   rather than refused, since an extra prop bone costs nothing. */
int skinned_model_add_clips(SkinnedModel* model, const char* path,
                            char* error, int error_size);

/* The clip's index, or -1. Names come from the file, so this is how a caller
   asks for "Sprint_Loop" without knowing what number it landed on. */
int skinned_model_find_clip(const SkinnedModel* model, const char* name);

/* ---- posing and skinning (src/render/skinned_pose.c) -------------------- */

/* The skeleton as authored, which is what a model with no clips draws in. */
void skinned_model_bind_pose(const SkinnedModel* model, Pose* out);

/* Samples `clip` at `time` seconds, holding the first and last keyframes
   outside the clip's range rather than wrapping: a caller that wants a loop
   wraps the time itself, and one playing a one-shot wants the end held. */
void skinned_model_sample(const SkinnedModel* model, int clip, float time, Pose* out);

/* out = a moving toward b by t. Blending local transforms rather than skinned
   vertices is what keeps a cross-fade from collapsing limbs through the body
   on the way. `out` may alias either input, so a pose can be blended in place. */
void skinned_model_blend(const SkinnedModel* model, const Pose* a, const Pose* b,
                         float t, Pose* out);

/* The same blend, restricted to the nodes `mask` allows. A NULL mask blends
   everything, which is what skinned_model_blend is. Nodes the mask excludes
   keep `a` exactly, so a layer can be laid over a clip without the clip
   needing to know about it. */
void skinned_model_blend_masked(const SkinnedModel* model, const Pose* a, const Pose* b,
                                float t, const PoseMask* mask, Pose* out);

/* Marks `root` and everything descended from it. Zeroes the mask first, so an
   unknown bone name leaves a mask that moves nothing rather than everything -
   a layer that quietly did nothing is easier to spot than one that quietly
   overwrote the legs. Non-zero when the bone was found. */
int skinned_model_mask_subtree(const SkinnedModel* model, const char* root,
                               PoseMask* out);

/* Gives `out` its own copy of `model`'s vertices, seeded with the bind pose so
   a draw before the first skin shows the body standing rather than collapsed at
   the origin. Non-zero on success; on failure `out` is left zeroed, which draws
   nothing rather than reading a half-allocated array. Needs no GL context.

   One per body. Free it with skinned_instance_free, and not after the model it
   was built against: the free only touches its own arrays, but the vertex
   counts it was sized to came from there. */
int skinned_instance_init(SkinnedInstance* out, const SkinnedModel* model);
void skinned_instance_free(SkinnedInstance* instance);

/* Poses the skeleton and writes `instance`'s vertices. Call once per frame per
   body, before drawing it. `model` is const: skinning reads the bind pose and
   the skeleton and writes only the instance, which is what lets two bodies
   share one rig. */
void skinned_model_skin(const SkinnedModel* model, const Pose* pose,
                        SkinnedInstance* instance);

/* Where the named bone has ended up in `pose`, as a transform in the model's
   own space. Non-zero when the skeleton has that bone.

   This is how a prop gets carried: a rifle is not skinned to anything, it
   hangs off one bone, and multiplying it onto this matrix puts it in the hand
   for every frame of every clip without the animation knowing it is there.
   Only that bone's own ancestors are walked, so asking for one bone is cheaper
   than the full pose skinning does. */
int skinned_model_bone_matrix(const SkinnedModel* model, const Pose* pose,
                              const char* name, Mat4* out);

/* ---- drawing (src/render/skinned_model.c) ------------------------------- */

/* Hands the decoded images and the shared geometry - indices and UVs - to GL.
   Needs a current context; call once, after loading, and before the first
   draw. Safe on a model with no textures, and on a driver with no buffer
   objects, which keeps the client-side arrays.

   Separate from the load for the reason the whole file is split that way: the
   loader stays runnable with no GL context, which is where a bad accessor index
   is cheapest to find. */
void skinned_model_upload(SkinnedModel* model);

/* Releases an instance's GL buffers. Needs a current context, which is what
   keeps it out of skinned_instance_free - that one is pure allocation and has
   to stay runnable headlessly. Call this first, then that. */
void skinned_instance_free_buffers(SkinnedInstance* instance);

/* Frees the model, GL textures and buffers included, so this needs a current
   context. Instances built against it are not touched; free those first. */
void skinned_model_free(SkinnedModel* model);

/* Hands this frame's skinned vertices to the card, creating the instance's
   buffers on the first call. Needs a current context; call once per frame per
   body, after skinned_model_skin and before drawing it.

   Separate from the skin for the reason the whole file is split that way:
   skinning stays arithmetic on arrays with no GL in it, so it can be run and
   checked headlessly, and this is the one step that cannot. A body that is
   skinned but never uploaded still draws - from its client-side array, which
   is also what a driver with no buffer objects leaves it on. */
void skinned_instance_upload(const SkinnedModel* model, SkinnedInstance* instance);

/* Draws one body about the model's own origin, with the current transform: the
   vertices come from `instance` and the materials, indices and UVs from
   `model`, so the caller sets a transform and this sets colours. */
void skinned_model_draw(const SkinnedModel* model, const SkinnedInstance* instance);

/* ---- a cut of the rig -------------------------------------------------- */

/* Which triangles of a rig belong to one part of its skeleton.

   A PoseMask says which bones a layer may MOVE; this says which geometry those
   bones CARRY, which is a different question and the one you have to answer to
   draw an arm without the body attached to it. First-person arms are the reason
   it exists: the player is inside their own chest, and drawing the whole rig
   there shows the inside of it.

   A triangle is in when all three of its vertices are, and a vertex is in when
   the joint holding most of its weight is a bone the mask marks. Dominant
   weight rather than any weight, because a shoulder vertex is blended across
   the clavicle and the upper arm and either rule alone gives a ragged seam -
   this one cuts where the influence actually changes hands.

   Built once against a model and valid for as long as that model is. The
   indices are the subset's own; the vertices, materials and UVs still come from
   the model and the instance, so a cut costs an index array and nothing else. */
typedef struct {
    unsigned int* indices[SKIN_MAX_MESHES];
    int index_count[SKIN_MAX_MESHES];
    int mesh_count;
} SkinnedSubset;

/* Non-zero on success. A mask marking no bone the geometry uses yields a subset
   that draws nothing rather than one that draws everything, for the same reason
   skinned_model_mask_subtree zeroes first: a cut that quietly included the
   whole body would be harder to notice than one that showed nothing. */
int skinned_subset_init(SkinnedSubset* out, const SkinnedModel* model,
                        const PoseMask* mask);
void skinned_subset_free(SkinnedSubset* subset);

/* skinned_model_draw, restricted to `subset`. Always takes the client-side
   vertex path: a subset's indices live in process memory rather than on the
   card, and GL will not read indices from here while an element buffer is
   bound. An arm is a few thousand vertices, so that costs nothing worth the
   second upload path it would take to avoid. */
void skinned_model_draw_subset(const SkinnedModel* model,
                               const SkinnedInstance* instance,
                               const SkinnedSubset* subset);

#endif
