#ifndef COMBATANT_VISUAL_STATE_H
#define COMBATANT_VISUAL_STATE_H

#include "gameplay/combatant.h"
#include "render/character_state.h"

/* The one place that knows both a Combatant and the rig's payload, and the
   exact counterpart of player_visual_state.h - same job, same side of the line,
   same reason for existing.

   A combatant stands still, so most of the payload is zero and the interesting
   part is the two yaws. The rig takes a heading in atan2(vx, vz) degrees and a
   look direction in the camera's, which are half a turn apart; a body whose
   look and heading disagree is drawn with its legs twisted back under a chest
   that has turned, which is right for a player strafing past a doorway and
   wrong for a body simply standing and facing you. So both are set from the one
   facing and the twist comes out at zero by construction.

   A combatant arriving over a wire would skip this file entirely - it would
   already have the payload - which is the whole point of the payload. */
void combatant_visual_state_fill(CharacterVisualState* out,
                                 const Combatant* combatant);

#endif
