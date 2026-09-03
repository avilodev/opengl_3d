#ifndef MELEE_SWING_H
#define MELEE_SWING_H

#include "gameplay/combatant.h"
#include "gameplay/weapon_spec.h"
#include "utils/mat4.h"

/* What a swing hits, and how a blade decides it.

   A shot is a ray from one point in one direction and resolves in one call. A
   swing is neither: the blade is a SEGMENT rather than a point, it is live for
   a window rather than an instant, and over that window it moves - a katana's
   tip runs past twenty metres a second at the middle of a cut. So this file
   answers three questions the gun's own hit detection never had to:

     * WHERE the blade is. Two points from the spec, taken through the
       weapon's live transform. Not the mesh bounds: a bounding box takes in
       the grip and the guard, and a sword that cut with its handle is a
       surprise nobody debugs quickly.

     * WHEN it is live. The attack's own window, as a fraction of its clip. A
       blade is a solid object for the whole animation and only CUTS while the
       edge is travelling; without the window a wind-up that passes through
       somebody kills them before the swing starts.

     * WHETHER it has already hit. One body, one swing. A three-frame active
       window with no memory pays out three times, so a katana would do triple
       damage at 60fps and double at 30 - a bug that arrives disguised as game
       feel and gets tuned around rather than found.

   And the sweep, which is the part that cannot be skipped. Testing the blade
   only where it is this frame tunnels straight through people: at a tip speed
   of 20 m/s the tip covers 33cm in one 60Hz frame, which is wider than a
   torso. So the segment is stepped from where it was to where it is, at a
   chord fine enough that nothing person-sized fits between two steps - a slow
   swing costs one step and a fast one costs as many as it needs. */

/* Bodies already cut by the swing that is running. Sized to the set, because
   a wide cut through a crowd is exactly the case that has to keep its
   memory. */
#define MELEE_SWING_MAX_HITS COMBATANT_MAX

/* How far apart two samples of the blade may be, in metres. Under a torso's
   width so nothing person-sized can sit between two of them unseen. */
#define MELEE_SWING_CHORD 0.12f

/* And a ceiling on the count, so a transform that arrives garbage - a NaN, a
   frame the game was paused for - costs a frame rather than the process. */
#define MELEE_SWING_MAX_STEPS 24

typedef struct {
    /* Which attack is running, as an index into the spec's own table, or -1.
       The same number the animation is playing, handed in rather than kept in
       step: two clocks for one swing is how a blade ends up live during the
       wrong animation. */
    int attack;
    /* Fraction of the attack's clip, 0 to 1. */
    float progress;

    int hit[MELEE_SWING_MAX_HITS];
    int hit_count;
} MeleeSwing;

/* Clears the swing and starts `attack`. Call on the frame an attack begins;
   the hit list is what it is really clearing, and forgetting to is the triple
   damage above. */
void melee_swing_begin(MeleeSwing* swing, int attack);

/* Ends it, so nothing is live and the next begin starts clean. */
void melee_swing_end(MeleeSwing* swing);

/* One step of a running swing. `prev` and `now` are the weapon's transform on
   the last frame and this one, in whatever space the bodies are in; `progress`
   is where the clip has got to. Applies damage through combatant_apply_damage,
   so banding, flinch and the floating figure are the rifle's - a sword that
   paid damage its own way would be a second set of rules to keep in step.

   Returns how many bodies it cut this step. */
int melee_swing_update(MeleeSwing* swing, const WeaponSpec* spec,
                       const Mat4* prev, const Mat4* now,
                       float progress, CombatantSet* targets);

#endif
