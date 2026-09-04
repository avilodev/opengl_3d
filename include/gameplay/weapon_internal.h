#ifndef WEAPON_INTERNAL_H
#define WEAPON_INTERNAL_H

#include "gameplay/weapon.h"

/* The seam between the three files weapon.c became, and nothing outside them
   includes it.

   weapon.c used to be twelve hundred lines that alternated gun code and sword
   code down their whole length: a magazine drop, then a carry blend, then a
   tracer, then a sheathe. The data had already split - a weapon carries a
   FirearmSpec or a MeleeSpec and never both - and the behaviour had not, so
   every reader of either half read past the other.

   The split follows the same line the spec draws:

     weapon.c          what every weapon has - the state, the timers, the
                       viewmodel pose, and the one dispatch that decides which
                       of the two below a frame goes to
     weapon_firearm.c  the cone, the kick, the trace, the tracers, the
                       magazine, the reload
     weapon_melee.c    the carries, the attack chain, the blade, the draw and
                       the sheathe

   What is here is only what crosses. Everything else in those two files stayed
   static, which is most of them: a gun has ten functions nothing outside it
   ever calls, and they are not in this header.

   The prefixes are load bearing. These were file-statics with names like
   `fire` and `melee_update`, which is fine while there is one file; as external
   symbols in a C11 link they are names the whole program can collide with. */

/* --- weapon_firearm.c ---------------------------------------------------- */

/* One round. Pays the cone, the kick and the ammunition, traces, and leaves a
   tracer and an impact behind. Called only with a gun half present and a
   cooldown of zero - weapon_update checks both. */
void weapon_firearm_fire(Weapon* weapon, const PlayerController* player,
                         float shot_yaw_degrees, float shot_pitch_degrees,
                         float camera_roll_degrees, float eye_height,
                         float view_fov_degrees);

/* Starts a magazine change, or answers zero for a weapon that has nothing to
   gain by one - a full magazine, a reload already running, or no gun half. The
   return is what tells weapon_update whether the frame was spent. */
int weapon_firearm_begin_reload(Weapon* weapon);

/* The one frame the support hand lets go of the old magazine, which leaves as
   a physical object. Called on the crossing of the reload track's drop point,
   not on the range past it. */
void weapon_firearm_drop_magazine(Weapon* weapon, const PlayerController* player,
                                  float aim_yaw_degrees, float aim_pitch_degrees,
                                  float camera_roll_degrees, float eye_height);

/* Falls the magazines already on their way to the floor. */
void weapon_firearm_advance_dropped_mags(Weapon* weapon, float delta_time);

/* --- weapon_melee.c ------------------------------------------------------ */

/* A melee weapon's whole frame: the attack chain, the cancels and the block.
   Reached only after weapon_update has taken the sheathe, which is the one
   button the two halves share. */
void weapon_melee_update(Weapon* weapon, const WeaponCommand* command,
                         float delta_time);

/* Eases the three carry weights toward whichever stance the player's state
   calls for. Runs before every early return in weapon_update, so a stance
   interrupted by a ledge grab resumes from where it actually is. */
void weapon_melee_carry_update(Weapon* weapon, const PlayerController* player,
                               float delta_time);

/* Advances a draw or a sheathe, and finishes it. */
void weapon_melee_advance_action(Weapon* weapon, float delta_time);

/* The track a draw or a sheathe is running, and how long it takes, or NULL
   when neither is. Read by the viewmodel pose as well as by the advance, which
   is why it crosses rather than staying inside weapon_melee.c. */
const ActionTrack* weapon_melee_action_track(const Weapon* weapon,
                                             float* seconds);

#endif
