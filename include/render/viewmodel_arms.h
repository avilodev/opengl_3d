#ifndef RENDER_VIEWMODEL_ARMS_H
#define RENDER_VIEWMODEL_ARMS_H

#include "render/character_library.h"
#include "gameplay/weapon_spec.h"
#include "utils/mat4.h"

/* First-person arms, cut out of the body rig the player already wears.

   No new asset and no second skeleton: the same UAL1 rig that draws the body in
   third person is drawn here from the shoulders out, and the rest of it is left
   unbuilt by a SkinnedSubset rather than hidden. That is the whole trick - the
   player is standing inside their own chest, so what has to go away is the
   geometry and not the bones, because the bones are what the arms hang off.

   The hands are solved TO the gun, not the other way round. In third person the
   hand drives the rifle: character_renderer reads hand_r and hangs the mesh off
   it. Here the weapon owns its own pose - aiming, bob, sway and recoil all move
   it - so the arms have to reach whatever it did, which is two-bone IK per side
   against the same solver the ledge hang uses.

   It is deliberately a look and never a fact. Nothing in gameplay reads the
   arms: the bullet leaves the eye and the reticle rides the gun, so a hand that
   lands a centimetre off its grip costs a frame of fidelity and nothing else. */

/* Draws the arms in the caller's current transform, which must be CAMERA space
   - the arms are not children of the gun.

   `gun_matrix` is the gun's own camera-space transform, read straight off the
   modelview stack while it was current rather than recomposed here. The grips
   are points on the weapon mesh, so this is what turns them into the targets
   the hands reach for, and taking the live matrix is what stops the arms from
   drifting off a gun whose pose changed.

   Silently draws nothing until the rig is loaded, which is the same bargain
   every other lazily loaded mesh here makes. */
/* `spec` is where the gun states its own half of the hold - the two points its
   hands close on, and what kind of grip it is. Everything about the arms
   themselves stays here; where the shoulders have to be to reach those points is
   solved from them the first frame a weapon is drawn. So a new gun is two
   measurements in weapon_specs.c and nothing in this file.

   The blends arrive already resolved - each one put through the weapon's own
   accessor, so the precedence between them is settled before it gets here. */
typedef struct {
    /* weapon_sprint_blend and weapon_reload_blend: how much of each carry the
       gun is wearing. */
    float sprint;
    float reload;
    float ads;
    /* Where in the magazine change the hands are, 0 to 1. Unlike the three
       above this is a CLOCK and not a weight: it says which part of the track
       to read, while `reload` says how much of it to apply. */
    float reload_progress;
} ViewmodelBlend;

/* Draws the arms. The shoulders and the blade move on every blend above,
   because one placement cannot put the hands on a gun that travels from the hip
   to the chest to the eye - the sprint carry alone leaves the support arm
   folded to 0.28 of its reach from the hip's placement, and comfortable from
   its own.

   A reload asks more of that than the carries do. The support hand leaves the
   weapon entirely, and the placement that reaches a handguard reaches neither
   the magazine well nor a pouch on the chest: measured against this rig they
   come out at 0.46 and 0.27 of reach, both folded far enough to throw an elbow
   through the sight picture. So the reload's keyframes are solved for the same
   way the three carries are - one placement each, at load - and the shoulders
   walk between them as the hand travels. That the shoulders drift is
   anatomically nonsense and entirely invisible, since a placement that would
   put one inside the frame is refused outright. */
void viewmodel_arms_draw(const CharacterLibrary* library, int rig,
                         const WeaponSpec* spec,
                         const float gun_matrix[16],
                         const ViewmodelBlend* blend);

/* Where the support hand is this frame, in camera space, given the gun's live
   transform. Exposed because the magazine has to be drawn in that hand while it
   is being carried, and a second copy of this arithmetic in the renderer is how
   the hand and the magazine would come to disagree. */
void viewmodel_arms_support_target(const WeaponSpec* spec,
                                   const ViewmodelBlend* blend,
                                   const Mat4* gun, float out[3]);

/* Draws an arm pose that came from somewhere else - a swing clip, rather than
   this file's solve. The same instance, the same cut, the same anchor and
   scale; only the pose is another module's. Exposed so the clip-driven arms do
   not need a second skinned instance and a second copy of the forearm cut,
   which is the only part of this file a sword has any use for.

   Returns 0 when the rig is not ready, exactly as the solve path would. */
int viewmodel_arms_draw_pose(const CharacterLibrary* library, int rig,
                             const Pose* pose);

/* Releases the instance and the cut. Needs no GL context - a subset is index
   arrays and an instance is vertices - but call it at shutdown alongside the
   rest. */
void viewmodel_arms_shutdown(void);

#endif
