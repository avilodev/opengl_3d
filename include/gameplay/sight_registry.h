#ifndef SIGHT_REGISTRY_H
#define SIGHT_REGISTRY_H

#include "gameplay/sight_spec.h"
#include "gameplay/weapon_spec.h"

/* Every optic there is, as a list a caller can walk - the same shape
   weapon_registry.h has, and here for the same reason.

   The list comes out of sight_list.h, which is the same file that declares the
   externs, so an optic that can be named at all is an optic that is in here.
   Code that knows perfectly well which sight it means still says
   SIGHT_HOLOGRAPHIC; this is for the two callers that do not - the probe that
   checks every optic's mesh loads, and the development key that puts each of
   them on the gun in turn. */

int sight_registry_count(void);

/* Always answers an optic - an index off either end is clamped - because the
   caller is a cycle key and it has nothing useful to do with a NULL. */
const SightSpec* sight_registry_get(int index);

/* Where `sight` sits in the list, or -1 for one that is not in it. NULL is not
   in it either, and answers -1: a gun with no optic is a real state, and the
   cycle key starts it at the first sight rather than treating it as an error. */
int sight_registry_index_of(const SightSpec* sight);

/* --- swapping one at runtime -------------------------------------------

   Putting a different optic on a gun means writing to its FirearmSpec, and
   those are const globals that ship. The registry keeps a mutable copy of
   every weapon and hands the copy to everyone - see weapon_registry_mutable -
   so a sight swap is a write into that copy.

   That copy used to exist only in a tuning build, and so did this: swapping
   optics was a thing a developer did to check a mount height, and the key said
   so. It is a thing the player does now, which is why neither is conditional
   any more. What changed is not the mechanism - it is the same write to the
   same copy - but who it is for, and a control the player has cannot be one
   that needs a development build.

   It lives here rather than in main.c because "which optic comes after this
   one" is a question about the rack, and because the caller that asks it
   should not also have to know that the answer is stored on a copy.

   Answers the optic the weapon is now wearing, or NULL for a weapon that has
   no rail to clamp one to - which is a sword, and is an answer rather than a
   failure. */
const SightSpec* sight_registry_cycle(const WeaponSpec* spec, int step);

#endif
