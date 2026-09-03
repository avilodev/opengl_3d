#ifndef SPAWN_SELECT_H
#define SPAWN_SELECT_H

#include "gameplay/arena.h"
#include "gameplay/combatant.h"

/* Which of a team's deployment slots to come back at.

   It is its own file because it is the seam INTENT names: the map publishes
   both six-player sets through arena_team_spawns and the mode decides which
   slot is safe and when. Written inline in main.c it would be three lines that
   have to move again the moment there is a session layer asking the same
   question for twelve people; written here it is a pure function of a team and
   a list of bodies, it needs no GL and no window, and the probe can hold it to
   its own rule.

   The rule is the simplest one that is not a lie: take the slot whose nearest
   live body is furthest away. It is not spawn protection, it does not know
   about sight lines, and it has no opinion about which body is an enemy -
   today every body in the set is a calibration mannequin and there is nobody
   to be on a team with. All three of those are session-layer work and all three
   want twelve humans to tune. What this does buy, now, is that a player killed
   at Red Control does not come back into the gun that killed them. */

/* Fills `out` with the chosen slot and returns non-zero. Returns zero, leaving
   `out` untouched, only when the team has no slots at all - which is a map
   authoring error and not a state the caller can do anything useful about.

   `bodies` may be NULL, or every body in it dead, which is a world with nothing
   to avoid: the first slot is then as good as any other and is what comes back.
   Dead bodies are skipped, because a corpse is not a threat and treating one as
   one would push the player away from the safest ground on the map. */
int spawn_select(ArenaTeam team, const Combatant* bodies, int body_count,
                 ArenaSpawn* out);

#endif
