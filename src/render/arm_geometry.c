/* The arm arithmetic that holds no state - see render/arm_geometry.h for what
   that means and why it is worth a file of its own.

   Nothing here was rewritten. It was lifted out of viewmodel_arms.c, where it
   sat between the calibration that fills a long-lived struct and the draw that
   reads it, belonging to neither. */
#include "render/arm_geometry.h"
#include "gameplay/movement_tuning.h"
#include "render/weapon_grip.h"
#include "utils/math_utils.h"

#include <math.h>
#include <string.h>

short arm_find_node(const Skeleton* skeleton, const char* name) {
    for (int i = 0; i < skeleton->node_count; ++i)
        if (strcmp(skeleton->node_names[i], name) == 0) return (short)i;
    return -1;
}

/* The direction a bone points in its parent's frame at rest: toward its first
   child, because a bone's length and heading are recorded on the child rather
   than on itself. The same measurement the ledge hang's solver takes. */
int arm_rest_axis(const Skeleton* skeleton, int node, float out[3]) {
    for (int i = 0; i < skeleton->node_count; ++i) {
        if (skeleton->parent[i] != node) continue;
        const float* t = skeleton->bind_local[i].translation;
        float length = sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
        if (length < 1e-6f) continue;
        out[0] = t[0]/length; out[1] = t[1]/length; out[2] = t[2]/length;
        return 1;
    }
    return 0;
}

float arm_bone_length(const Skeleton* skeleton, int node) {
    if (node < 0) return 0.0f;
    const float* t = skeleton->bind_local[node].translation;
    return sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
}

int arm_resolve(const Skeleton* skeleton, ArmChain* arm,
                       const char* upper, const char* lower, const char* hand,
                       float side) {
    arm->upper = arm_find_node(skeleton, upper);
    arm->lower = arm_find_node(skeleton, lower);
    arm->hand = arm_find_node(skeleton, hand);
    if (arm->upper < 0 || arm->lower < 0 || arm->hand < 0) return 0;
    if (!arm_rest_axis(skeleton, arm->upper, arm->upper_axis)) return 0;
    if (!arm_rest_axis(skeleton, arm->lower, arm->lower_axis)) return 0;
    /* The hand's own rest axis runs toward whichever finger the exporter listed
       first, which is near enough "along the fingers" to aim the wrist with. */
    if (!arm_rest_axis(skeleton, arm->hand, arm->hand_axis)) return 0;
    /* A bone is as long as the distance to the child that ends it. */
    arm->upper_length = arm_bone_length(skeleton, arm->lower);
    arm->lower_length = arm_bone_length(skeleton, arm->hand);
    arm->side = side;
    return arm->upper_length > 1e-5f && arm->lower_length > 1e-5f;
}

/* Which way the palm faces, in the hand bone's own frame. Off the rig rather
   than stated, the way every other measurement in here is: a palm is square to
   both the hand's length and the line its knuckles sit on, which the bind pose
   gives directly - the knuckle bones are children of the hand, so their rest
   translations are already in the frame the answer is wanted in.

   A cross product settles the axis and not its sign, and the sign is the whole
   point: get it backwards and the hand comes out palm-up. The thumb decides it,
   because the thumb's base sits on the palm's side of the hand and does so on
   both hands - this rig mirrors, so the cross product alone comes out the same
   way round on the left as on the right and would put one palm inside out.

   No finger bones, no measurement, and the wrist keeps the free spin it has
   always had: a hand that rolls is better than one rolled to a guess. */
