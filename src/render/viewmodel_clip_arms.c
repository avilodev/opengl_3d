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

/* Where in the hand a hilt actually sits, in rig units along the hand bone's
   own axes. A bone's origin is the WRIST, and a fist closes some way past it
   toward the fingers - on this rig about 11cm once scaled - so a weapon hung
   at the bone floats behind the hand that is supposedly gripping it. The same
   correction character_transform.c makes for the third-person hand, in the
   same two directions and for the same reason. */
#define CLIP_GRIP_INTO_PALM -0.020f
#define CLIP_GRIP_ALONG_HAND 0.060f

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

/* The other shoulder, needed only to find the midpoint between them. */
#define CLIP_ARM_UPPER_L "upperarm_l"

int viewmodel_clip_arms_calibrate(const SkinnedModel* model, float* scale) {
    if (!model) return 0;
    const Skeleton* skeleton = &model->skeleton;
    int upper = node_of(skeleton, CLIP_ARM_UPPER);
    int lower = node_of(skeleton, CLIP_ARM_LOWER);
    int hand = node_of(skeleton, CLIP_ARM_HAND);
    if (upper < 0 || lower < 0 || hand < 0) return 0;

    /* Off the bind pose, because this one IS a property of the rig: how long
       the arm is does not change with what it is doing. */
    Pose bind;
    skinned_model_bind_pose(model, &bind);
    Mat4 u = pose_global(skeleton, &bind, upper);
    Mat4 l = pose_global(skeleton, &bind, lower);
    Mat4 h = pose_global(skeleton, &bind, hand);

    /* Scaled by reach and not by height, for the reason viewmodel_arms.c is:
       what has to be right is how far the hands are from the shoulders. */
    float reach = distance_between(&u, &l) + distance_between(&l, &h);
    if (reach <= 1e-4f) return 0;
    if (scale) *scale = ARMS_REACH / reach;
    return 1;
}

int viewmodel_clip_arms_anchor(const SkinnedModel* model, const Pose* pose,
                               float shoulder_mid[3]) {
    if (!model || !pose || !shoulder_mid) return 0;
    const Skeleton* skeleton = &model->skeleton;
    int upper = node_of(skeleton, CLIP_ARM_UPPER);
    int upper_l = node_of(skeleton, CLIP_ARM_UPPER_L);
    if (upper < 0 || upper_l < 0) return 0;
    Mat4 u = pose_global(skeleton, pose, upper);
    Mat4 ul = pose_global(skeleton, pose, upper_l);
    for (int c = 0; c < 3; ++c)
        shoulder_mid[c] = (u.m[12 + c] + ul.m[12 + c]) * 0.5f;
    return 1;
}

int viewmodel_clip_arms_find(const SkinnedModel* model, const char* name) {
    if (!model || !name) return -1;
    return skinned_model_find_clip(model, name);
}

int viewmodel_clip_arms_pose(const SkinnedModel* model, int clip, float time,
                             const MeleeSpec* melee, Pose* out, Mat4* hand_out) {
    if (!model || !out || clip < 0 || clip >= model->clip_count) return 0;
    const Skeleton* skeleton = &model->skeleton;

    int hand = node_of(skeleton, CLIP_ARM_HAND);
    if (hand < 0) return 0;

    float scale;
    if (!viewmodel_clip_arms_calibrate(model, &scale)) return 0;

    skinned_model_sample(model, clip, time, out);

    float shoulder_mid[3];
    if (!viewmodel_clip_arms_anchor(model, out, shoulder_mid)) return 0;

    /* The hand relative to the shoulder LINE of this same pose - the one point
       the arms are also drawn about, so the two cannot separate. Against the
       firing shoulder alone it sat 21cm to the side of the fist holding it;
       against the bind pose's shoulders it carried the whole spine bend of a
       cut and lurched a metre down the screen every swing. */
    Mat4 h = pose_global(skeleton, out, hand);

    /* Into the fist, along the hand's own axes rather than the camera's: the
       hand rotates through the swing and the grip point has to rotate with it,
       or the hilt slides through the palm as the wrist turns. */
    h = mat4_multiply(h, mat4_translation(CLIP_GRIP_INTO_PALM,
                                          CLIP_GRIP_ALONG_HAND, 0.0f));

    if (hand_out) {
        float rig_offset[3] = { h.m[12] - shoulder_mid[0],
                                h.m[13] - shoulder_mid[1],
                                h.m[14] - shoulder_mid[2] };
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

int viewmodel_clip_arms_base(const SkinnedModel* model, int clip, float time,
                             const MeleeSpec* melee, Pose* arms_out,
                             ViewmodelPose* out) {
    Pose scratch;
    Mat4 hand;
    Pose* arms = arms_out ? arms_out : &scratch;
    if (!out) return 0;
    if (!viewmodel_clip_arms_pose(model, clip, time, melee, arms, &hand))
        return 0;

    memset(out, 0, sizeof *out);
    out->x = hand.m[12];
    out->y = hand.m[13];
    out->z = hand.m[14];

    /* The renderer names glRotatef(yaw, 0,1,0), then pitch about x, then roll
       about z, and GL hands the last one named to the vertex first - so the
       matrix it builds is Ry * Rx * Rz, and these three lines are that product
       read backwards. Getting the order wrong here does not fail: it produces
       a sword that is merely rotated oddly, and looks like an art problem.

       Column-major, so R[row][col] is m[col * 4 + row]. */
    float sin_pitch = -hand.m[9];
    if (sin_pitch > 1.0f) sin_pitch = 1.0f;
    if (sin_pitch < -1.0f) sin_pitch = -1.0f;
    out->pitch = asinf(sin_pitch) * (1.0f / MATH_DEG_TO_RAD);
    out->roll = atan2f(hand.m[1], hand.m[5]) * (1.0f / MATH_DEG_TO_RAD);
    out->yaw = atan2f(hand.m[8], hand.m[10]) * (1.0f / MATH_DEG_TO_RAD);
    return 1;
}
