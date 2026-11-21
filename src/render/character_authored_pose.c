#include "render/character_rig_internal.h"
#include "render/arm_geometry.h"
#include "utils/math_utils.h"

#include <math.h>

/* Hand-authored poses for states the clip library lacks, plus two-bone arm IK
   used by those poses and by callers placing hands on props. */

/* ---- the ledge hang ----
   No library clip puts both hands overhead in a hang, so this is built by hand.
   Model space: +y up, +z toward the wall. Unlisted bones keep the base clip.
   Shape: left hand on the lip, right arm down with the rifle, legs dangling. */
static const AuthoredBone HANG_BONES[] = {
    {"spine_01",    0.00f, 0.99f, 0.10f},
    {"spine_02",    0.00f, 0.99f, 0.10f},
    {"spine_03",    0.00f, 0.99f, 0.08f},
    {"neck_01",     0.00f, 0.97f, 0.24f},
    /* Reaching arm fallback; overridden by IK when a ledge target is set. */
    {"upperarm_l",  0.20f, 0.96f, 0.20f},
    {"lowerarm_l",  0.06f, 0.99f, 0.12f},
    /* Palm forward over the lip. */
    {"hand_l",      0.05f, 0.30f, 0.95f},
    {"upperarm_r", -0.26f,-0.95f, 0.06f},
    {"lowerarm_r", -0.16f,-0.94f, 0.30f},
    {"hand_r",     -0.10f,-0.88f, 0.46f},
    {"thigh_l",     0.10f,-0.91f, 0.40f},
    {"thigh_r",    -0.10f,-0.91f, 0.40f},
    {"calf_l",      0.08f,-0.93f,-0.36f},
    {"calf_r",     -0.08f,-0.93f,-0.36f},
    {"foot_l",      0.05f,-0.55f,-0.83f},
    {"foot_r",     -0.05f,-0.55f,-0.83f},
    /* Gripping fingers over the edge, thumb opposed. Overrides the base clip's
       pistol-grip curl. */
    {"index_01_l", -0.10f, 0.02f, 0.99f},
    {"index_02_l", -0.08f,-0.72f, 0.69f},
    {"index_03_l", -0.06f,-0.96f, 0.24f},
    {"middle_01_l",-0.02f, 0.02f, 0.99f},
    {"middle_02_l",-0.02f,-0.72f, 0.69f},
    {"middle_03_l",-0.01f,-0.96f, 0.24f},
    {"ring_01_l",   0.06f, 0.02f, 0.99f},
    {"ring_02_l",   0.05f,-0.72f, 0.69f},
    {"ring_03_l",   0.04f,-0.96f, 0.24f},
    {"pinky_01_l",  0.14f, 0.02f, 0.99f},
    {"pinky_02_l",  0.11f,-0.72f, 0.69f},
    {"pinky_03_l",  0.08f,-0.96f, 0.24f},
    {"thumb_01_l", -0.30f,-0.34f, 0.89f},
    {"thumb_02_l", -0.22f,-0.66f, 0.72f},
    {"thumb_03_l", -0.16f,-0.84f, 0.52f},
};

/* Elbow bend direction for the left arm: out and slightly back. +x is the
   model's left. */
static const float HANG_ELBOW_POLE[3] = {1.0f, -0.25f, -0.45f};

enum { AUTHORED_SLOT_HANG = 0 };

#define HANG_BONE_COUNT ((int)(sizeof HANG_BONES / sizeof HANG_BONES[0]))
_Static_assert(HANG_BONE_COUNT <= CHARACTER_AUTHORED_BONES,
               "CHARACTER_AUTHORED_BONES must cover the longest authored pose");

const AuthoredPose CHARACTER_HANG_POSE = {
    AUTHORED_SLOT_HANG, 1, "upperarm_l", "lowerarm_l", "hand_l",
    HANG_ELBOW_POLE, HANG_BONE_COUNT, HANG_BONES,
};

/* Past this fraction of full reach, reach_hands leaves the arm alone rather
   than straightening it through the torso. */
#define REACH_LIMIT 0.97f

