#ifndef RENDER_ARM_GEOMETRY_H
#define RENDER_ARM_GEOMETRY_H

#include "gameplay/weapon_spec.h"
#include "gameplay/action_track.h"
#include "gameplay/viewmodel_pose.h"
#include "render/skinned_model.h"
#include "render/viewmodel_arms.h"
#include "utils/mat4.h"

/* The arm arithmetic that holds no state.

   viewmodel_arms.c is one long-lived struct - a rig, its two arm chains, and
   every placement solved against them - and everything in that file either
   fills it or reads it. What is HERE is the part that does neither: measuring
   a bone off a skeleton, turning a grip into a wrist, pointing a bone at a
   point, deciding where a hand goes during a reload. All of it takes what it
   needs and answers; none of it can be affected by what was drawn last frame.

   Split out for the reason arm_placement.c already was: code with no state and
   no GL is code a headless probe can link and check one call at a time, and
   code that can be read without holding a thousand lines of context. What is
   left behind in viewmodel_arms.c is the stateful core, which is one thing and
   stays one thing - splitting THAT would mean publishing a forty-field mutable
   global across two files, which is worse than the line count it would fix. */

/* Which way the elbows break: down and out, the way a shouldered rifle holds
   them. Mirrored on x for the left arm. Here rather than beside the solver
   because arm_reach is what applies it, and a pole read from one file and
   applied in another is a pole that can be changed in only one of them. */
static const float ARMS_ELBOW_POLE[3] = {0.55f, -0.78f, 0.30f};

/* As many keys as the reload track is allowed. Raising it costs two floats a
   key and one more scan at load. */
#define ARMS_MAX_RELOAD_KEYS 16

/* One arm, as the solver needs it: three bones, their rest axes, where the
   fingers should point, and how long the segments are. */
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
       Zero for a hand whose roll is left free - see weapon_grip_palm_dir. */
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

/* --- measuring a rig ---------------------------------------------------- */

/* A node by name, or -1. Names come from the file, so this is how anything
   finds a bone. */
short arm_find_node(const Skeleton* skeleton, const char* name);

/* The direction a bone runs in its parent's frame, at rest. */
int arm_rest_axis(const Skeleton* skeleton, int node, float out[3]);

/* How long a bone is, in the rig's own units. */
float arm_bone_length(const Skeleton* skeleton, int node);

/* Fills `arm` from three bone names, or answers 0 if the rig has not got
   them. */
int arm_resolve(const Skeleton* skeleton, ArmChain* arm, const char* upper, const char* lower, const char* hand, float side);

/* Which way the palm faces out of this hand, measured off the rig rather than
   chosen: it is a fact about the skeleton. */
void arm_measure_palm(const Skeleton* skeleton, ArmChain* arm, const char* index, const char* pinky, const char* thumb);

/* An orthonormal basis for a hand from where it points and where its palm
   faces. */
void arm_hand_basis(const float along[3], const float palm[3], float u[3], float v[3], float w[3]);

/* Where the closed fist's grip actually sits relative to the wrist, measured
   off the rig in its own units. */
void arm_measure_grip_centre(const SkinnedModel* model, const Pose* fist, SkinnedInstance* skinned, ArmChain* arm, const char* hand);

/* The wrist that puts this arm's GRIP at a point. A hand does not hold a gun
   at its wrist, and the walk back down the fingers is the difference. */
void arm_wrist_from_grip(const ArmChain* arm, const float fingers[3], const float palm[3], const float grip[3], float out[3]);

/* A node's global transform in the bind pose. */
Mat4 arm_bind_global(const Skeleton* skeleton, int node);

/* --- pointing and reaching ---------------------------------------------- */

void arm_direction_normalize(float direction[3]);
void arm_direction_blend(const float from[3], const float to[3], float t, float out[3]);

/* Turns a bone to run along `direction`, in the rig's space. */
void arm_point_bone(const Skeleton* skeleton, const Mat4 global[], int node, const float axis[3], const float target[3], Pose* pose);

/* Two bones from a shoulder to a target: the elbow angle that reaches it, and
   the pole that decides which way the elbow breaks. */
void arm_reach(const float shoulder[3], const float target[3], float upper_length, float lower_length, float side, float upper_dir[3], float lower_dir[3]);

/* How far the palm has to roll about the forearm to face where it should. */
float arm_palm_roll(const Skeleton* skeleton, const Mat4 global[], int node, const ArmChain* arm, const Pose* pose);

/* --- where a hand is told to go ----------------------------------------- */

/* A reload key's hand point, off the gun's resting pose and off its live
   transform respectively. */
void arm_key_hand(const ActionKey* key, const ViewmodelPose* gun, float out[3]);
void arm_key_hand_live(const ActionKey* key, const Mat4* gun, float out[3]);

/* Which way the support hand's fingers point through a magazine change. The
   hand leaves the weapon and comes back, and no single direction follows it. */
void arm_reload_support_direction(const WeaponSpec* spec, const ViewmodelBlend* blend, const Mat4* gun, const float resting[3], float out[3], float* wrist_roll);

#endif
