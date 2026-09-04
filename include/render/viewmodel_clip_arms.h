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

     * the hold. How a fist meets this weapon - which way its fingers run and
       where on the mesh they close - is the weapon's own fact and comes from
       render/weapon_grip.h, which is why this takes a spec rather than only the
       melee half of one. It used to be a quarter turn stated in this file and
       twice more elsewhere, which is a rifle's answer spent on a sword.

     * the anchor. The pose arrives about the rig's root, which is on the
       floor between its feet. It is carried so that the rig's own EYE lands on
       the camera, because in first person the camera IS the eye - see
       viewmodel_clip_arms_eye. Any per-weapon nudge on top of that is the
       spec's view_clip_offset, and it is a nudge: the anatomy is the anchor's
       job, not the offset's.

   Returns 0 when the rig has no such clip, or no arms to find, in which case
   nothing is written. */
int viewmodel_clip_arms_pose(const SkinnedModel* model, int clip, float time,
                             const WeaponSpec* spec, Pose* out, Mat4* hand_out);

/* The same sample, handed back as the pose type the rest of the viewmodel
   speaks. The hand's rotation is decomposed into the yaw-pitch-roll the
   renderer applies in that order, so a weapon drawn through the resulting
   ViewmodelPose lands exactly where hand_out said it would - which is what
   lets the tracer origin, the arms and the mesh all read one transform.

   Returns 0 for the same reasons the call above does. */
int viewmodel_clip_arms_base(const SkinnedModel* model, int clip, float time,
                             const WeaponSpec* spec, Pose* arms_out,
                             ViewmodelPose* out);

/* How many metres a rig unit is worth, measured across the arm.

   Scaled by reach and not by height, for the reason viewmodel_arms.c is: what
   has to be right is how far the hands are from the shoulders.

   Returns 0 if the rig has no arms to measure. */
int viewmodel_clip_arms_calibrate(const SkinnedModel* model, float* scale);

/* Where the EYE has got to in a given pose, in rig space - the head bone, plus
   the short step out of the skull to the eye that actually sees.

   This is the point the whole pose is carried onto the camera, because in
   first person the camera is the eye and nothing else is. Anchoring anywhere
   else has to make up the difference with a stated offset, and the difference
   is not a constant: it is whatever the clip's neck and spine are doing that
   frame.

   Two anchors were tried before this one and both are worth naming, because
   the failure each produced looks like something other than an anchor.

     * the bind pose's shoulders. The spine bends into a cut and carries the
       shoulders a long way with it, and against a fixed point that travel
       arrives as the whole viewmodel lurching a metre down the screen through
       every swing - the body moving, read as the arms moving.

     * the POSE's shoulders, with the eye stated as a fixed height above them.
       That fixes the lurch and leaves the horizontal error, which is the one
       that hurts: the shoulder line was pinned directly under the eye when a
       real one sits a hand's breadth behind it, and the per-weapon offset that
       was then tuned to bring the hands into frame pushed the shoulder line
       40cm in FRONT of the camera. The upper arms ended up ahead of the eye and
       the forearms across the near plane, which on screen is not a misplaced
       arm - it is a wall of forearm filling the frame, with no shape left in it
       to say what it is.

   Per pose, through the head bone's own rotation, so a clip that ducks or
   turns the head carries the eye with it - which is exactly the relative
   motion a real first-person view has, the arms rising in frame as the head
   drops rather than the head dropping and taking the arms with it.

   Both the arms and the weapon in them subtract this same point, from the same
   pose, which is what keeps the hilt in the fist frame by frame. */
int viewmodel_clip_arms_eye(const SkinnedModel* model, const Pose* pose,
                            float eye[3]);

/* The step from the head bone's own origin out to that eye, in CAMERA axes and
   in metres - the same short walk viewmodel_clip_arms_eye takes, handed out on
   its own.

   Its one caller negates it to get a neck pivot, because a head bone's origin
   is where the skull actually turns and the eye is a hand's breadth up and
   forward of it. Measured off the rig rather than stated, for the reason that
   function is anchored where it is: the two constants in this codebase that
   stood in for anatomy were each wrong by tens of centimetres, and this rig
   has always known the answer.

   Off the bind pose and the rig's own scale, so it is a property of the body
   and not of the frame - a pivot that moved with the clip would be a skull
   that came off its neck.

   Returns 0 if the rig has no head to measure, or no arms to scale by. */
int viewmodel_clip_arms_eye_step(const SkinnedModel* model, float step[3]);

/* The clip index for a name on this rig, or -1. A thin pass-through, here so
   callers of this module do not each reach into the skinned model to ask. */
int viewmodel_clip_arms_find(const SkinnedModel* model, const char* name);

#endif
