#ifndef COMBAT_TUNING_H
#define COMBAT_TUNING_H

#include "gameplay/movement_tuning.h"

/* What it costs to be hit, and what being hit looks like.

   The third tuning header, and it exists for the same reason the first two are
   apart: movement_tuning.h holds what the player's body does, weapon_tuning.h
   holds what a gun does, and neither of them owns what a TARGET is worth. A
   headshot multiplier is not a property of the rifle - every weapon in the game
   gets it - and it is not a property of the controller either. Put in one of
   those two it would have to be re-stated in the other the day a second weapon
   or a second kind of body arrives.

   Every number a designer would sweep to change how combat reads is here. */

/* --- the hitbox ---

   A vertical cylinder standing on the combatant's feet, banded by height into
   the regions below. A cylinder rather than the arena's axis-aligned boxes
   because a body turns and a box would not: the silhouette a player is aiming
   at has to be the same width from every angle, or a target is quietly easier
   to hit from its side than from its front.

   Height is the drawn body's, not a number of its own - character_renderer
   scales every rig to CHARACTER_RIG_HEIGHT, so taking it from there is what
   keeps the box the player shoots at the same size as the body they see. The
   radius is stated as a fraction of it for the same reason: re-scale the rig
   and the hitbox keeps its proportions instead of becoming a post or a barrel.

   0.16 of height is a little wider than the shoulders. That is deliberate and
   it is the one number here chosen for feel rather than measured: this is a
   game where both parties are moving fast, and a hitbox trimmed to the mesh
   punishes the player for the target's animation. */
#define COMBAT_BODY_HEIGHT CHARACTER_RIG_HEIGHT
#define COMBAT_BODY_RADIUS (COMBAT_BODY_HEIGHT * 0.16f)

/* Where the bands meet, as fractions of the height above the feet. Below the
   first is legs, between them is the chest, above the second is the head. */
#define COMBAT_LEGS_TOP 0.47f
#define COMBAT_HEAD_BOTTOM 0.86f

/* What each band multiplies the weapon's flat damage by. The spread is
   deliberately narrow at the bottom and wide at the top: legs at 0.8 is a small
   enough penalty that a player who hits the body they aimed at is never sorry,
   and the head at 2.0 is worth crossing a room for. */
#define COMBAT_DAMAGE_LEGS 0.8f
#define COMBAT_DAMAGE_CHEST 1.0f
#define COMBAT_DAMAGE_HEAD 2.0f

/* --- the player ---

   A pool of its own, and smaller than the dummy's on purpose: the mannequin is
   sized to soak a sustained burst so a player can read their hits against it,
   and a player sized the same way would make every fight a magazine long. This
   is roughly a second and a half of rifle fire, or two headshots and a little.

   The player regenerates on the same delay and at the same rate as anything
   else, because leaving a fight to heal is a decision the map's routes are
   supposed to be worth making, and a second set of numbers here would be a
   second answer to a question already answered above. */
#define COMBAT_PLAYER_HEALTH 100.0f

/* How long a body stays down. Long enough that dying costs the route the player
   was working, short enough that it does not cost them the fight their team is
   still in - the count to beat is the time it takes to run back to the middle
   of Floodgate, which is where this number goes when twelve people play it. */
#define COMBAT_RESPAWN_DELAY 5.0f

/* What the debug damage key pays in. It exists because nothing in the world can
   hurt the player yet - the mannequins have no AI and a muzzle inside its own
   owner's cylinder does not resolve against it - so the death loop needs a hand
   to drive it until the session layer arrives. Four presses is a death, which
   is enough to feel the timer without being a key you hold. */
#define COMBAT_DEBUG_DAMAGE 25.0f

/* --- the training dummy ---

   A pool this size soaks about five and a half seconds of sustained rifle fire,
   and it regenerates once the shooting stops, so a player can keep working a
   route without a respawn timer deciding when they are allowed to. It is set
   this high for exactly that reason and not because a mannequin is tougher than
   a player: it is a calibration target, and a target that dies in a burst is a
   target you spend the session waiting for.

   They do now die, on the same rules as anybody else - one state and one timer,
   shared - and come back where they were authored. None of the numbers above
   changed to make that true. */
#define COMBAT_DUMMY_HEALTH 250.0f
#define COMBAT_REGEN_DELAY 3.0f
#define COMBAT_REGEN_RATE 140.0f

/* How fast a body turns to face whoever is shooting at it, in degrees a second,
   and how far off it can be before it bothers. Not AI - there is no decision
   here, only a facing - but it is what makes the head and chest bands mean
   something, since a target that never turns can only ever be shot from the
   angle it was authored at. */
#define COMBAT_TURN_RATE 220.0f
#define COMBAT_TURN_DEADZONE 1.5f

/* The rock a body takes when a round lands, as a fraction of the rig's own
   lean, and how fast it settles. The rig has no hit reaction clip, so this
   borrows the lean channel it does have: the spine bends and the feet stay
   planted, which is exactly the shape of a flinch. */
#define COMBAT_FLINCH_AMOUNT 0.55f
#define COMBAT_FLINCH_DECAY 4.5f

/* --- the damage number ---

   Anchored in the world at the point the round landed and projected to the
   screen every frame, so it stays on the body it belongs to rather than being
   a piece of HUD that happens to appear. */
#define DAMAGE_NUMBER_LIFETIME 1.05f
/* Metres a second: up, and a small sideways drift so two numbers in the same
   place do not stack into an unreadable smear. */
#define DAMAGE_NUMBER_RISE 1.15f
#define DAMAGE_NUMBER_DRIFT 0.5f
/* Rounds arriving inside this window on the same body are added to the number
   already there instead of spawning another one. At the rifle's hundred rounds
   a minute, one number per round is ten a second on one target and the player
   reads none of them; accumulated, the number climbs and the player reads the
   total, which is the thing they actually want to know. */
#define DAMAGE_NUMBER_MERGE_TIME 0.22f
/* How far the number pops out over its size when it is spawned or added to,
   and how long that takes to settle. The re-pop is the whole reason a merged
   number still reads as a stream of hits rather than as one. */
#define DAMAGE_NUMBER_POP 0.55f
#define DAMAGE_NUMBER_POP_TIME 0.12f
/* On-screen size at DAMAGE_NUMBER_REFERENCE metres, and the range the distance
   falloff is allowed to move it over. Left unclamped a number at forty metres
   is a pixel high and one at two metres fills the screen. */
#define DAMAGE_NUMBER_SCALE 0.9f
#define DAMAGE_NUMBER_REFERENCE 8.0f
#define DAMAGE_NUMBER_SCALE_MIN 0.55f
#define DAMAGE_NUMBER_SCALE_MAX 1.35f

/* How long the crosshair's hit marker stays up. Short: it is a confirmation,
   not a readout, and the number beside it is what carries the detail. */
#define COMBAT_HIT_MARKER_TIME 0.16f

#endif
