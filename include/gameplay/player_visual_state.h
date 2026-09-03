#ifndef PLAYER_VISUAL_STATE_H
#define PLAYER_VISUAL_STATE_H

#include "gameplay/player_controller.h"
#include "render/character_state.h"

/* The one place that knows both a PlayerController and the rig's payload.

   It lives on the gameplay side of the line deliberately. The rig reads a
   CharacterVisualState and has never heard of a controller; this reads a
   controller and fills one. A body arriving over the network skips this file
   entirely - it already has the payload - which is the whole point of having
   it.

   `previous` is the state this same body was in last frame, or NULL on its
   first, and is what the one-shot event bits are the difference against: the
   controller publishes no jump or landing edge of its own, so someone has to
   notice, and here is the last place with both frames in hand. It may alias
   `out`, so the ordinary call keeps one persistent state per body and passes it
   as both.

   `look_yaw_degrees` and `look_pitch_degrees` are where this player is looking.
   For the local player that is their camera; the camera is not otherwise this
   function's business. `ads` is how far their sight is up. */
void player_visual_state_fill(CharacterVisualState* out,
                              const CharacterVisualState* previous,
                              const PlayerController* player,
                              float look_yaw_degrees, float look_pitch_degrees,
                              float ads);

#endif