void arm_measure_palm(const Skeleton* skeleton, ArmChain* arm,
                         const char* index, const char* pinky,
                         const char* thumb) {
    arm->has_palm = 0;
    arm->palm_axis[0] = arm->palm_axis[1] = arm->palm_axis[2] = 0.0f;

    short index_node = arm_find_node(skeleton, index);
    short pinky_node = arm_find_node(skeleton, pinky);
    short thumb_node = arm_find_node(skeleton, thumb);
    if (index_node < 0 || pinky_node < 0 || thumb_node < 0) return;

    const float* from_index = skeleton->bind_local[index_node].translation;
    const float* from_pinky = skeleton->bind_local[pinky_node].translation;
    float knuckles[3];
    for (int c = 0; c < 3; ++c) knuckles[c] = from_index[c] - from_pinky[c];

    float normal[3] = {
        knuckles[1] * arm->hand_axis[2] - knuckles[2] * arm->hand_axis[1],
        knuckles[2] * arm->hand_axis[0] - knuckles[0] * arm->hand_axis[2],
        knuckles[0] * arm->hand_axis[1] - knuckles[1] * arm->hand_axis[0],
    };
    float length = sqrtf(normal[0]*normal[0] + normal[1]*normal[1] +
                         normal[2]*normal[2]);
    if (length < 1e-6f) return;
    for (int c = 0; c < 3; ++c) normal[c] /= length;

    const float* to_thumb = skeleton->bind_local[thumb_node].translation;
    if (normal[0]*to_thumb[0] + normal[1]*to_thumb[1] +
        normal[2]*to_thumb[2] < 0.0f)
        for (int c = 0; c < 3; ++c) normal[c] = -normal[c];

    memcpy(arm->palm_axis, normal, sizeof arm->palm_axis);
    arm->has_palm = 1;
}

/* The hand's own basis, from the two axes already measured: along the fingers,
   out of the palm, and across the knuckles. Orthonormal, because the two
   measurements are near square but not exactly so and everything downstream
   assumes a rotation. */
void arm_hand_basis(const float along[3], const float palm[3],
                       float u[3], float v[3], float w[3]) {
    memcpy(u, along, sizeof(float) * 3);
    float length = sqrtf(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]);
    if (length < 1e-8f) length = 1.0f;
    for (int c = 0; c < 3; ++c) u[c] /= length;

    float dot = palm[0]*u[0] + palm[1]*u[1] + palm[2]*u[2];
    for (int c = 0; c < 3; ++c) v[c] = palm[c] - u[c] * dot;
    length = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (length < 1e-8f) {
        /* A palm pointing along its own fingers is not a hand. Any square
           direction will do rather than a divide by zero; the caller's own
           guards keep it from being used. */
        v[0] = u[1]; v[1] = -u[0]; v[2] = 0.0f;
        length = sqrtf(v[0]*v[0] + v[1]*v[1]);
        if (length < 1e-8f) { v[0] = 1.0f; v[1] = v[2] = 0.0f; length = 1.0f; }
    }
    for (int c = 0; c < 3; ++c) v[c] /= length;

    w[0] = u[1]*v[2] - u[2]*v[1];
    w[1] = u[2]*v[0] - u[0]*v[2];
    w[2] = u[0]*v[1] - u[1]*v[0];
}

/* WHERE THE FIST ACTUALLY HOLDS SOMETHING, relative to its own wrist.

   The arm solver reaches a point, and the point it was given was the gun's
   grip - so the WRIST landed on the grip and the fist, which is a hand's length
   of closed hand on this rig, shut on the air in front of it. On screen that is
   a hand floating off the pistol grip with the grip visible through the hole it
   should be filling. No amount of getting the wrist's ANGLE right fixes it; the
   hand is in the wrong place.

   Measured off the MESH, not off the bones. The first attempt at this took the
   midpoint between each knuckle and its own curled fingertip, on the reasoning
   that both sit on the surface of whatever is being held - true of a hand
   wrapped round a bar, and wrong here, because this rig's hand is a CLOSED
   fist and there is no bar and no hole. It put the hold point up among the
   fingers, and on screen the grip came out of the bottom of the fist.

   What a closed fist can do is swallow the grip, and for that the point that
   has to land on it is the middle of the fist's own mass. That is the centroid
   of the vertices the hand and its fingers own, in the pose they are actually
   drawn in - which is a thing this rig can be asked directly. */