void character_authored_reset(CharacterAnimation* animation) {
    for (int i = 0; i < CHARACTER_AUTHORED_POSES; ++i)
        animation->authored[i].count = 0;
    animation->reach_upper = -1;
    animation->reach_lower = -1;
    animation->reach_upper_length = 0.0f;
    animation->reach_lower_length = 0.0f;
}

/* Missing bones resolve to -1 and are skipped at apply time. Bones with no
   child have no rest axis and are also skipped. */
void character_authored_resolve(CharacterAnimation* animation,
                                const SkinnedModel* model, const AuthoredPose* pose) {
    const Skeleton* skeleton = &model->skeleton;
    if (!pose || pose->slot < 0 || pose->slot >= CHARACTER_AUTHORED_POSES) return;

    for (int i = 0; i < pose->count && i < CHARACTER_AUTHORED_BONES; ++i) {
        animation->authored[pose->slot].node[i] = -1;
        short n = arm_find_node(skeleton, pose->bones[i].bone);
        float axis[3];
        if (n < 0 || !arm_rest_axis(skeleton, n, axis)) continue;
        animation->authored[pose->slot].node[i] = n;
        animation->authored[pose->slot].axis[i][0] = axis[0];
        animation->authored[pose->slot].axis[i][1] = axis[1];
        animation->authored[pose->slot].axis[i][2] = axis[2];
        animation->authored[pose->slot].count++;
    }
    if (pose->reaches) {
        animation->reach_upper = arm_find_node(skeleton, pose->reach_upper);
        animation->reach_lower = arm_find_node(skeleton, pose->reach_lower);
        animation->reach_upper_length =
            arm_bone_length(skeleton, arm_find_node(skeleton, pose->reach_lower));
        animation->reach_lower_length =
            arm_bone_length(skeleton, arm_find_node(skeleton, pose->reach_hand));
    }
}

/* Law-of-cosines two-bone solve. The pole picks the bend plane. Out-of-reach
   targets are clamped to full extension. Directions are model space. */
static void reach_two_bone(const float shoulder[3], const float target[3],
                           float upper_length, float lower_length,
                           const float pole[3],
                           float upper_dir[3], float lower_dir[3]) {
    float to[3];
    for (int i = 0; i < 3; ++i) to[i] = target[i] - shoulder[i];
    float distance = sqrtf(to[0]*to[0] + to[1]*to[1] + to[2]*to[2]);
    if (distance < 1e-5f) { distance = 1e-5f; to[1] = distance; }

    float axis[3];
    for (int i = 0; i < 3; ++i) axis[i] = to[i] / distance;

    float shortest = fabsf(upper_length - lower_length) + 1e-4f;
    float longest = upper_length + lower_length - 1e-4f;
    if (distance < shortest) distance = shortest;
    if (distance > longest) distance = longest;

    float cosine = (upper_length * upper_length + distance * distance -
                    lower_length * lower_length) / (2.0f * upper_length * distance);
    cosine = clampf(cosine, -1.0f, 1.0f);
    float angle = acosf(cosine);

    /* Pole component perpendicular to the arm line; fall back to any
       perpendicular if the pole is parallel. */
    float dot = pole[0]*axis[0] + pole[1]*axis[1] + pole[2]*axis[2];
    float bend[3];
    for (int i = 0; i < 3; ++i) bend[i] = pole[i] - axis[i] * dot;
    float length = sqrtf(bend[0]*bend[0] + bend[1]*bend[1] + bend[2]*bend[2]);
    if (length < 1e-5f) {
        bend[0] = axis[1]; bend[1] = -axis[0]; bend[2] = 0.0f;
        length = sqrtf(bend[0]*bend[0] + bend[1]*bend[1]);
        if (length < 1e-5f) { bend[0] = 1.0f; bend[1] = bend[2] = 0.0f; length = 1.0f; }
    }
    for (int i = 0; i < 3; ++i) bend[i] /= length;

    float elbow[3];
    for (int i = 0; i < 3; ++i) {
        upper_dir[i] = axis[i] * cosf(angle) + bend[i] * sinf(angle);
        elbow[i] = shoulder[i] + upper_dir[i] * upper_length;
        lower_dir[i] = target[i] - elbow[i];
    }
    length = sqrtf(lower_dir[0]*lower_dir[0] + lower_dir[1]*lower_dir[1] +
                   lower_dir[2]*lower_dir[2]);
    if (length < 1e-5f) { lower_dir[0] = upper_dir[0]; lower_dir[1] = upper_dir[1];
                          lower_dir[2] = upper_dir[2]; }
    else for (int i = 0; i < 3; ++i) lower_dir[i] /= length;
}

