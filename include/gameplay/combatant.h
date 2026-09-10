#ifndef COMBATANT_H
#define COMBATANT_H

#include "gameplay/combat_tuning.h"
#include "gameplay/damage_number.h"

/* A body in the world that a round can land on.

   This is the other end of the trace. The rifle has always known what it
   struck and carried a damage number to pay out; there was nothing to receive
   it. A Combatant is the smallest thing that can: somewhere to stand, a shape
   to be hit in, and a pool to take it out of.

   It is deliberately not a player and not an agent. There is no controller
   here, no movement, no decision - a combatant stands where it was placed and
   turns to face whoever is shooting at it, and that is the whole of its
   behaviour. What it is FOR is that the weapon, the hitbox arithmetic, the
   damage bands and the feedback all have to exist and agree before any of that
   is worth writing, and every one of them is testable against a body that does
   nothing.

   Death is a state on this struct and a timer beside it, and it changed nothing
   that calls into here: combatant_raycast skips a dead body and the renderer
   stops drawing one, a condition each. Where a body comes back is deliberately
   not decided in this file - combatant_tick reports that a timer ran out and
   the caller answers with a place, because the map owns the places and this
   does not know the map.

   The player is one of these. Not a slot in the set - their own variable, the
   way their CharacterInstance is their own - and filled from the controller by
   player_combatant.c the way the rig's payload is filled by
   player_visual_state.c. That is what makes health a thing the controller does
   not have to know about: it stays a function of state, input and time.

   The layering is the player's, exactly. Gameplay owns the state and the
   arithmetic and touches no GL; combatant_visual_state.h fills the rig's
   payload from it, the way player_visual_state.h does from a controller; and
   render/combatant_renderer.c owns the per-body rig state and draws it. A
   combatant that arrived over a wire would skip the first of those three and
   nothing else would know. */

/* Raising this costs a few hundred bytes of gameplay state per slot and one
   CharacterInstance per slot on the render side. The rig itself is shared, so
   a body is not a second import. */
#define COMBATANT_MAX 12

/* Which band of the hitbox a round landed in. Ordered by what it is worth, so
   "the better of two hits" is a comparison rather than a table. */
typedef enum {
    COMBATANT_REGION_LEGS,
    COMBATANT_REGION_CHEST,
    COMBATANT_REGION_HEAD,
    COMBATANT_REGION_COUNT
} CombatantRegion;

typedef struct {
    /* Where it stands - its feet, in world units, matching CharacterVisualState
       and everything else in this codebase that talks about a body. */
    float x, y, z;

    /* Which way it faces, in the atan2(vx, vz) degrees the rest of the game
       measures headings in, so zero faces +Z. Turned toward whoever is shooting
       at it at COMBAT_TURN_RATE. */
    float yaw_degrees;

    float health;
    float max_health;
    /* Seconds since a round last landed on it. The regeneration waits on this,
       which is what makes leaving a body alone the thing that heals it. */
    float quiet_time;

    /* Standing and shootable. Regeneration waits on this too, or a body that
       was killed would heal back to full without ever getting up. */
    int alive;
    /* Seconds left of being down, counting to zero. Meaningless while alive. */
    float respawn_timer;

    /* The rock a round put through it: how hard, decaying to zero, and which
       way the round was travelling as a unit horizontal direction in world
       space.

       A direction rather than the signed sideways figure this used to be. That
       one existed because the only channel a rig with no hit clip had was the
       lean, which bends about one axis - so a round taken in the chest and a
       round taken in the back produced identical pictures, and both of them
       fought the peek lean of any body that could also peek. The rig's reaction
       layer rotates this into the body's own frame and bends the spine away
       from it, which covers every angle a round can arrive from. */
    float flinch;
    float flinch_x, flinch_z;
    /* Which band it landed in, as a CombatantRegion. Picks the head snap apart
       from the body rock. */
    int flinch_region;

    int active;
} Combatant;

