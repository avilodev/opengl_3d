#ifndef PLAYER_COMBATANT_H
#define PLAYER_COMBATANT_H

#include "gameplay/combatant.h"
#include "gameplay/player_controller.h"

/* The one place that knows both a PlayerController and a Combatant.

   It is the exact counterpart of player_visual_state.h, and it is here for the
   same reason. That file reads a controller and fills the rig's payload; this
   one reads a controller and fills the body a round can land on. Neither
   direction is the controller's business: the controller stays a function of
   state, input and time, with no health on it and no opinion about being shot,
   which is what keeps it testable as the thing whose feel a tuning change can
   quietly move.

   A player arriving over a wire skips this file entirely - they already have a
   combatant, sent as one - which is the same promise player_visual_state makes
   and the reason both of them are one function and no state.

   The player's combatant is a variable of the mode's, not a slot in the
   CombatantSet. Every consumer of that set draws the body it names and puts a
   pool bar over it, and the local player wants neither; carving two exceptions
   into the renderer and the HUD to hide one entry would cost more than the day
   the session layer unifies the roster and answers it properly. */

/* Stands the player up as a body: their own pool, at the controller's pose.
   Called once when they enter the map. */
void player_combatant_init(Combatant* out, const PlayerController* player);

/* Where they are now. Position and facing only - health, the respawn count and
   the flinch belong to the combatant and are never written from here.

   A body that is down is not moved. The controller is not stepped while the
   player is dead so it would not have moved anyway, but saying so here is what
   makes the corpse's place a property of where they died rather than an
   accident of what the mode happens to be calling. */
void player_combatant_fill(Combatant* out, const PlayerController* player);

#endif
