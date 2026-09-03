#include "render/viewmodel_arms.h"
#include "render/arm_placement.h"
#include "utils/mat4.h"
#include "utils/math_utils.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- where the shoulders sit ---
   Camera space: +x right, +y up, -z forward.

   SOLVED, not tuned. Two numbers per pose - how far forward the shoulder line
   sits and how far the stance is bladed - chosen at load time so that both of
   this gun's grips come out at a comfortable bend. They used to be constants,
   and constants meant they were the M4's: they were picked so that ITS grip at
   0.37m and ITS handguard at 0.72m were both reachable, and any gun of a
   different length would have had one arm straightened and the other folded.

   The search itself is arm_placement.c, which knows nothing about a skeleton
   and can therefore be run against the real rig without a window - what a
   silently-falling-back search needs and could not have while it lived in here.
   What stays in this file is everything that needs bones.

   Why solved per pose: the gun travels between the hip, the chest and the eye
   - 0.34m from hip to eye on the rifle - so one placement cannot serve all
   three ends. They are crossfaded on the weapon's own `sprint` and `ads`, like
   everything else here.

   The sprint carry is the one that made a third placement unavoidable rather
   than merely tidy. Measured against the placement the hip solve picks for the
   rifle, the carry leaves the firing arm at 0.41 of its reach and the support
   arm at 0.28 - a folded arm, which throws its elbow across the sight picture.
   Given its own placement both come back to 0.72, which is the comfort target.
   Most of that is the rotation: without the cant the support arm reaches 0.43,
   so it is the roll and the yaw that put the handguard out of reach, not the
   0.16m the gun moved. */

/* Which way the fingers point, in the GUN's own frame, so the wrists follow the
   weapon rather than the body. Rotating a bone onto a direction leaves its roll
   free, and these are what decide it.

   Per hold rather than per gun, because what a wrist does is decided by what it
   is holding and not by which gun it is: every rifle's support hand runs along
   a handguard, and every pistol's wraps the firing hand. A gun states which of
   these it is and gets both wrists for free.

   This rig's hand is a closed fist, 0.13m from wrist to fingertip against a
   handguard 0.06m across - it cannot wrap the tube, so whatever it does not wrap
   overshoots. Aimed across and up the support fist crossed the bore and came out
   the far side of the receiver level with the optic; run forward along the tube
   the overshoot goes where a support hand already is. */
static const float ARMS_HAND_DIR[2][2][3] = {
    /* TWO_POINT: firing hand forward across the grip, support hand along the
       handguard with a little wrap. */
    { { 0.00f, -0.15f, -0.99f }, { 0.30f,  0.10f, -0.95f } },
    /* HANDS_TOGETHER: both forward, the support hand rolled slightly under the
       firing one rather than across a tube it does not have. */
    { { 0.00f, -0.15f, -0.99f }, { 0.18f, -0.28f, -0.94f } },
};

/* Which way the PALM faces, in the gun's own frame. The second half of a wrist,
   and the half that was missing.

   Pointing a bone onto a direction is a minimal rotation, so aiming the fingers
   settles where they go and leaves the palm free to spin around them - and what
   it spun to was whatever the elbow's own unconstrained roll happened to hand
   down. Measured in the gun's frame, the same firing grip came out palm-inward
   at the hip and palm-DOWN at the shoulder and through the sprint carry: a
   quarter turn of wrist between two poses of one hold, on a hand that never let
   go of the grip. The arm was tracking the gun and the wrist was not.

   The shooting hand wraps the grip from its right, so the palm faces across the
   gun toward the centreline, tilted a fifth down - which is what puts the thumb
   side up the grip's rearward cant rather than straight up. Both holds are a
   fist on a pistol grip and want the same answer; the table is per hold anyway
   so a gun held some other way has somewhere to say so.

   The support hand is deliberately left free (all zeroes). It is the hand the
   reload track authors a roll for, key by key, against this same spin -
   constraining it means re-basing every one of those angles, and a pass at that
   is what this reverted from. Its wrist has the same defect and wants the same
   fix, once the reload rolls are re-measured against a base that holds still
   and the result is looked at rather than reasoned about. */
static const float ARMS_PALM_DIR[2][2][3] = {
    /* TWO_POINT */
    { { -1.00f, -0.20f, 0.00f }, { 0.00f, 0.00f, 0.00f } },
    /* HANDS_TOGETHER */
    { { -1.00f, -0.20f, 0.00f }, { 0.00f, 0.00f, 0.00f } },
};

/* Which way the elbows break: down and out, the way a shouldered rifle holds
   them. Mirrored on x for the left arm. */
static const float ARMS_ELBOW_POLE[3] = {0.55f, -0.78f, 0.30f};

/* The clip the hands and fingers are taken from. The arms are solved, but a
   hand is twenty-two bones and none of them are worth solving: what a grip
   looks like is authored, and this rig ships one. Sampled at a fixed time
   because it is wanted as a pose and not as a motion.

   Without it the fingers stay in the bind pose - splayed straight out - and a
   0.19m spread of straight finger at a third of a metre from the eye subtends
   27 degrees. That is not a hand near a gun, it is a hand filling the screen,
   and it was the first thing that went wrong here. */
