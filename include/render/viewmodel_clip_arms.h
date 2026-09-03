#ifndef RENDER_VIEWMODEL_CLIP_ARMS_H
#define RENDER_VIEWMODEL_CLIP_ARMS_H

#include "gameplay/weapon_spec.h"
#include "render/skinned_model.h"
#include "utils/mat4.h"
#include "gameplay/viewmodel_pose.h"

/* First-person arms taken straight from an animation clip, for a weapon whose
   motion is authored rather than solved.

   A sibling of viewmodel_arms.c and deliberately not an addition to it. That
   file answers the same question the other way round, and answering it both
   ways in one place would be a branch through 1200 lines rather than two
   modules with one interface: for a gun the weapon is placed where the spec
   says and the arms are solved to reach its grips, because a gun's pose is a
   number somebody tuned; for a sword the arms are animated and the weapon goes
   wherever the hand went, because a swing is a clip somebody drew and no solve
   is going to reproduce it.

   Both end at a ViewmodelPose, which is the only thing the rest of the
   renderer ever sees.

   Camera space out: +x right, +y up, -z forward, metres. */

/* Samples `clip` at `time` and hands back the arm pose and where the firing
   hand ended up, in camera space.

   Three corrections stand between the rig and the camera, and every one of
   them is silent when it is missing rather than wrong:

     * the facing. The rig faces +Z and the camera looks down -Z, so the half
       turn ARMS_FACING_DEGREES names is what stops the arms being drawn
       behind the eye, where they render perfectly happily and are never seen.

     * the scale. The rig is CHARACTER_RIG_HEIGHT tall - 2.53m, a giant, sized
       for a third-person body - and a giant's arm at arm's length from the
       camera is a wall of forearm. Scaled by reach and not by height for the
       same reason viewmodel_arms.c is: what has to be right is how far the
       hands are from the shoulders.

     * the offset. These clips were staged for a camera behind the body, so
       the same motion needs bringing toward the eye. That is per weapon and
       lives on the spec as view_clip_offset.

   Returns 0 when the rig has no such clip, or no arms to find, in which case
   nothing is written. */
int viewmodel_clip_arms_pose(const SkinnedModel* model, int clip, float time,
                             const MeleeSpec* melee, Pose* out, Mat4* hand_out);

/* The same sample, handed back as the pose type the rest of the viewmodel
   speaks. The hand's rotation is decomposed into the yaw-pitch-roll the
   renderer applies in that order, so a weapon drawn through the resulting
   ViewmodelPose lands exactly where hand_out said it would - which is what
   lets the tracer origin, the arms and the mesh all read one transform.

   Returns 0 for the same reasons the call above does. */
int viewmodel_clip_arms_base(const SkinnedModel* model, int clip, float time,
                             const MeleeSpec* melee, Pose* arms_out,
                             ViewmodelPose* out);

/* Where the shoulder LINE sits in rig space, and how many metres a rig unit
   is worth.

   One function, because these two numbers place the arms and place the weapon
   and the two have to be the same placement or the fist closes on air. The
   shoulder line is the MIDPOINT of the two shoulders and not the firing one:
   the arms are drawn as a pair about their own centre, and measuring the
   weapon from one shoulder instead put the sword 21cm to the side of the hand
   that was supposedly gripping it - a mismatch invisible to any check that
   compared the weapon against itself.

   Returns 0 if the rig has no arms to measure. */
int viewmodel_clip_arms_calibrate(const SkinnedModel* model, float* scale);

/* Where the shoulder line has got to in a given pose - the midpoint of the two
   shoulders, in rig space.

   Per pose and not from the bind, because these clips were authored for a
   whole body: the spine bends into a cut and carries the shoulders a long way
   with it. Anchored at the bind pose that travel arrives in camera space as
   the whole viewmodel lurching a metre downward through every swing, which is
   the body moving, not the arms. Anchored here the shoulders stay put and the
   arms swing about them, which is what a first-person camera riding on those
   shoulders would actually see.

   Both the arms and the weapon in them subtract this same point, from the same
   pose, which is what keeps the hilt in the fist frame by frame. */
int viewmodel_clip_arms_anchor(const SkinnedModel* model, const Pose* pose,
                               float shoulder_mid[3]);

/* The clip index for a name on this rig, or -1. A thin pass-through, here so
   callers of this module do not each reach into the skinned model to ask. */
int viewmodel_clip_arms_find(const SkinnedModel* model, const char* name);

#endif
