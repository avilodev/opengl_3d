#ifndef DAMAGE_NUMBER_H
#define DAMAGE_NUMBER_H

/* The figure that flies off a body when a round lands on it.

   It lives on the gameplay side for the same reason the tracers and the spent
   magazines do: its lifetime is simulation rather than drawing. It is spawned
   by the thing that resolved the damage, it rises and fades on its own clock,
   and the renderer reads it and never writes to it.

   What it is NOT is a piece of HUD. The anchor is a world point - where the
   round actually struck - and whoever draws it projects that point every frame,
   so the number stays on the shoulder it came off while the player wall-runs
   past. A number parented to the screen would have to be told which target it
   belonged to and would still slide off it the moment either party moved.

   Nothing here knows what a combatant is. A number carries an `owner` tag it
   never interprets, only compares, which is what lets the same pool serve
   melee, falling damage or an explosion the day those exist. */

#include "gameplay/combat_tuning.h"

/* Deep enough that a full magazine into a crowd cannot evict a number the
   player is still reading, given that sustained fire on one body merges into a
   single entry rather than filling a slot per round. */
#define DAMAGE_NUMBER_MAX 24

/* No owner. A number tagged this way never merges with anything, which is what
   a one-off - a fall, a blast - wants. */
#define DAMAGE_NUMBER_NO_OWNER (-1)

typedef struct {
    /* Where it is, in world units, and the sideways drift it is riding. The
       rise is the same for every number and is not stored. */
    float x, y, z;
    float drift_x, drift_z;

    /* The running total this entry has accumulated. Rounds landing on the same
       owner inside DAMAGE_NUMBER_MERGE_TIME are added here rather than given a
       slot of their own. */
    float amount;

    /* Seconds remaining. At or below zero the slot is free. Refreshed to the
       full lifetime every time the entry is added to, so a burst keeps one
       number alive and climbing. */
    float life;
    /* Seconds left on the pop the last addition gave it. This is what makes a
       merged number still read as a stream of hits: the figure jumps each time
       it grows instead of only sliding upward. */
    float pop;

    /* The band that was hit, passed through untouched for whoever draws this
       to colour by. Meaningful only to the caller. */
    int region;
    /* Which body this belongs to, for merging. Compared, never dereferenced. */
    int owner;
} DamageNumber;

typedef struct {
    DamageNumber items[DAMAGE_NUMBER_MAX];
    /* The drift's own xorshift, so the scatter is this pool's and does not move
       when something else in the process calls rand(). */
    unsigned int random_state;
} DamageNumbers;

void damage_numbers_init(DamageNumbers* numbers);

/* Records `amount` at a world point. Merges into the live entry for `owner`
   when there is one younger than DAMAGE_NUMBER_MERGE_TIME, and otherwise takes
   the slot with the least life left - so the number the player just made is
   always the one that survives a full pool. */
void damage_numbers_add(DamageNumbers* numbers, int owner, int region,
                        float amount, float x, float y, float z);

void damage_numbers_update(DamageNumbers* numbers, float delta_time);

/* How far through its pop an entry is, 0 at rest to 1 the instant it was added
   to. Public because the size a number is drawn at is the pop, and one function
   means the HUD and any check of it read the same curve. */
float damage_number_pop_blend(const DamageNumber* number);

/* The most recently added entry's age in seconds, and the band it hit. Zero if
   the pool is empty. This is what a hit marker is drawn off: a confirmation
   that a round landed is the same event as a number being spawned, so deriving
   it costs no state rather than a second timer that could disagree. */
int damage_numbers_latest(const DamageNumbers* numbers, float* age, int* region);

#endif