/* Idle_Loop, and not one of the Pistol_ clips, which is not the obvious answer.
   Measured wrist-to-fingertip on this rig: the bind pose spreads 0.255m, the
   pistol poses 0.204m - they hold a pistol grip open-handed - and a whole family
   of clips including Idle_Loop, Punch_Jab and Sprint_Loop share one closed hand
   at 0.131m. That shared pose is the rig's fist, and a fist is what goes round
   a grip. Any of them would do; this one is named for what it is. */
#define ARMS_GRIP_CLIP "Idle_Loop"

/* As many keys as the reload track is allowed. Raising it costs two floats a
   key and one more scan at load. */
#define ARMS_MAX_RELOAD_KEYS 16

/* Which way the fingers point, in the GUN's own frame, so the wrists follow the
   weapon rather than the body. Rotating a bone onto a direction leaves its roll
   free, and these are what decide it.

   Tuning knobs, and the two most likely to want moving: they are what makes a
   hand read as holding the gun rather than as touching it. The right hand wraps
   a pistol grip, so its fingers run forward and a little down across the grip;
   the left supports the handguard from the side, so its fingers run across the
   barrel and over the top. */

enum { ARMS_UNTRIED, ARMS_READY, ARMS_FAILED };

typedef struct {
    short upper, lower, hand;
    float upper_axis[3], lower_axis[3], hand_axis[3];
    /* Where the fingers should point, in camera space, refreshed every frame
       off the gun's transform. */
    float hand_dir[3];
    /* Which way the palm faces out of this hand, in the hand bone's own frame.
       Measured off the rig, because it is a fact about the skeleton rather than
       a number to choose. Zero, and has_palm clear, when the rig did not give
       up enough finger bones to measure it. */
    float palm_axis[3];
    int has_palm;
    /* Roll about hand_dir. Pointing the hand fixes where the fingers go but
       leaves the palm free to spin around them; reload grips need that second
       degree of freedom to keep the wrist from turning inside-out. */
    float hand_roll;
    /* Where that palm should face this frame, in the rig's space, off the gun.
       Zero for a hand whose roll is left free - see ARMS_PALM_DIR. */
    float palm_dir[3];
    /* Where the closed fist's grip actually is, relative to the wrist: how far
       along the fingers, how far toward the palm, and how far across. Measured
       off the rig in its own units, and the reason is the whole of
       wrist_from_grip below. Zero, with has_grip clear, when the fingers could
       not be found to measure it. */
    float grip_centre[3];
    int has_grip;
    float upper_length, lower_length;
    /* +1 for the right arm, -1 for the left. Flips the elbow pole, which is the
       only thing about the two sides that is not simply the rig's own mirror. */
    float side;
} ArmChain;

static struct {
    int state;
    const SkinnedModel* model;
    SkinnedInstance instance;
    SkinnedSubset subset;
    ArmChain arms[2];
    /* The authored grip, or -1 if the rig has no such clip - in which case the
       fingers stay in the bind pose and look it, which is a worse hand rather
       than no hand. */
    int grip_clip;
    /* What the placement search needs off this rig: the shoulders, and the
       scale that turns its units into metres. Measured here, where the bones
       are; used there, where there are none. */
    ArmRig rig;

    /* The solved placement of each carry, and the spec they were solved for. A
       weapon swap re-solves; nothing else does. */
    const WeaponSpec* solved_for;
    ArmPlacement hip, sprint, ads;
    /* One per key of the reload track, solved at load like the three above.
       The support hand leaves the weapon during a reload and no single
       placement follows it, so the shoulders walk between these as it goes. */
    ArmPlacement reload[ARMS_MAX_RELOAD_KEYS];
    int reload_count;
} g_arms;

static short find_node(const Skeleton* skeleton, const char* name) {
    for (int i = 0; i < skeleton->node_count; ++i)
        if (strcmp(skeleton->node_names[i], name) == 0) return (short)i;
    return -1;
}

/* The direction a bone points in its parent's frame at rest: toward its first
   child, because a bone's length and heading are recorded on the child rather
   than on itself. The same measurement the ledge hang's solver takes. */
