#ifndef HUD_H
#define HUD_H

#include "gameplay/combatant.h"
#include "gameplay/weapon.h"
#include "gameplay/third_person_camera.h"

/* The combat overlay: a crosshair whose gap is the weapon's real cone measured
   in pixels, an ammo counter, and everything the player is told about what they
   just hit - the figures that fly off a body, the marker that confirms a round
   landed, and a pool bar over anything that is not at full health.

   The camera's live vertical FOV is what turns an angle into a distance on
   screen, so the crosshair keeps telling the truth while speed is widening the
   view, and `scope_fov_degrees` is the same for the inside of a magnified
   optic, which is drawn at its own field of view and where that optic's reticle
   lives.

   `player` is here because half of what this draws is anchored in the WORLD
   rather than on the screen: a damage figure belongs to the shoulder it came
   off, and turning a world point into a screen point needs what the camera is
   orbiting as well as the camera. `combatants` may be NULL, which is a world
   with nothing in it to shoot - the crosshair and the ammo counter do not
   depend on there being anything out there.

   `player_body` is the player's own pool and the count while they are down, and
   may be NULL for a mode that has no health in it. It is a Combatant rather
   than two floats because that is what the player IS now - the same struct
   every other body in the world is, filled from the controller - and passing
   the parts would be inventing a second answer to what the player's health is.

   The player's own readout is the only thing here drawn at a fixed place on the
   screen rather than over something in the world, which is why it sits opposite
   the ammo counter: those two are what the player glances at, and everything
   else is drawn where they are already looking. */
void hud_draw(const Weapon* weapon, const ThirdPersonCamera* camera,
              const PlayerController* player, const CombatantSet* combatants,
              const Combatant* player_body,
              int viewport_width, int viewport_height);

#endif