void arm_measure_grip_centre(const SkinnedModel* model, const Pose* fist,
                                SkinnedInstance* skinned, ArmChain* arm,
                                const char* hand) {
    arm->has_grip = 0;
    arm->grip_centre[0] = arm->grip_centre[1] = arm->grip_centre[2] = 0.0f;
    if (!arm->has_palm) return;

    /* Which nodes are this hand's, so a vertex can be asked whether it belongs
       to the fist or to the forearm above it. */
    PoseMask owned;
    if (!skinned_model_mask_subtree(model, hand, &owned)) return;

    Mat4 hand_global;
    if (!skinned_model_bone_matrix(model, fist, hand, &hand_global)) return;

    /* The fist as it is drawn. */
    skinned_model_skin(model, fist, skinned);

    const Skeleton* skeleton = &model->skeleton;
    float centre[3] = {0, 0, 0};
    int counted = 0;
    for (int m = 0; m < model->mesh_count && m < skinned->mesh_count; ++m) {
        const SkinnedMesh* mesh = &model->meshes[m];
        const SkinnedMeshVertices* posed = &skinned->meshes[m];
        if (!mesh->joints || !mesh->weights || !posed->vertices) continue;
        int count = mesh->vertex_count < posed->vertex_count
                  ? mesh->vertex_count : posed->vertex_count;
        for (int v = 0; v < count; ++v) {
            /* The joint that owns the vertex, which is the one it follows. A
               vertex split between a finger and the forearm belongs to
               whichever pulls harder. */
            int best = -1;
            float most = 0.0f;
            for (int i = 0; i < SKIN_INFLUENCES; ++i) {
                float w = mesh->weights[v * SKIN_INFLUENCES + i];
                if (w <= most) continue;
                most = w;
                best = mesh->joints[v * SKIN_INFLUENCES + i];
            }
            if (best < 0 || best >= skeleton->joint_count) continue;
            int node = skeleton->joint_node[best];
            if (node < 0 || node >= SKIN_MAX_NODES || !owned.node[node]) continue;
            const float* p = &posed->vertices[v * SKIN_VERTEX_FLOATS];
            for (int c = 0; c < 3; ++c) centre[c] += p[c];
            ++counted;
        }
    }
    if (!counted) return;
    for (int c = 0; c < 3; ++c) centre[c] /= (float)counted;

    /* Into the hand's own frame, about its own origin. */
    float basis[3][3];
    for (int c = 0; c < 3; ++c) {
        float length = sqrtf(hand_global.m[c*4+0]*hand_global.m[c*4+0] +
                             hand_global.m[c*4+1]*hand_global.m[c*4+1] +
                             hand_global.m[c*4+2]*hand_global.m[c*4+2]);
        if (length < 1e-8f) length = 1.0f;
        for (int r = 0; r < 3; ++r) basis[c][r] = hand_global.m[c*4+r] / length;
    }
    float world[3], local[3];
    for (int c = 0; c < 3; ++c) world[c] = centre[c] - hand_global.m[12+c];
    for (int c = 0; c < 3; ++c)
        local[c] = basis[c][0]*world[0] + basis[c][1]*world[1] +
                   basis[c][2]*world[2];

    float u[3], v[3], w[3];
    arm_hand_basis(arm->hand_axis, arm->palm_axis, u, v, w);
    arm->grip_centre[0] = local[0]*u[0] + local[1]*u[1] + local[2]*u[2];
    arm->grip_centre[1] = local[0]*v[0] + local[1]*v[1] + local[2]*v[2];
    arm->grip_centre[2] = local[0]*w[0] + local[1]*w[1] + local[2]*w[2];
    arm->has_grip = 1;
}

/* The point the WRIST has to reach for the fist to close on `grip`.

   The grip, walked back down the hand by the fist's own dimensions. `fingers`
   and `palm` are where the hand will be aimed this frame, in whatever space
   `grip` is in - the offset is a rotation of a measured vector, so it works in
   camera space for the solve and in the rig's for the draw without either
   having to convert the other's.

   ONLY THE FIRING GRIP WANTS THIS, and the specs say which points are which.
   weapon_specs.c calls grip_right "the middle of it - which is what a fist
   closes on", and that is a point on the gun: hand it to a solver that reaches
   with the wrist and the fist shuts on the air in front of the grip. It calls
   grip_left the place "the support wrist sits just under it", and the reload
   track's keys are hand positions too - authored by eye against a wrist, down
   to the one that puts the hand inside a pouch. Those already mean what the
   solver wants and walking them back would move the whole magazine change ten
   centimetres short of everywhere it was drawn to reach. */