static int rest_axis(const Skeleton* skeleton, int node, float out[3]) {
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

static float bone_length(const Skeleton* skeleton, int node) {
    if (node < 0) return 0.0f;
    const float* t = skeleton->bind_local[node].translation;
    return sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
}

static int resolve_arm(const Skeleton* skeleton, ArmChain* arm,
                       const char* upper, const char* lower, const char* hand,
                       float side) {
    arm->upper = find_node(skeleton, upper);
    arm->lower = find_node(skeleton, lower);
    arm->hand = find_node(skeleton, hand);
    if (arm->upper < 0 || arm->lower < 0 || arm->hand < 0) return 0;
    if (!rest_axis(skeleton, arm->upper, arm->upper_axis)) return 0;
    if (!rest_axis(skeleton, arm->lower, arm->lower_axis)) return 0;
    /* The hand's own rest axis runs toward whichever finger the exporter listed
       first, which is near enough "along the fingers" to aim the wrist with. */
    if (!rest_axis(skeleton, arm->hand, arm->hand_axis)) return 0;
    /* A bone is as long as the distance to the child that ends it. */
    arm->upper_length = bone_length(skeleton, arm->lower);
    arm->lower_length = bone_length(skeleton, arm->hand);
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
static void measure_palm(const Skeleton* skeleton, ArmChain* arm,
                         const char* index, const char* pinky,
                         const char* thumb) {
    arm->has_palm = 0;
    arm->palm_axis[0] = arm->palm_axis[1] = arm->palm_axis[2] = 0.0f;

    short index_node = find_node(skeleton, index);
    short pinky_node = find_node(skeleton, pinky);
    short thumb_node = find_node(skeleton, thumb);
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
static void hand_basis(const float along[3], const float palm[3],
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
static void measure_grip_centre(const SkinnedModel* model, const Pose* fist,
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
    hand_basis(arm->hand_axis, arm->palm_axis, u, v, w);
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
static void wrist_from_grip(const ArmChain* arm, const float fingers[3],
                            const float palm[3], const float grip[3],
                            float out[3]) {
    memcpy(out, grip, sizeof(float) * 3);
    if (!arm->has_grip) return;
    float u[3], v[3], w[3];
    hand_basis(fingers, palm, u, v, w);
    for (int c = 0; c < 3; ++c)
        out[c] -= u[c] * arm->grip_centre[0] + v[c] * arm->grip_centre[1] +
                  w[c] * arm->grip_centre[2];
}

/* The bind pose global of one node, walking only its own ancestors. */
static Mat4 bind_global(const Skeleton* skeleton, int node) {
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

/* The two grips of one pose, in camera space, from the spec alone.

   From the spec and not from the live gun transform on purpose: bob, sway and
   recoil all move that matrix, and calibrating against a gun that happens to be
   mid-bob would bake one frame's wobble into the stance. What is wanted is
   where the gun sits at rest at each end of the aim. */
static void pose_targets(const WeaponSpec* spec, float sprint, float reload,
                         float ads, float targets[2][3]) {
    /* The same function the drawn gun rests at, so the stance is calibrated
       against the pose the hands will actually be asked to reach. Recomposing
       the offsets here is what let the arms be solved for a gun that had since
       grown a rotation nobody told them about. */
    ViewmodelPose pose;
    weapon_spec_rest_pose(spec, sprint, reload, ads, &pose);
    for (int a = 0; a < 2; ++a)
        viewmodel_pose_transform_point(&pose,
                                       a ? spec->grip_left : spec->grip_right,
                                       targets[a]);
    /* The shoulders are placed for where the WRIST goes, and for the firing
       hand that is a fist short of the grip it closes on. Solving against the
       grip put the placement a hand's length too far out. */
    float fingers[3], palm[3];
    viewmodel_pose_transform_direction(&pose, ARMS_HAND_DIR[spec->hold][0],
                                       fingers);
    viewmodel_pose_transform_direction(&pose, ARMS_PALM_DIR[spec->hold][0],
                                       palm);
    float grip[3];
    memcpy(grip, targets[0], sizeof grip);
    wrist_from_grip(&g_arms.arms[0], fingers, palm, grip, targets[0]);
}

/* One reload key's support-hand target in camera space.

   `gun` is the transform to read gun-space points through: the resting reload
   pose while placements are being solved, and the LIVE weapon matrix while the
   arms are drawn. A key on the chest is in camera space already and ignores it,
   which is the whole reason a key states its frame - the pouch stays on the
   body while the weapon moves, and every other point on the track does the
   opposite. */
static void key_hand(const ReloadKey* key, const ViewmodelPose* gun,
                     float out[3]) {
    if (key->hand_frame == RELOAD_IN_VIEW) {
        for (int c = 0; c < 3; ++c) out[c] = key->hand[c];
        return;
    }
    viewmodel_pose_transform_point(gun, key->hand, out);
}

/* The same, against the live weapon transform rather than a resting pose. Two
   functions rather than one because the solve works in poses and the draw works
   in matrices, and neither should have to build the other's. */
static void key_hand_live(const ReloadKey* key, const Mat4* gun, float out[3]) {
    if (key->hand_frame == RELOAD_IN_VIEW) {
        for (int c = 0; c < 3; ++c) out[c] = key->hand[c];
        return;
    }
    mat4_transform_point(gun, key->hand, out);
}

static void direction_normalize(float direction[3]) {
    float length = sqrtf(direction[0] * direction[0] +
                         direction[1] * direction[1] +
                         direction[2] * direction[2]);
    if (length < 1e-6f) return;
    for (int c = 0; c < 3; ++c) direction[c] /= length;
}

static void direction_blend(const float from[3], const float to[3], float t,
                            float out[3]) {
    t = clampf(t, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    for (int c = 0; c < 3; ++c) out[c] = lerpf(from[c], to[c], t);
    direction_normalize(out);
}

/* The support wrist changes jobs during a reload. Its target already follows
   the magazine and pouch, but previously its direction remained the handguard
   direction for the entire track. That left the fist pointing along the rifle
   while the arm carried a vertical magazine, then twisted it through the pouch
   and insertion.

   These are directions only: they change the hand bone and nothing upstream of
   the wrist. Position, elbow, shoulder placement, reload timing and the
   magazine itself remain exactly where the existing track puts them. */
static void reload_support_direction(const WeaponSpec* spec,
                                     const ViewmodelBlend* blend,
                                     const Mat4* gun,
                                     const float resting[3], float out[3],
                                     float* wrist_roll) {
    memcpy(out, resting, sizeof(float) * 3);
    *wrist_roll = 0.0f;
    if (blend->reload <= 0.0f) return;

    const ReloadSequence* track = weapon_spec_reload_sequence(spec);
    ReloadSample sample;
    reload_sequence_sample(track, blend->reload_progress, &sample);
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
    direction_normalize(magazine);

    /* At the pouch the fingers follow the forearm down and slightly inward,
       instead of retaining a weapon-relative twist while the hand belongs to
       the body. The tap turns the fist upward under the floorplate. */
    const float pouch[3] = {-0.18f, -0.96f, -0.22f};
    const float tap_local[3] = {0.18f, 0.96f, -0.22f};
    float tap[3];
    mat4_transform_direction(gun, tap_local, tap);
    direction_normalize(tap);

    float wanted[3];
    if (progress < 0.10f) {
        memcpy(wanted, resting, sizeof wanted);
    } else if (progress < 0.24f) {
        direction_blend(resting, magazine,
                        (progress - 0.10f) / 0.14f, wanted);
    } else if (progress < 0.34f) {
        memcpy(wanted, magazine, sizeof wanted);
    } else if (progress < 0.51f) {
        direction_blend(magazine, pouch,
                        (progress - 0.34f) / 0.17f, wanted);
    } else if (progress < 0.58f) {
        memcpy(wanted, pouch, sizeof wanted);
    } else if (progress < 0.64f) {
        direction_blend(pouch, magazine,
                        (progress - 0.58f) / 0.06f, wanted);
    } else if (progress < 0.83f) {
        memcpy(wanted, magazine, sizeof wanted);
    } else if (progress < 0.87f) {
        direction_blend(magazine, tap,
                        (progress - 0.83f) / 0.04f, wanted);
    } else if (progress < 0.95f) {
        memcpy(wanted, tap, sizeof wanted);
    } else {
        direction_blend(tap, resting,
                        (progress - 0.95f) / 0.05f, wanted);
    }

    direction_blend(resting, wanted, blend->reload, out);

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

/* Solved once per weapon. A gun states two grip points and a hold; where the
   shoulders have to be to reach them is this function's problem, which is the
   whole reason a new gun is a data change and not an edit in here. */
static void solve_for_weapon(const WeaponSpec* spec) {
    float targets[2][3];

    pose_targets(spec, 0.0f, 0.0f, 0.0f, targets);
    int hip = arm_placement_solve(&g_arms.rig, targets, spec->view_fov * 0.5f,
                                  &g_arms.hip);
    /* Against the hip field of view, not the aimed one: a sprint is a hip
       state, and the gun is drawn through its wide projection throughout. */
    pose_targets(spec, 1.0f, 0.0f, 0.0f, targets);
    int sprinting = arm_placement_solve(&g_arms.rig, targets,
                                        spec->view_fov * 0.5f, &g_arms.sprint);
    pose_targets(spec, 0.0f, 0.0f, 1.0f, targets);
    int aimed = arm_placement_solve(&g_arms.rig, targets,
                                    spec->view_ads_fov * 0.5f, &g_arms.ads);

    /* And one for every key of the reload track. The firing hand stays on the
       pistol grip throughout - a magazine change is done one-handed on the
       weapon - so only the support target moves between them.

       Solved against the RESTING reload pose rather than the live matrix, for
       the reason pose_targets gives: a stance calibrated mid-bob bakes one
       frame's wobble into itself. */
    const ReloadSequence* track = weapon_spec_reload_sequence(spec);
    ViewmodelPose held;
    weapon_spec_rest_pose(spec, 0.0f, 1.0f, 0.0f, &held);
    g_arms.reload_count = track->count < ARMS_MAX_RELOAD_KEYS
                        ? track->count : ARMS_MAX_RELOAD_KEYS;
    int reload_missed = 0;
    /* Solved in order, each one drawn toward the last. The shoulders are
       INTERPOLATED between these as the hand travels, so what matters is not
       only that every key is comfortable but that the line between them is -
       and two adjacent keys can otherwise land on quite different placements
       that are each fine alone and fold the arm halfway between. */
    const ArmPlacement* previous = NULL;
    for (int i = 0; i < g_arms.reload_count; ++i) {
        float grip[3], fingers[3], palm[3];
        viewmodel_pose_transform_point(&held, spec->grip_right, grip);
        /* The firing hand keeps its hold on the grip throughout, so it is
           walked back the same way here as anywhere else. The support hand's
           keys are already wrist positions. */
        viewmodel_pose_transform_direction(&held, ARMS_HAND_DIR[spec->hold][0],
                                           fingers);
        viewmodel_pose_transform_direction(&held, ARMS_PALM_DIR[spec->hold][0],
                                           palm);
        wrist_from_grip(&g_arms.arms[0], fingers, palm, grip, targets[0]);
        key_hand(&track->keys[i], &held, targets[1]);
        if (arm_placement_solve_near(&g_arms.rig, targets,
                                     spec->view_fov * 0.5f, previous,
                                     &g_arms.reload[i]))
            previous = &g_arms.reload[i];
        else
            ++reload_missed;
    }

    /* No placement reaches this gun's grips at a bend. Rather than draw arms
       clamped straight at whatever the search last tried, fall back to another
       pose's answer if one found it, and to something behind the eye if none
       did - visibly wrong beats silently wrong.

       The hip is tried first as the donor because it is the pose the gun spends
       most of its time in and the one the other two are reached from. */
    struct { int found; ArmPlacement* placement; } solved[3] = {
        {hip, &g_arms.hip},
        {sprinting, &g_arms.sprint},
        {aimed, &g_arms.ads},
    };
    int donor = -1;
    for (int i = 0; i < 3 && donor < 0; ++i)
        if (solved[i].found) donor = i;

    if (donor < 0) {
        ArmPlacement fallback = {0.10f, 45.0f};
        for (int i = 0; i < 3; ++i) *solved[i].placement = fallback;
        printf("First-person arms: no shoulder placement reaches %s's grips; "
               "using a fallback\n", spec->name);
    } else {
        for (int i = 0; i < 3; ++i)
            if (!solved[i].found)
                *solved[i].placement = *solved[donor].placement;
    }

    g_arms.solved_for = spec;
    printf("First-person arms for %s: hip z %+.2f blade %.0f deg, "
           "sprint z %+.2f blade %.0f deg, aimed z %+.2f blade %.0f deg\n",
           spec->name, g_arms.hip.z, g_arms.hip.blade,
           g_arms.sprint.z, g_arms.sprint.blade,
           g_arms.ads.z, g_arms.ads.blade);
    /* A reload key nothing reaches keeps whatever the search last left in it,
       which is a stance rather than a crash - but it is worth saying so, since
       the usual cause is a key moved somewhere an arm of this length cannot
       follow and the symptom is one frozen elbow mid-animation. */
    if (reload_missed)
        printf("First-person arms: %d of %d reload keys are out of reach\n",
               reload_missed, g_arms.reload_count);
    fflush(stdout);
}

static int build(const CharacterLibrary* library, int rig) {
    const SkinnedModel* model = character_library_model(library, rig);
    if (!model) return 0;                /* not loaded yet; try again next frame */

    const Skeleton* skeleton = &model->skeleton;
    /* The sides look inverted and are not: the pole below is a direction in the
       rig's own space, and the facing yaw mirrors x on its way to the screen, so
       the firing arm's elbow breaks toward model -x to come out on the right. */
    if (!resolve_arm(skeleton, &g_arms.arms[0], "upperarm_r", "lowerarm_r",
                     "hand_r", -1.0f) ||
        !resolve_arm(skeleton, &g_arms.arms[1], "upperarm_l", "lowerarm_l",
                     "hand_l", 1.0f)) {
        g_arms.state = ARMS_FAILED;
        return 0;
    }
    measure_palm(skeleton, &g_arms.arms[0], "index_01_r", "pinky_01_r",
                 "thumb_01_r");
    measure_palm(skeleton, &g_arms.arms[1], "index_01_l", "pinky_01_l",
                 "thumb_01_l");

    /* Everything from the clavicles out. Marking the upper arms instead would
       cut at the shoulder seam and leave a ring of clavicle vertices behind on
       the body's side of the cut; marking the clavicles takes the deltoid with
       the arm, which is where an arm actually starts. */
    PoseMask mask, other;
    if (!skinned_model_mask_subtree(model, "clavicle_r", &mask)) {
        g_arms.state = ARMS_FAILED;
        return 0;
    }
    if (skinned_model_mask_subtree(model, "clavicle_l", &other))
        for (int i = 0; i < SKIN_MAX_NODES; ++i)
            if (other.node[i]) mask.node[i] = 1;

    if (!skinned_instance_init(&g_arms.instance, model) ||
        !skinned_subset_init(&g_arms.subset, model, &mask)) {
        skinned_instance_free(&g_arms.instance);
        skinned_subset_free(&g_arms.subset);
        g_arms.state = ARMS_FAILED;
        return 0;
    }

    g_arms.grip_clip = -1;
    for (int c = 0; c < model->clip_count; ++c) {
        if (strcmp(model->clips[c].name, ARMS_GRIP_CLIP) != 0) continue;
        g_arms.grip_clip = c;
        break;
    }

    Mat4 right = bind_global(skeleton, g_arms.arms[0].upper);
    Mat4 left = bind_global(skeleton, g_arms.arms[1].upper);
    for (int c = 0; c < 3; ++c)
        g_arms.rig.shoulder_mid[c] = (right.m[12 + c] + left.m[12 + c]) * 0.5f;

    /* Scaled by reach and not by height: what has to be right here is how far
       the hands are from the shoulders, because that is what the solve is
       against. The rig's own height is the third-person body's business. */
    float reach = g_arms.arms[0].upper_length + g_arms.arms[0].lower_length;
    g_arms.rig.scale = reach > 1e-4f ? ARMS_REACH / reach : 1.0f;

    /* The shoulders as the grip clip leaves them, not as the bind pose has
       them. The clip turns the clavicles, so a placement calibrated against the
       bind pose would be calibrated against shoulders that are not there - and
       the solve reads the posed ones. */
    {
        Pose pose;
        if (g_arms.grip_clip >= 0)
            skinned_model_sample(model, g_arms.grip_clip, 0.0f, &pose);
        else
            skinned_model_bind_pose(model, &pose);
        const char* bone[2] = {"upperarm_r", "upperarm_l"};
        for (int a = 0; a < 2; ++a) {
            Mat4 m;
            if (!skinned_model_bone_matrix(model, &pose, bone[a], &m)) continue;
            for (int c = 0; c < 3; ++c)
                g_arms.rig.shoulder_offset[a][c] = m.m[12 + c] -
                                               g_arms.rig.shoulder_mid[c];
        }

        /* And the fist itself, off the same pose - it is the pose the hands
           are drawn in, so it is the one whose grip has to be measured. */
        const char* hand_bone[2] = {"hand_r", "hand_l"};
        for (int a = 0; a < 2; ++a)
            measure_grip_centre(model, &pose, &g_arms.instance, &g_arms.arms[a],
                                hand_bone[a]);
    }

    g_arms.model = model;
    g_arms.solved_for = NULL;
    g_arms.state = ARMS_READY;

    int triangles = 0;
    for (int m = 0; m < SKIN_MAX_MESHES; ++m)
        triangles += g_arms.subset.index_count[m] / 3;
    printf("First-person arms: %d triangles cut from the body rig\n", triangles);
    /* Worth saying, because the symptom is subtle: the arms still track the gun
       and only the wrists go back to rolling wherever the elbow leaves them. */
    if (!g_arms.arms[0].has_palm || !g_arms.arms[1].has_palm)
        printf("First-person arms: no finger bones to measure a palm from; "
               "the wrists keep a free roll\n");
    if (g_arms.arms[0].has_grip)
        printf("First-person arms: the fist grips %.3fm ahead of the wrist "
               "and %.3fm toward the palm\n",
               g_arms.arms[0].grip_centre[0] * g_arms.rig.scale,
               g_arms.arms[0].grip_centre[1] * g_arms.rig.scale);
    else
        printf("First-person arms: the fist's grip could not be measured; "
               "the wrist lands on the grip and the hand floats off it\n");
    fflush(stdout);
    return 1;
}

/* Where the shoulder line sits this frame, and how far the stance is bladed.
   Both crossfaded from the three solved poses on the weapon's own carries. */
static void shoulder_anchor(const WeaponSpec* spec, const ViewmodelBlend* blend,
                            float out[3], float* blade) {
    /* Sprint first, then the reload over the top of it, then the aim over both
       - the same nesting weapon_spec_rest_pose blends the POSE with, so the
       shoulders travel with the gun instead of taking a different route to the
       same place. */
    ArmPlacement carry;
    carry.z = lerpf(g_arms.hip.z, g_arms.sprint.z, blend->sprint);
    carry.blade = lerpf(g_arms.hip.blade, g_arms.sprint.blade, blend->sprint);

    /* Where on the reload track the shoulders are. The keys are sampled the
       same way the hand's own target is, so the placement and the point it has
       to reach are never a key out of step - which would put the shoulders
       where the previous waypoint wanted them and the hand where the next one
       does, and fold the arm between the two. */
    if (blend->reload > 0.0f && g_arms.reload_count > 0) {
        const ReloadSequence* track = weapon_spec_reload_sequence(spec);
        ReloadSample sample;
        reload_sequence_sample(track, blend->reload_progress, &sample);
        int from = (int)(sample.from - track->keys);
        int to = (int)(sample.to - track->keys);
        if (from >= g_arms.reload_count) from = g_arms.reload_count - 1;
        if (to >= g_arms.reload_count) to = g_arms.reload_count - 1;
        ArmPlacement reloading;
        reloading.z = lerpf(g_arms.reload[from].z, g_arms.reload[to].z, sample.t);
        reloading.blade = lerpf(g_arms.reload[from].blade,
                                g_arms.reload[to].blade, sample.t);
        carry.z = lerpf(carry.z, reloading.z, blend->reload);
        carry.blade = lerpf(carry.blade, reloading.blade, blend->reload);
    }

    carry.z = lerpf(carry.z, g_arms.ads.z, blend->ads);
    carry.blade = lerpf(carry.blade, g_arms.ads.blade, blend->ads);
    arm_placement_anchor(carry, out);
    *blade = carry.blade;
}

/* Points one bone along `target`, given in the model's space, by rotating its
   rest axis onto it inside its parent's frame. Lifted from the ledge hang's
   apply pass, including the reason the parent's basis is normalized first: the
   caller scales the rig, and a scaled basis would tilt the direction rather
   than only rotating it. */
static void point_bone(const Skeleton* skeleton, const Mat4 global[],
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
static void reach(const float shoulder[3], const float target[3],
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

   This is the roll that point_bone leaves free. It is measured after the
   pointing rather than authored alongside it, because what it has to cancel is
   not a constant: it is whatever roll the shoulder and elbow arrived at, and
   that changes with every target the arm reaches for.

   Both directions are flattened across the fingers first. A turn about an axis
   cannot change what anything does along it, so the component along the fingers
   is not this rotation's to fix and would only bias the angle if it were left
   in. */
static float palm_roll(const Skeleton* skeleton, const Mat4 global[], int node,
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

/* One pass down the hierarchy, parents before children, because pointing a bone
   needs to know where its parent ended up: a direction is in the model's space
   and what a bone stores is relative to its parent. Each arm is solved when its
   upper bone comes round, since the forearm's answer depends on where the elbow
   landed. */
static void solve_arms(const SkinnedModel* model, const float targets[2][3],
                       Pose* pose) {
    const Skeleton* skeleton = &model->skeleton;
    Mat4 global[SKIN_MAX_NODES];
    float upper_dir[2][3], lower_dir[2][3];

    for (int i = 0; i < skeleton->node_count; ++i) {
        int node = skeleton->order[i];

        for (int a = 0; a < 2; ++a) {
            const ArmChain* arm = &g_arms.arms[a];
            if (node == arm->upper) {
                int parent = skeleton->parent[node];
                float shoulder[3] = {0, 0, 0};
                if (parent >= 0) {
                    float offset[3];
                    mat4_transform_direction(&global[parent],
                                             pose->local[node].translation,
                                             offset);
                    for (int c = 0; c < 3; ++c)
                        shoulder[c] = global[parent].m[12 + c] + offset[c];
                }
                reach(shoulder, targets[a], arm->upper_length,
                      arm->lower_length, arm->side, upper_dir[a], lower_dir[a]);
                point_bone(skeleton, global, node, arm->upper_axis,
                           upper_dir[a], pose);
            } else if (node == arm->lower) {
                point_bone(skeleton, global, node, arm->lower_axis,
                           lower_dir[a], pose);
            } else if (node == arm->hand) {
                /* Pointing the forearm leaves its roll free, so the palm would
                   otherwise face wherever the minimal rotation happened to put
                   it. Aiming the hand off the gun's frame is what settles that,
                   and it is why the wrist tracks the weapon through recoil and
                   the swing up to aim rather than only the arm doing it. */
                point_bone(skeleton, global, node, arm->hand_axis,
                           arm->hand_dir, pose);
                /* The measured roll puts the palm where the gun wants it; the
                   authored one is the reload's own twist on top of that. One
                   turn, not two, so the hand is never rolled and re-rolled. */
                float roll = arm->hand_roll +
                             palm_roll(skeleton, global, node, arm, pose);
                if (fabsf(roll) > 0.001f) {
                    int parent = skeleton->parent[node];
                    float local_axis[3] = {arm->hand_dir[0], arm->hand_dir[1],
                                           arm->hand_dir[2]};
                    if (parent >= 0) {
                        const Mat4* p = &global[parent];
                        for (int c = 0; c < 3; ++c) {
                            float scale = sqrtf(p->m[c*4+0]*p->m[c*4+0] +
                                                p->m[c*4+1]*p->m[c*4+1] +
                                                p->m[c*4+2]*p->m[c*4+2]);
                            if (scale < 1e-8f) scale = 1.0f;
                            local_axis[c] =
                                (p->m[c*4+0]*arm->hand_dir[0] +
                                 p->m[c*4+1]*arm->hand_dir[1] +
                                 p->m[c*4+2]*arm->hand_dir[2]) / scale;
                        }
                    }
                    float turn[4], pointed[4];
                    memcpy(pointed, pose->local[node].rotation,
                           sizeof pointed);
                    quat_from_axis_angle(local_axis, roll * MATH_DEG_TO_RAD,
                                         turn);
                    quat_multiply(turn, pointed,
                                  pose->local[node].rotation);
                }
            }
        }

        const Transform* local = &pose->local[node];
        Mat4 matrix = mat4_from_trs(local->translation, local->rotation,
                                    local->scale);
        int parent = skeleton->parent[node];
        global[node] = parent < 0 ? matrix : mat4_multiply(global[parent],
                                                           matrix);
    }
}

/* Where the support hand is this frame, in camera space.

   Public because the magazine needs the same answer: a magazine being carried
   up from the chest is drawn AT the hand, and computing that a second time in
   the renderer is how the two would come to disagree by a frame. */
void viewmodel_arms_support_target(const WeaponSpec* spec,
                                   const ViewmodelBlend* blend,
                                   const Mat4* gun, float out[3]) {
    /* Off the gun's own grip when no reload is running - which is the common
       case and wants no track lookup at all. */
    mat4_transform_point(gun, spec->grip_left, out);
    if (blend->reload <= 0.0f) return;

    const ReloadSequence* track = weapon_spec_reload_sequence(spec);
    ReloadSample sample;
    reload_sequence_sample(track, blend->reload_progress, &sample);

    /* Both keys resolved into camera space BEFORE they are blended, because
       they may not be in the same space: a key on the magazine is on the gun
       and a key at the pouch is on the body, and interpolating the raw numbers
       would slide the hand between two different frames of reference. Resolved
       first, the crossing from one to the other is just a line through the
       world, which is what a hand leaving a weapon actually travels. */
    float from[3], to[3];
    key_hand_live(sample.from, gun, from);
    key_hand_live(sample.to, gun, to);

    /* And blended against the handguard by how much the reload owns the hands,
       so aiming out of a reload brings the hand home rather than stranding it
       in mid-air. */
    for (int c = 0; c < 3; ++c)
        out[c] = lerpf(out[c], lerpf(from[c], to[c], sample.t), blend->reload);
}

void viewmodel_arms_draw(const CharacterLibrary* library, int rig,
                         const WeaponSpec* spec,
                         const float gun_matrix[16],
                         const ViewmodelBlend* blend) {
    if (g_arms.state == ARMS_FAILED) return;
    if (g_arms.state != ARMS_READY && !build(library, rig)) return;
    /* The library could in principle hand back a different rig than the one the
       cut was built against, and a subset indexed into the wrong mesh is a read
       past the end of it. Cheaper to check than to reason about. */
    if (character_library_model(library, rig) != g_arms.model) return;
    if (!spec) return;
    /* A weapon swap is the only thing that re-solves. */
    if (spec != g_arms.solved_for) solve_for_weapon(spec);

    const SkinnedModel* model = g_arms.model;

    /* The grips, off the live gun transform rather than recomposed here. */
    Mat4 gun;
    memcpy(gun.m, gun_matrix, sizeof gun.m);
    float anchor[3], blade;
    shoulder_anchor(spec, blend, anchor, &blade);

    /* The grips come off the live gun transform rather than the spec, so the
       hands ride the bob, the sway and the recoil the gun actually has. The
       SHOULDERS were solved from the spec's resting pose; the hands track the
       gun from there.

       The firing hand is always the pistol grip - it never leaves the weapon,
       reload or not. The support hand is wherever the reload track has put it,
       which for most of the time is the handguard and for the length of a
       magazine change is the magazine, the air, and the player's own chest. */
    /* The fingers point where the gun does. A direction and not a point, so the
       gun's rotation is all that is read - the placement between camera space
       and the rig is a uniform scale and a translation, neither of which turns
       a direction, so this needs no conversion of its own.

       Aimed BEFORE the targets are worked out, because where the wrist has to
       go depends on which way the hand is pointing: the fist is walked back
       down its own fingers, and that is a direction this loop has to have
       settled first. */
    float fingers[2][3], palm[2][3];
    for (int a = 0; a < 2; ++a) {
        g_arms.arms[a].hand_roll = 0.0f;
        mat4_transform_direction(&gun, ARMS_HAND_DIR[spec->hold][a], fingers[a]);
        mat4_transform_direction(&gun, ARMS_PALM_DIR[spec->hold][a], palm[a]);

        /* The support hand is the one that lets go. Through a reload its
           direction comes off the track instead of off the gun, along with the
           twist the track authors for it. */
        if (a == 1) {
            float turned[3];
            reload_support_direction(spec, blend, &gun, fingers[a], turned,
                                     &g_arms.arms[a].hand_roll);
            memcpy(fingers[a], turned, sizeof fingers[a]);
        }

        /* Into the rig's own frame, since the facing and the blade turn it. A
           direction takes the rotation and nothing else. */
        arm_placement_rig_from_camera(fingers[a], blade,
                                      g_arms.arms[a].hand_dir);

        /* And the same for the palm, which is what stops the wrist rolling to
           whatever the elbow left it at. A zero entry means this hand's roll is
           left free, and the target stays zero for palm_roll to read. */
        const float* want = ARMS_PALM_DIR[spec->hold][a];
        if (want[0] != 0.0f || want[1] != 0.0f || want[2] != 0.0f)
            arm_placement_rig_from_camera(palm[a], blade,
                                          g_arms.arms[a].palm_dir);
        else
            memset(g_arms.arms[a].palm_dir, 0,
                   sizeof g_arms.arms[a].palm_dir);
    }

    /* Where each hand HOLDS, and then where its wrist has to be for it to.

       The grip used to be handed straight to the solver, which put the wrist on
       it and left the fist closed on the air in front. There used to be three
       centimetres of upward bias on the firing grip here to make that look less
       wrong - the fist otherwise hung off the grip's lower edge - and it is
       gone, because it was treating the symptom of this. */
    float targets[2][3], camera_point[3], wrist[3];
    mat4_transform_point(&gun, spec->grip_right, camera_point);
    wrist_from_grip(&g_arms.arms[0], fingers[0], palm[0], camera_point, wrist);
    arm_placement_to_model_space(&g_arms.rig, wrist, anchor, blade, targets[0]);

    /* The support hand's own point is already a wrist - see wrist_from_grip. */
    viewmodel_arms_support_target(spec, blend, &gun, camera_point);
    arm_placement_to_model_space(&g_arms.rig, camera_point, anchor, blade,
                                 targets[1]);

    /* The authored grip underneath, the solved arms on top. What the clip is
       here for is the twenty-two bones of each hand: the arms are overwritten
       one bone later, and everything above the clavicles is not drawn. */
    Pose pose;
    if (g_arms.grip_clip >= 0)
        skinned_model_sample(model, g_arms.grip_clip, 0.0f, &pose);
    else
        skinned_model_bind_pose(model, &pose);
    solve_arms(model, targets, &pose);
    skinned_model_skin(model, &pose, &g_arms.instance);

    glPushMatrix();
    /* The anchor, then the rig's scale, then back off the point the anchor was
       measured from - the inverse of arm_placement_to_model_space, in the order GL composes. */
    glTranslatef(anchor[0], anchor[1], anchor[2]);
    /* The blade, then the facing - and in that order, because GL applies the
       last transform named to the vertex first. Negative on the blade, because
       GL's y-rotation is the transpose of the one the rig conversion undoes. */
    glRotatef(-blade, 0, 1, 0);
    glRotatef(ARMS_FACING_DEGREES, 0, 1, 0);
    glScalef(g_arms.rig.scale, g_arms.rig.scale, g_arms.rig.scale);
    glTranslatef(-g_arms.rig.shoulder_mid[0], -g_arms.rig.shoulder_mid[1],
                 -g_arms.rig.shoulder_mid[2]);
    skinned_model_draw_subset(model, &g_arms.instance, &g_arms.subset);
    glPopMatrix();
}

void viewmodel_arms_shutdown(void) {
    skinned_instance_free(&g_arms.instance);
    skinned_subset_free(&g_arms.subset);
    memset(&g_arms, 0, sizeof g_arms);
}
