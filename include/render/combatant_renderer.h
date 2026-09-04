#ifndef RENDER_COMBATANT_RENDERER_H
#define RENDER_COMBATANT_RENDERER_H

#include "gameplay/combatant.h"
#include "render/character_library.h"
#include "render/character_renderer.h"

/* The rig state for every combatant in a set, and drawing them.

   This is main.c's `body`/`body_state` pair for the local player, in the
   plural, and it exists because that pair is exactly what a second body needs:
   character_renderer.h already says a rig is shared and everything a body DOES
   with that rig is per body. So this owns one CharacterInstance and one payload
   per combatant, and nothing else - the bodies' gameplay state stays in the
   CombatantSet, on the other side of the line.

   Every body here wears the same rig index, which means one import and one copy
   of eight megabytes of mesh, skeleton and clips however many bodies there are.
   A second character type is a second character_library_add and a rig index per
   body; nothing in this file would change to allow it. */

typedef struct {
    CharacterInstance instances[COMBATANT_MAX];
    CharacterVisualState states[COMBATANT_MAX];
    /* Whether the last update skinned this body, and so whether there are
       vertices worth drawing. One flag written by the update and read by the
       draw, rather than the draw deciding for itself: a body that was culled
       holds the pose it was last skinned in, and one that has never been in
       range holds no pose at all - an instance is allocated on its first update
       but its vertices are not filled until its first VISIBLE one. Drawing
       either from the far side of the arena would put a stale figure or a
       collapsed one on screen. */
    unsigned char drawable[COMBATANT_MAX];
    int count;
} CombatantBodies;

/* Sets up one body per combatant in the set, all wearing `rig` - an index from
   character_library_add - and all carrying `weapon`. Allocation-free: the
   instances fill themselves in on their first update, once there is a loaded
   model to size them against.

   The weapon is a parameter rather than a constant in the body of this file,
   which is where it used to be: every combatant carried WEAPON_SPEC_ASSAULT_
   RIFLE because combatant_renderer.c said so, so a world of swordsmen would
   have been a world of swordsmen holding rifles. What a body carries is the
   caller's business - it is the same class of decision as which rig they wear,
   and it is made in the same place. A NULL arms them with nothing and draws
   the box fallback.

   For a body that carries something OTHER than what everyone else does, see
   combatant_bodies_set_weapon. */
void combatant_bodies_init(CombatantBodies* bodies, const CombatantSet* set,
                           int rig, const WeaponSpec* weapon);

/* What one body carries, after the fact.

   The pieces that make a mixed arena work were all in place - the third person
   draws the body's OWN weapon and resolves its clip set against whatever rig it
   is wearing - and nothing called them with two different answers. This is the
   call that does. An index outside the set is ignored rather than a crash: the
   callers are a spawn table and a debug key, and neither has anything useful to
   do with a failure. */
void combatant_bodies_set_weapon(CombatantBodies* bodies, int index,
                                 const WeaponSpec* weapon);

/* Refills each payload from its combatant and advances its animation. Once a
   frame, before drawing, and for the same reason the player's body is updated
   there: the world may be drawn twice when a magnified sight is up, and a body
   that skinned itself inside the draw would advance its clock twice on exactly
   the frames a scope was up.

   `eye_*` and `look_*` are where the camera is and which way it points, used
   only to decide which bodies are worth skinning. The clip clock runs for all
   of them either way, so a body's pose is already right the frame it comes back
   into view. */
void combatant_bodies_update(CombatantBodies* bodies, CharacterLibrary* library,
                             const CombatantSet* set,
                             float eye_x, float eye_y, float eye_z,
                             float look_x, float look_y, float look_z,
                             float delta_time);

/* Draws the poses the last update left. Safe to call more than once a frame,
   which the scope pass needs. */
void combatant_bodies_draw(const CombatantBodies* bodies,
                           const CharacterLibrary* library);

/* Releases every body's arrays and vertex buffers. Needs a current GL context.
   The rig they were wearing stays loaded - it belongs to the library. */
void combatant_bodies_free(CombatantBodies* bodies);

#endif