void arm_wrist_from_grip(const ArmChain* arm, const float fingers[3],
                            const float palm[3], const float grip[3],
                            float out[3]) {
    memcpy(out, grip, sizeof(float) * 3);
    if (!arm->has_grip) return;
    float u[3], v[3], w[3];
    arm_hand_basis(fingers, palm, u, v, w);
    for (int c = 0; c < 3; ++c)
        out[c] -= u[c] * arm->grip_centre[0] + v[c] * arm->grip_centre[1] +
                  w[c] * arm->grip_centre[2];
}

/* The bind pose global of one node, walking only its own ancestors. */
Mat4 arm_bind_global(const Skeleton* skeleton, int node) {
    Mat4 out = mat4_identity();
    int chain[SKIN_MAX_NODES];
    int depth = 0;
    for (int n = node; n >= 0 && depth < SKIN_MAX_NODES; n = skeleton->parent[n])
        chain[depth++] = n;
    for (int i = depth - 1; i >= 0; --i) {
        const Transform* t = &skeleton->bind_local[chain[i]];
        out = mat4_multiply(out, mat4_from_trs(t->translation, t->rotation,
                                              t->scale));
    }
    return out;
}

/* One reload key's support-hand target in camera space.

   `gun` is the transform to read gun-space points through: the resting reload
   pose while placements are being solved, and the LIVE weapon matrix while the
   arms are drawn. A key on the chest is in camera space already and ignores it,
   which is the whole reason a key states its frame - the pouch stays on the
   body while the weapon moves, and every other point on the track does the
   opposite. */
void arm_key_hand(const ActionKey* key, const ViewmodelPose* gun,
                     float out[3]) {
    if (key->hand_frame == ACTION_IN_VIEW) {
        for (int c = 0; c < 3; ++c) out[c] = key->hand[c];
        return;
    }
    viewmodel_pose_transform_point(gun, key->hand, out);
}

/* The same, against the live weapon transform rather than a resting pose. Two
   functions rather than one because the solve works in poses and the draw works
   in matrices, and neither should have to build the other's. */
void arm_key_hand_live(const ActionKey* key, const Mat4* gun, float out[3]) {
    if (key->hand_frame == ACTION_IN_VIEW) {
        for (int c = 0; c < 3; ++c) out[c] = key->hand[c];
        return;
    }
    mat4_transform_point(gun, key->hand, out);
}

void arm_direction_normalize(float direction[3]) {
    float length = sqrtf(direction[0] * direction[0] +
                         direction[1] * direction[1] +
                         direction[2] * direction[2]);
    if (length < 1e-6f) return;
    for (int c = 0; c < 3; ++c) direction[c] /= length;
}