/* World position of a bone's origin: parent position plus its offset. */
static void bone_origin(const Mat4 global[], const Pose* pose, int parent, int node,
                        float out[3]) {
    out[0] = global[parent].m[12];
    out[1] = global[parent].m[13];
    out[2] = global[parent].m[14];
    float offset[3];
    mat4_transform_direction(&global[parent], pose->local[node].translation, offset);
    for (int c = 0; c < 3; ++c) out[c] += offset[c];
}

/* Rotates `node` so its rest axis points along model-space `direction`. */
static void point_bone(const Mat4 global[], int parent, const float rest_axis[3],
                       const float direction[3], Transform* local) {
    float local_target[3];
    character_axis_in_parent(parent >= 0 ? &global[parent] : NULL, direction,
                             local_target);
    quat_between(rest_axis, local_target, local->rotation);
}

/* Applies the pose's directions in one parent-first pass (each bone needs its
   parent's final global). The reaching arm is solved when its upper bone is
   reached. */
void character_authored_apply(const CharacterAnimation* animation,
                              const SkinnedModel* model,
                              const AuthoredPose* pose, Pose* out) {
    const Skeleton* skeleton = &model->skeleton;
    if (!pose || pose->slot < 0 || pose->slot >= CHARACTER_AUTHORED_POSES) return;
    if (animation->authored[pose->slot].count <= 0) return;

    int reaching = pose->reaches && animation->reach_valid &&
                   animation->reach_upper >= 0 && animation->reach_lower >= 0 &&
                   animation->reach_upper_length > 1e-5f &&
                   animation->reach_lower_length > 1e-5f;
    float upper_dir[3] = {0, 1, 0}, lower_dir[3] = {0, 1, 0};

    Mat4 global[SKIN_MAX_NODES];
    for (int i = 0; i < skeleton->node_count; ++i) {
        int node = skeleton->order[i];

        for (int e = 0; e < pose->count && e < CHARACTER_AUTHORED_BONES; ++e) {
            if (animation->authored[pose->slot].node[e] != node) continue;
            int parent = skeleton->parent[node];
            float target[3] = {pose->bones[e].x, pose->bones[e].y,
                               pose->bones[e].z};
            if (reaching && node == animation->reach_upper) {
                float shoulder[3];
                bone_origin(global, out, parent, node, shoulder);
                reach_two_bone(shoulder, animation->reach_target,
                               animation->reach_upper_length,
                               animation->reach_lower_length, pose->elbow_pole,
                               upper_dir, lower_dir);
                for (int c = 0; c < 3; ++c) target[c] = upper_dir[c];
            } else if (reaching && node == animation->reach_lower) {
                for (int c = 0; c < 3; ++c) target[c] = lower_dir[c];
            }
            float length = sqrtf(target[0]*target[0] + target[1]*target[1] +
                                 target[2]*target[2]);
            if (length < 1e-6f) break;
            for (int c = 0; c < 3; ++c) target[c] /= length;

            /* Into the parent's frame via its normalized basis transpose. */
            float local_target[3] = {target[0], target[1], target[2]};
            if (parent >= 0) {
                const Mat4* p = &global[parent];
                float basis[9];
                for (int c = 0; c < 3; ++c) {
                    float s = sqrtf(p->m[c*4+0]*p->m[c*4+0] + p->m[c*4+1]*p->m[c*4+1] +
                                    p->m[c*4+2]*p->m[c*4+2]);
                    if (s < 1e-8f) s = 1.0f;
                    for (int r = 0; r < 3; ++r) basis[c*3+r] = p->m[c*4+r] / s;
                }
                for (int c = 0; c < 3; ++c)
                    local_target[c] = basis[c*3+0]*target[0] + basis[c*3+1]*target[1] +
                                      basis[c*3+2]*target[2];
            }
            quat_between(animation->authored[pose->slot].axis[e], local_target,
                         out->local[node].rotation);
            break;
        }

        const Transform* local = &out->local[node];
        Mat4 matrix = mat4_from_trs(local->translation, local->rotation, local->scale);
        int parent = skeleton->parent[node];
        global[node] = parent < 0 ? matrix : mat4_multiply(global[parent], matrix);
    }
}