/* Every combatant in the world, and the numbers they have thrown.

   The numbers live here rather than beside the weapon because they belong to
   what was hit rather than to what did the hitting: the day a second player is
   also shooting these, both of them are writing into one pool anchored on one
   set of bodies, and a pool per gun would draw each hit twice. */
typedef struct {
    Combatant items[COMBATANT_MAX];
    int count;
    DamageNumbers numbers;
} CombatantSet;

/* Where a ray met a body. `index` is into the set's own array, which is what
   the damage then goes back through - a pointer would go stale the moment the
   set is copied, and this is meant to be copyable. */
typedef struct {
    int index;
    int region;
    /* Along the ray, in the same units as the max_distance that bounded it. */
    float distance;
    float x, y, z;
    float normal_x, normal_y, normal_z;
} CombatantHit;

/* Stands the arena's combatants up on whatever surface is under each of them.
   The spawn table is in combatant.c, authored as two coordinates and a facing:
   the height comes from arena_support_height, so a body placed on a roof stands
   on the roof without anyone having to restate how tall the roof is. */
void combatant_set_init(CombatantSet* set);

/* One simulation step. `player_x`/`player_z` is who they turn to face; height
   does not enter into it, because a body turns about its own spine and the rig
   has nothing that would bend to look up a tower.

   Bodies in the set that die come back where combatant.c authored them, which
   is the only spawn rule a calibration mannequin needs. */
void combatant_set_update(CombatantSet* set, float player_x, float player_z,
                          float delta_time);

/* One body's own clock: flinch settling, quiet time, regeneration, and the
   respawn count while it is down. Returns non-zero on the single step that
   count reaches zero, and does NOT stand the body up - the caller answers that
   with combatant_respawn and a place, because where a body belongs is a map
   question. A body left down and never respawned simply stays down, which is
   the correct behaviour for a mode that has run out of spawns.

   Split out of combatant_set_update because the player's body is not in a set
   and still has all of the same clock. */
int combatant_tick(Combatant* combatant, float delta_time);

/* Stands a body up: full pool, no flinch, no timer, at a place and a facing.
   Also how a body is first put into the world, so entering the map and
   re-entering it are the same act. */
void combatant_respawn(Combatant* combatant, float x, float y, float z,
                       float yaw_degrees);

/* Nearest body along the ray within `max_distance`, if any. A ray that starts
   inside a body does not hit that body, which is the rule the arena's own
   raycast follows and is what stops a muzzle from resolving against its owner.

   The set is const: a trace answers a question and never applies its own
   consequence. Handing the answer back to combatant_apply_damage is what does. */
int combatant_raycast(const CombatantSet* set,
                      float origin_x, float origin_y, float origin_z,
                      float direction_x, float direction_y, float direction_z,
                      float max_distance, CombatantHit* hit);

/* Pays `damage` into one body, banded by region, and takes it down at zero.
   Returns what was actually taken off, never more than was left.

   This is the whole of what being hit COSTS. It knows nothing about a set,
   which is what lets the player's own body - which is not in one - be damaged
   by exactly the same arithmetic as everything else. A body already down takes
   nothing: rounds pass through the dead. */
float combatant_damage(Combatant* combatant, int region, float damage);

/* The above, plus the two things that need the set and the geometry of the
   shot: the flinch, which is derived from the hit normal, and the figure thrown
   into the world for the player to read. Returns what was taken off, which is
   the number that was thrown - so a caller cannot print one amount and apply
   another. */
float combatant_apply_damage(CombatantSet* set, const CombatantHit* hit,
                             float damage);

/* What a band multiplies flat damage by. One function, so the number the trace
   resolves and the number anything checking it computes are the same one. */
float combatant_region_multiplier(int region);

/* Health as a fraction, 0 to 1. */
float combatant_health_fraction(const Combatant* combatant);

#endif
