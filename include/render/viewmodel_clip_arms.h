#ifndef RENDER_VIEWMODEL_CLIP_ARMS_H
#define RENDER_VIEWMODEL_CLIP_ARMS_H

#include "gameplay/weapon_spec.h"
#include "render/skinned_model.h"
#include "utils/mat4.h"

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

/* The clip index for a name on this rig, or -1. A thin pass-through, here so
   callers of this module do not each reach into the skinned model to ask. */
int viewmodel_clip_arms_find(const SkinnedModel* model, const char* name);

#endif
