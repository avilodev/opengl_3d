#ifndef RENDER_VIEWMODEL_NECK_H
#define RENDER_VIEWMODEL_NECK_H

#include "gameplay/weapon_spec.h"

/* Where the body has got to, relative to an eye that moved without it.

   Everything the player holds is drawn in camera space, which is to say welded
   to the eye: pitch the view and the world turns underneath a gun that has not
   moved a pixel. A real eye is mounted on a head, the head pivots on a neck,
   and the arms hang off a torso that stayed where it was - so looking up takes
   the gun out of frame and looking down brings the hands into it. That relative
   motion is most of what makes a first-person view read as a body rather than
   as a camera with a gun taped to it.

   This is that motion, as one rotation. It is a CARRY in the sense
   viewmodel_pose.h draws the line: cosmetic, applied to the drawn viewmodel and
   to anything that sits on it, and never to where a round goes. The crosshair
   is safe by construction - hud.c draws it off weapon_aim_angles, which is the
   aim and not the drawn carry - and the round leaves the eye along those same
   angles whatever this does to the mesh.

   ONE module rather than an angle each call site composes, because there are
   three consumers and they have to agree: the GL draw, the stencil window cut
   for a magnified optic, and the tracer's origin. VIEWMODEL_LEAN_ROLL is the
   warning. It is the same shape of rotation with the same three consumers, and
   it was applied by hand in all three - three copies of one arithmetic, each
   free to drift, and a drift between them is a scope window sliding off the
   glass it is a window into. The lean folds in here, so the copies become one
   and the neck does not become three more.

   No GL in this file on purpose. The tracer's origin is computed in
   src/gameplay/weapon.c, which the melee probe links without a renderer; a
   glPushMatrix in here would be an undefined symbol in a headless build. So the
   module hands out a MATRIX and the one call site that draws multiplies it on,
   which also settles the agreement question for free: the matrix GL is given
   and the matrix a point is transformed by are the same sixteen floats. */

/* The neck for one frame: three angles and the point they turn about. Built
   once per frame and passed to whichever of the three consumers wants it,
   rather than each of them rebuilding it from the player. */
typedef struct {
    /* Degrees about camera x, positive nose-up. The NEGATIVE of what the head
       did, because the head is what moved: relative to a camera that pitched
       up, the body pitched down. */
    float pitch;
    /* Degrees about camera y, positive nose-left. The NEGATIVE of the head's
       turn off its own chest, for the reason `pitch` is negative: the torso is
       what stayed behind.

       Unlike the pitch this is NOT a function of the view angle, and cannot be.
       Yaw wraps - spin a full turn and you are back where you started with the
       weapon in the same place - so there is no absolute camera yaw to feed a
       curve. What there is instead is the OFFSET between where the eye points
       and where the chest faces, which exists only while the body is catching
       up and which the third-person rig already measures every frame. See
       viewmodel_neck_set_look_yaw. */
    float yaw;
    /* Degrees about camera z - the lean's cant, which used to be a bare
       glRotatef at each of the three call sites. */
    float roll;
    /* Where the skull turns on the neck, in camera space. Behind and below the
       eye, which is why it is not simply the origin. */
    float pivot[3];
} ViewmodelNeck;

/* How far the head itself has turned for a given view pitch, in degrees, before
   any per-weapon follow or aim fade. Public because it is the one part of this
   worth checking on its own: it has to saturate, and a saturation is only
   visible over a range of inputs.

   A saturation and not a clamp - see viewmodel_neck.c - so near zero it is the
   identity and at the pitch limits it approaches the neck's own range and
   stops. */
float viewmodel_neck_head_pitch(float view_pitch_degrees);

/* The saturation applied to a head that has turned off its chest, in degrees.
   The same shape as the pitch's and for the same reason - a clamp would stop the
   viewmodel dead in the middle of a turn - against its own, much shorter limit.
   See VIEWMODEL_NECK_YAW_LIMIT for why it is shorter. */
float viewmodel_neck_head_yaw(float look_yaw_degrees);

/* The frame's head-off-chest yaw, in degrees, positive to the left.

   Module state, and for a DIFFERENT reason than the pivot's: the pivot is a
   property of the rig, measured once; this is a property of the frame, set every
   one. What they share is why neither is a parameter. Three consumers have to
   agree, and only one module can answer this - the renderer, which owns the
   body's animation. src/gameplay/weapon.c is one of the three and links headless
   in tools/melee_probe.c, so it can reach no CharacterAnimation to ask; handing
   it a number it would have to be given anyway is how the pivot already works.

   Set from CharacterAnimation.look_yaw, which is the same value the third-person
   rig distributes over spine_03, neck_01 and Head. READING it, rather than
   growing a second lag beside it, is what stops the two views disagreeing about
   where the head is - the same argument as render/weapon_grip.h.

   One frame stale in weapon.c: renderer3d_draw runs after weapon_update, so a
   tracer's origin uses the previous frame's turn. Worth a sentence rather than a
   fix. It is a fraction of a degree of neck at any turn rate a mouse produces,
   on the origin of a STREAK, and never on where the round goes.

   Zero until the renderer speaks, which is a neck that does not turn - the right
   fallback for a frame before the body has been animated, and exactly what this
   module did before the yaw existed. */
void viewmodel_neck_set_look_yaw(float degrees);

/* The frame's neck. `spec` may be NULL, which is a weapon that follows not at
   all; `lean` is the player's normalized lean and `ads` the eased aim, which
   fades the whole thing out - at full aim the gun is welded to the eye exactly
   as it is today, which is both what makes the sights land and what a person
   bringing a gun up to their face actually does. */
void viewmodel_neck_build(ViewmodelNeck* out, const WeaponSpec* spec,
                          float view_pitch_degrees, float lean, float ads);

/* The neck as a 4x4, column-major, ready for glMultMatrixf. This is the whole
   of the transform; everything else in this header is built on it. */
void viewmodel_neck_matrix(const ViewmodelNeck* neck, float out[16]);

/* A camera-space point through the neck, for the consumers that need the drawn
   position and cannot read a GL matrix: the optic centre the stencil is cut
   around, and the muzzle a tracer leaves from. Safe to alias with `in`. */
void viewmodel_neck_transform_point(const ViewmodelNeck* neck,
                                    const float in[3], float out[3]);

/* Where the skull turns, measured off the rig rather than stated.

   The atlanto-occipital joint sits behind and below the eye, and how far is a
   fact about the rig the arms are cut from - which already knows, because
   viewmodel_clip_arms_eye walks the same step the other way to find the eye
   from the head bone. Take that step, negate it, and this is the pivot. The
   alternative is a constant standing in for anatomy, and the two constants in
   this codebase that tried that were both wrong by tens of centimetres.

   Until it is called the pivot is the eye itself, which is a neck with its
   joint in the wrong place by seven centimetres and not a neck that is
   broken - the right shape of fallback for the frames before the arms rig has
   finished loading. */
void viewmodel_neck_set_pivot(const float pivot[3]);

#endif