/* Solves one arm toward `target`. Returns 0 if bones are missing or the target
   is beyond REACH_LIMIT, leaving the pose untouched. */
static int reach_arm(const Skeleton* skeleton, const Mat4 global[], Pose* pose,
                     const char* upper_name, const char* lower_name,
                     const char* hand_name, const float target[3],
                     const float pole[3],
                     float upper_dir[3], float lower_dir[3]) {
    short upper = arm_find_node(skeleton, upper_name);
    short lower = arm_find_node(skeleton, lower_name);
    if (upper < 0 || lower < 0) return 0;
    float upper_length = arm_bone_length(skeleton, lower);
    float lower_length = arm_bone_length(skeleton, arm_find_node(skeleton, hand_name));
    if (upper_length < 1e-5f || lower_length < 1e-5f) return 0;

    int parent = skeleton->parent[upper];
    float shoulder[3] = {0.0f, 0.0f, 0.0f};
    if (parent >= 0) bone_origin(global, pose, parent, upper, shoulder);

    float span = 0.0f;
    for (int c = 0; c < 3; ++c) {
        float d = target[c] - shoulder[c];
        span += d * d;
    }
    span = sqrtf(span);
    if (span > (upper_length + lower_length) * REACH_LIMIT) return 0;

    reach_two_bone(shoulder, target, upper_length, lower_length, pole,
                   upper_dir, lower_dir);
    return 1;
}

/* Points a bone along a model-space direction; roll is left to the minimal
   rotation. */
static void point_along(const Skeleton* skeleton, const Mat4 global[],
                        int node, const float dir[3], Pose* pose) {
    float axis[3];
    if (!arm_rest_axis(skeleton, node, axis)) return;
    float length = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    if (length < 1e-6f) return;
    float unit[3] = {dir[0]/length, dir[1]/length, dir[2]/length};
    point_bone(global, skeleton->parent[node], axis, unit, &pose->local[node]);
}

void character_animation_reach_hands(const SkinnedModel* model,
                                     const float* right_target,
                                     const float* left_target, Pose* pose) {
    if (!model || !pose || (!right_target && !left_target)) return;
    const Skeleton* skeleton = &model->skeleton;

    short upper_r = arm_find_node(skeleton, "upperarm_r");
    short lower_r = arm_find_node(skeleton, "lowerarm_r");
    short upper_l = arm_find_node(skeleton, "upperarm_l");
    short lower_l = arm_find_node(skeleton, "lowerarm_l");
    float ru[3], rl[3], lu[3], ll[3];
    int did_r = 0, did_l = 0;

    Mat4 global[SKIN_MAX_NODES];
    for (int i = 0; i < skeleton->node_count; ++i) {
        int node = skeleton->order[i];

        if (right_target && node == upper_r) {
            /* Mirror the pole so the right elbow also bends outward. */
            const float pole_r[3] = {-HANG_ELBOW_POLE[0], HANG_ELBOW_POLE[1],
                                     HANG_ELBOW_POLE[2]};
            did_r = reach_arm(skeleton, global, pose, "upperarm_r", "lowerarm_r",
                              "hand_r", right_target, pole_r, ru, rl);
            if (did_r) point_along(skeleton, global, node, ru, pose);
        } else if (did_r && node == lower_r) {
            point_along(skeleton, global, node, rl, pose);
        } else if (left_target && node == upper_l) {
            did_l = reach_arm(skeleton, global, pose, "upperarm_l", "lowerarm_l",
                              "hand_l", left_target, HANG_ELBOW_POLE, lu, ll);
            if (did_l) point_along(skeleton, global, node, lu, pose);
        } else if (did_l && node == lower_l) {
            point_along(skeleton, global, node, ll, pose);
        }

        const Transform* local = &pose->local[node];
        Mat4 matrix = mat4_from_trs(local->translation, local->rotation,
                                    local->scale);
        int parent = skeleton->parent[node];
        global[node] = parent < 0 ? matrix : mat4_multiply(global[parent], matrix);
    }
}
