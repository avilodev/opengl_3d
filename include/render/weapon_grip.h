#ifndef RENDER_WEAPON_GRIP_H
#define RENDER_WEAPON_GRIP_H

#include "gameplay/weapon_spec.h"
#include "utils/mat4.h"

/* How a fist meets a weapon, per hold, in the WEAPON's own frame.

   ONE module because there are two cameras and they have to agree. This used to
   be two unrelated answers: a private table in viewmodel_arms.c that the
   first-person solver walked its wrists back along, and three #defines in
   character_transform.c naming "the middle of the RIFLE's pistol grip" - a
   number then applied to every weapon a third-person body ever held, katana
   included. A sword held 0.076m below its own hilt in one view and on it in the
   other is not a tuning difference; it is two files that were never introduced.

   Per HOLD and not per weapon, for the reason the original table gives: what a
   wrist does is decided by what it is holding and not by which one it is. A
   weapon states its hold and its grip points and gets both fists for free.

   The frame convention: a weapon's -z runs toward the muzzle or the tip and +y
   is its top. The rig's hand bone points +y along the fingers, +z toward the
   index side, and -x out through the palm. weapon_grip_hand_rotation is the
   change of basis between those two, BUILT from the directions rather than
   stated as an angle - which is what makes a new hold a data change and what
   stops the quarter turn that was right for a rifle being spent on a sword. */

/* Which way the fingers run, in the weapon's frame. `hand` is 0 for the right
   and 1 for the left. NULL if the hold is not one this knows. */
const float* weapon_grip_hand_dir(int hold, int hand);

/* Which way the palm faces, same frame and same indexing. An all-zero vector is
   a hand deliberately left free to roll - see the support hand on a gun - and a
   caller that needs a full rotation must treat it as absent rather than as a
   direction pointing nowhere. */
const float* weapon_grip_palm_dir(int hold, int hand);

/* The rotation taking the weapon's frame into that hand bone's frame: the
   fingers direction onto +y, the palm direction onto -x, and the third axis
   wherever those two leave it.

   Returns 0 - writing nothing - for a hold this does not know, for a hand whose
   palm is left free, and for a degenerate pair whose two directions are
   parallel enough that the cross product is noise. A caller handed 0 has no
   business inventing an orientation; drawing nothing is what an unknown hold
   deserves, and is what a weapon with no stated hold used to get by accident
   through a rotation that happened to suit exactly one of them. */
int weapon_grip_hand_rotation(int hold, int hand, Mat4* out);

#endif
