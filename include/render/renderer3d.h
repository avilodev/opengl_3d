#ifndef RENDERER3D_H
#define RENDERER3D_H

void renderer3d_init(int width, int height);
void renderer3d_resize(int width, int height);
#include "gameplay/combatant.h"
#include "gameplay/player_controller.h"
#include "gameplay/third_person_camera.h"
#include "gameplay/weapon.h"
#include "render/character_renderer.h"
#include "render/combatant_renderer.h"

/* One frame. `delta_time` is frame time, and drives the character's animation
   clock; zero holds the current pose, which is what a paused game wants.

   `player` is the local player - what the camera is attached to, what the
   grapple line hangs off, and what the viewmodel is held by. `body` and
   `body_state` are that same player as something drawn in the world, and they
   are separate arguments because those are separate things: a second body would
   arrive as another pair of them and want nothing from the first three. The
   caller owns both, because a body outlives a frame - and owns `characters`
   too, which outlives every body in it.

   `combatants` and `combatant_bodies` are the other bodies in the world and the
   rig state for them, and they are the same split one more time: the first is
   where they are and what has happened to them, the second is what each of them
   is doing with the shared rig. Both may be NULL, which is a world with nobody
   else in it.

   `player_body` is what has happened to the PLAYER, and it is here for exactly
   one reason: a body that is down is not drawn, and the player's body is a body.
   Everything else about it - where it is, which way it faces - reaches the rig
   through `body_state` the way it always has. It may be NULL, which is a mode
   with no health in it, and then the body is always drawn. */
void renderer3d_draw(const ThirdPersonCamera* camera, const PlayerController* player,
                     const Weapon* weapon, CharacterLibrary* characters,
                     CharacterInstance* body, const CharacterVisualState* body_state,
                     const Combatant* player_body,
                     const CombatantSet* combatants,
                     CombatantBodies* combatant_bodies,
                     float delta_time);

/* Whether this context can draw a magnified sight as a second pass of the world
   masked to the optic's glass. It needs a stencil buffer; without one a
   magnified sight has to fall back to zooming the whole screen. */
int renderer3d_supports_scope_pass(void);
void renderer3d_shutdown(void);

#endif