void arm_direction_blend(const float from[3], const float to[3], float t,
                            float out[3]) {
    t = clampf(t, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    for (int c = 0; c < 3; ++c) out[c] = lerpf(from[c], to[c], t);
    arm_direction_normalize(out);
}

/* The support wrist changes jobs during a reload. Its target already follows
   the magazine and pouch, but previously its direction remained the handguard
   direction for the entire track. That left the fist pointing along the rifle
   while the arm carried a vertical magazine, then twisted it through the pouch
   and insertion.

   These are directions only: they change the hand bone and nothing upstream of
   the wrist. Position, elbow, shoulder placement, reload timing and the
   magazine itself remain exactly where the existing track puts them. */
void arm_reload_support_direction(const WeaponSpec* spec,
                                     const ViewmodelBlend* blend,
                                     const Mat4* gun,
                                     const float resting[3], float out[3],
                                     float* wrist_roll) {
    memcpy(out, resting, sizeof(float) * 3);
    *wrist_roll = 0.0f;
    if (blend->reload <= 0.0f) return;

    const ActionTrack* track = weapon_spec_reload_sequence(spec);
    ActionSample sample;
    action_track_sample(track, blend->reload_progress, &sample);
    float progress = blend->reload_progress;

    /* Across the magazine rather than along the handguard. Pitch and roll are
       sampled from the same keys as the visible magazine, so the wrist rocks
       with the fresh magazine as its front lip enters the well. */
    float pitch = lerpf(sample.from->mag_pitch, sample.to->mag_pitch, sample.t) *
                  MATH_DEG_TO_RAD;
    float roll = lerpf(sample.from->mag_roll, sample.to->mag_roll, sample.t) *
                 MATH_DEG_TO_RAD;
    float magazine_local[3] = {0.86f, -0.18f, -0.48f};
    float cp = cosf(pitch), sp = sinf(pitch);
    float cr = cosf(roll), sr = sinf(roll);
    float pitched[3] = {
        magazine_local[0],
        magazine_local[1] * cp - magazine_local[2] * sp,
        magazine_local[1] * sp + magazine_local[2] * cp,
    };
    float rolled[3] = {
        pitched[0] * cr - pitched[1] * sr,
        pitched[0] * sr + pitched[1] * cr,
        pitched[2],
    };
    float magazine[3];
    mat4_transform_direction(gun, rolled, magazine);
    arm_direction_normalize(magazine);

    /* At the pouch the fingers follow the forearm down and slightly inward,
       instead of retaining a weapon-relative twist while the hand belongs to
       the body. The tap turns the fist upward under the floorplate. */
    const float pouch[3] = {-0.18f, -0.96f, -0.22f};
    const float tap_local[3] = {0.18f, 0.96f, -0.22f};
    float tap[3];
    mat4_transform_direction(gun, tap_local, tap);
    arm_direction_normalize(tap);

    float wanted[3];
    if (progress < 0.10f) {
        memcpy(wanted, resting, sizeof wanted);
    } else if (progress < 0.24f) {
        arm_direction_blend(resting, magazine,
                        (progress - 0.10f) / 0.14f, wanted);
    } else if (progress < 0.34f) {
        memcpy(wanted, magazine, sizeof wanted);
    } else if (progress < 0.51f) {
        arm_direction_blend(magazine, pouch,
                        (progress - 0.34f) / 0.17f, wanted);
    } else if (progress < 0.58f) {
        memcpy(wanted, pouch, sizeof wanted);
    } else if (progress < 0.64f) {
        arm_direction_blend(pouch, magazine,
                        (progress - 0.58f) / 0.06f, wanted);
    } else if (progress < 0.83f) {
        memcpy(wanted, magazine, sizeof wanted);
    } else if (progress < 0.87f) {
        arm_direction_blend(magazine, tap,
                        (progress - 0.83f) / 0.04f, wanted);
    } else if (progress < 0.95f) {
        memcpy(wanted, tap, sizeof wanted);
    } else {
        arm_direction_blend(tap, resting,
                        (progress - 0.95f) / 0.05f, wanted);
    }

    arm_direction_blend(resting, wanted, blend->reload, out);

    /* Direction alone cannot orient a wrist: it leaves an unconstrained spin
       around the fingers. Close the palm around the magazine from its left
       side, relax that roll as the empty hand enters the pouch, and flatten it
       under the floorplate for the tap. */
    float wanted_roll;
    if (progress < 0.10f) wanted_roll = 0.0f;
    else if (progress < 0.24f)
        wanted_roll = lerpf(0.0f, -72.0f, (progress - 0.10f) / 0.14f);
    else if (progress < 0.34f) wanted_roll = -72.0f;
    else if (progress < 0.51f)
        wanted_roll = lerpf(-72.0f, -18.0f, (progress - 0.34f) / 0.17f);
    else if (progress < 0.58f) wanted_roll = -18.0f;
    else if (progress < 0.64f)
        wanted_roll = lerpf(-18.0f, -72.0f, (progress - 0.58f) / 0.06f);
    else if (progress < 0.83f) wanted_roll = -72.0f;
    else if (progress < 0.87f)
        wanted_roll = lerpf(-72.0f, 0.0f, (progress - 0.83f) / 0.04f);
    else wanted_roll = 0.0f;
    *wrist_roll = wanted_roll * blend->reload;
}

/* Points one bone along `target`, given in the model's space, by rotating its
   rest axis onto it inside its parent's frame. Lifted from the ledge hang's
   apply pass, including the reason the parent's basis is normalized first: the
   caller scales the rig, and a scaled basis would tilt the direction rather
   than only rotating it. */
void arm_point_bone(const Skeleton* skeleton, const Mat4 global[],
                       int node, const float axis[3], const float target[3],
                       Pose* pose) {
    float length = sqrtf(target[0]*target[0] + target[1]*target[1] +
                         target[2]*target[2]);
    if (length < 1e-6f) return;
    float dir[3] = {target[0]/length, target[1]/length, target[2]/length};

    int parent = skeleton->parent[node];
    float local[3] = {dir[0], dir[1], dir[2]};
    if (parent >= 0) {
        const Mat4* p = &global[parent];
        for (int c = 0; c < 3; ++c) {
            float s = sqrtf(p->m[c*4+0]*p->m[c*4+0] + p->m[c*4+1]*p->m[c*4+1] +
                            p->m[c*4+2]*p->m[c*4+2]);
            if (s < 1e-8f) s = 1.0f;
            /* Transpose of a rotation is its inverse. */
            local[c] = (p->m[c*4+0]*dir[0] + p->m[c*4+1]*dir[1] +
                        p->m[c*4+2]*dir[2]) / s;
        }
    }
    quat_between(axis, local, pose->local[node].rotation);
}

/* Two bones reaching a point. The law of cosines gives the angle the upper arm
   makes with the straight line to the target and the pole decides which way
   round that cone the elbow sits. A target further away than the arm is long is
   pulled in to arm's length rather than refused, so a gun held too far out
   straightens the arm instead of tearing it off - which matters here, because
   full ADS pulls the rifle to within a few centimetres of the eye and the
   solver spends as much time at the folded limit as the straight one. */
void arm_reach(const float shoulder[3], const float target[3],
                  float upper_length, float lower_length, float side,
                  float upper_dir[3], float lower_dir[3]) {
    float to[3];
    for (int c = 0; c < 3; ++c) to[c] = target[c] - shoulder[c];
    float distance = sqrtf(to[0]*to[0] + to[1]*to[1] + to[2]*to[2]);
    if (distance < 1e-5f) { distance = 1e-5f; to[1] = -distance; }

    float axis[3];
    for (int c = 0; c < 3; ++c) axis[c] = to[c] / distance;

    float shortest = fabsf(upper_length - lower_length) + 1e-4f;
    float longest = upper_length + lower_length - 1e-4f;
    if (distance < shortest) distance = shortest;
    if (distance > longest) distance = longest;

    float cosine = (upper_length * upper_length + distance * distance -
                    lower_length * lower_length) /
                   (2.0f * upper_length * distance);
    float angle = acosf(clampf(cosine, -1.0f, 1.0f));

    float pole[3] = {ARMS_ELBOW_POLE[0] * side, ARMS_ELBOW_POLE[1],
                     ARMS_ELBOW_POLE[2]};
    float dot = pole[0]*axis[0] + pole[1]*axis[1] + pole[2]*axis[2];
    float bend[3];
    for (int c = 0; c < 3; ++c) bend[c] = pole[c] - axis[c] * dot;
    float length = sqrtf(bend[0]*bend[0] + bend[1]*bend[1] + bend[2]*bend[2]);
    if (length < 1e-5f) {
        bend[0] = axis[1]; bend[1] = -axis[0]; bend[2] = 0.0f;
        length = sqrtf(bend[0]*bend[0] + bend[1]*bend[1]);
        if (length < 1e-5f) { bend[0] = 1.0f; bend[1] = bend[2] = 0.0f;
                              length = 1.0f; }
    }
    for (int c = 0; c < 3; ++c) bend[c] /= length;

    float elbow[3];
    for (int c = 0; c < 3; ++c) {
        upper_dir[c] = axis[c] * cosf(angle) + bend[c] * sinf(angle);
        elbow[c] = shoulder[c] + upper_dir[c] * upper_length;
        lower_dir[c] = target[c] - elbow[c];
    }
    length = sqrtf(lower_dir[0]*lower_dir[0] + lower_dir[1]*lower_dir[1] +
                   lower_dir[2]*lower_dir[2]);
    if (length < 1e-5f) memcpy(lower_dir, upper_dir, sizeof(float) * 3);
    else for (int c = 0; c < 3; ++c) lower_dir[c] /= length;
}

/* How far the hand still has to turn about its own fingers for the palm to face
   where the gun says it should, in degrees. Zero when the rig gave no palm axis
   to turn or the gun asks for no particular facing.

   This is the roll that arm_point_bone leaves free. It is measured after the
   pointing rather than authored alongside it, because what it has to cancel is
   not a constant: it is whatever roll the shoulder and elbow arrived at, and
   that changes with every target the arm reaches for.

   Both directions are flattened across the fingers first. A turn about an axis
   cannot change what anything does along it, so the component along the fingers
   is not this rotation's to fix and would only bias the angle if it were left
   in. */
float arm_palm_roll(const Skeleton* skeleton, const Mat4 global[], int node,
                       const ArmChain* arm, const Pose* pose) {
    if (!arm->has_palm) return 0.0f;
    float want[3] = {arm->palm_dir[0], arm->palm_dir[1], arm->palm_dir[2]};
    if (want[0]*want[0] + want[1]*want[1] + want[2]*want[2] < 1e-8f) return 0.0f;

    float axis[3] = {arm->hand_dir[0], arm->hand_dir[1], arm->hand_dir[2]};
    float length = sqrtf(axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2]);
    if (length < 1e-6f) return 0.0f;
    for (int c = 0; c < 3; ++c) axis[c] /= length;

    /* Where the palm points now, with the roll still free. The hand's own
       global, which the caller has not composed yet at this point in the pass -
       it is the bone being posed. */
    const Transform* local = &pose->local[node];
    Mat4 matrix = mat4_from_trs(local->translation, local->rotation,
                                local->scale);
    int parent = skeleton->parent[node];
    Mat4 hand = parent < 0 ? matrix : mat4_multiply(global[parent], matrix);
    float have[3];
    mat4_transform_direction(&hand, arm->palm_axis, have);

    float along_have = have[0]*axis[0] + have[1]*axis[1] + have[2]*axis[2];
    float along_want = want[0]*axis[0] + want[1]*axis[1] + want[2]*axis[2];
    for (int c = 0; c < 3; ++c) {
        have[c] -= axis[c] * along_have;
        want[c] -= axis[c] * along_want;
    }
    float have_length = sqrtf(have[0]*have[0] + have[1]*have[1] +
                              have[2]*have[2]);
    float want_length = sqrtf(want[0]*want[0] + want[1]*want[1] +
                              want[2]*want[2]);
    /* Either one flat against the fingers leaves no angle to measure - a palm
       facing along its own fingers is not a hand, and asking for one says the
       table and the rig disagree. Better to leave the wrist where the pointing
       put it than to turn it by a rounding error. */
    if (have_length < 1e-4f || want_length < 1e-4f) return 0.0f;
    for (int c = 0; c < 3; ++c) { have[c] /= have_length; want[c] /= want_length; }

    /* Signed about the fingers, so the hand turns the short way and in the
       direction the axis says rather than always positively. */
    float cross[3] = {
        have[1]*want[2] - have[2]*want[1],
        have[2]*want[0] - have[0]*want[2],
        have[0]*want[1] - have[1]*want[0],
    };
    float sine = cross[0]*axis[0] + cross[1]*axis[1] + cross[2]*axis[2];
    float cosine = have[0]*want[0] + have[1]*want[1] + have[2]*want[2];
    return atan2f(sine, cosine) / MATH_DEG_TO_RAD;
}
