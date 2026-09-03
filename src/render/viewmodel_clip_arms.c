/* See include/render/viewmodel_clip_arms.h for what this is and why it is not
   part of viewmodel_arms.c. */
#include "render/viewmodel_clip_arms.h"
#include "render/arm_placement.h"
#include "utils/math_utils.h"

#include <math.h>
#include <string.h>

/* The bones the scale is measured across, and the one the weapon hangs off.
   Named here rather than shared with viewmodel_arms.c because that file's
   ArmChain carries a solve's worth of axes and lengths with it, and all that
   is wanted here is a length and a matrix. */
#define CLIP_ARM_UPPER "upperarm_r"
#define CLIP_ARM_LOWER "lowerarm_r"
#define CLIP_ARM_HAND  "hand_r"

/* Where the shoulder line sits in camera space. The arms arrive from the clip
   in rig space about the rig's own root, which is on the floor between its
   feet; hung there unshifted they would be drawn from the player's shins. The
   whole pose is carried up and back so the shoulders land where a pair of
   shoulders goes, which is the same place viewmodel_arms.c solves them to. */
#define CLIP_ARMS_SHOULDER_DOWN ARMS_SHOULDER_DOWN

static int node_of(const Skeleton* skeleton, const char* name) {
    for (int i = 0; i < skeleton->node_count; ++i)
        if (strcmp(skeleton->node_names[i], name) == 0) return i;
    return -1;
}

/* A bone's global transform in a pose, walking only its own ancestors. */
static Mat4 pose_global(const Skeleton* skeleton, const Pose* pose, int node) {
    Mat4 out = mat4_identity();
    int chain[SKIN_MAX_NODES];
    int depth = 0;
    for (int n = node; n >= 0 && depth < SKIN_MAX_NODES; n = skeleton->parent[n])
        chain[depth++] = n;
    for (int i = depth - 1; i >= 0; --i) {
        const Transform* t = &pose->local[chain[i]];
        out = mat4_multiply(out, mat4_from_trs(t->translation, t->rotation,
                                               t->scale));
    }
    return out;
}

static float distance_between(const Mat4* a, const Mat4* b) {
    float d = 0.0f;
    for (int c = 0; c < 3; ++c) {
        float e = a->m[12 + c] - b->m[12 + c];
        d += e * e;
    }
    return sqrtf(d);
}

/* Rig units to metres. Measured off the bind pose across the firing arm, so it
   is a property of the rig and not of whichever frame of whichever swing
   happens to be asking. */
static float rig_scale(const SkinnedModel* model) {
    const Skeleton* skeleton = &model->skeleton;
    int upper = node_of(skeleton, CLIP_ARM_UPPER);
    int lower = node_of(skeleton, CLIP_ARM_LOWER);
    int hand = node_of(skeleton, CLIP_ARM_HAND);
    if (upper < 0 || lower < 0 || hand < 0) return 0.0f;

    Pose bind;
    skinned_model_bind_pose(model, &bind);
    Mat4 u = pose_global(skeleton, &bind, upper);
    Mat4 l = pose_global(skeleton, &bind, lower);
    Mat4 h = pose_global(skeleton, &bind, hand);
    float reach = distance_between(&u, &l) + distance_between(&l, &h);
    return reach > 1e-4f ? ARMS_REACH / reach : 0.0f;
}

int viewmodel_clip_arms_find(const SkinnedModel* model, const char* name) {
    if (!model || !name) return -1;
    return skinned_model_find_clip(model, name);
}

int viewmodel_clip_arms_pose(const SkinnedModel* model, int clip, float time,
                             const MeleeSpec* melee, Pose* out, Mat4* hand_out) {
    if (!model || !out || clip < 0 || clip >= model->clip_count) return 0;
    const Skeleton* skeleton = &model->skeleton;

    int shoulder = node_of(skeleton, CLIP_ARM_UPPER);
    int hand = node_of(skeleton, CLIP_ARM_HAND);
    if (shoulder < 0 || hand < 0) return 0;

    float scale = rig_scale(model);
    if (scale <= 0.0f) return 0;

    skinned_model_sample(model, clip, time, out);

    /* The hand relative to the shoulder, which is the only part of the clip
       that means anything here. Taking the hand's absolute rig position
       instead would carry the root motion of a swing that was authored to
       travel, and a first-person camera does not travel with it. */
    Mat4 s = pose_global(skeleton, out, shoulder);
    Mat4 h = pose_global(skeleton, out, hand);

    if (hand_out) {
        float rig_offset[3] = { h.m[12] - s.m[12],
                                h.m[13] - s.m[13],
                                h.m[14] - s.m[14] };
        float camera[3];
        arm_placement_rig_to_camera(rig_offset, 0.0f, camera);

        /* The rotation goes through the same half turn, as a yaw about the
           vertical: the sword has to point where the hand points, and a
           position without an orientation would leave the blade pointing back
           down the rig's own forward however right the grip was. */
        Mat4 facing = mat4_rotation_degrees(ARMS_FACING_DEGREES, 0, 1, 0);
        Mat4 rotation = mat4_multiply(facing, h);
        *hand_out = rotation;
        hand_out->m[12] = camera[0] * scale
                        + (melee ? melee->view_clip_offset[0] : 0.0f);
        hand_out->m[13] = camera[1] * scale - CLIP_ARMS_SHOULDER_DOWN
                        + (melee ? melee->view_clip_offset[1] : 0.0f);
        hand_out->m[14] = camera[2] * scale
                        + (melee ? melee->view_clip_offset[2] : 0.0f);
    }
    return 1;
}
