#ifndef MOVEMENT_TUNING_H
#define MOVEMENT_TUNING_H

/* The single tuning surface for the whole momentum/traversal system. Anything
   that changes how movement feels belongs here, not inline in a subsystem. */

/* --- ground and air --- */
#define MOVE_WALK_SPEED 5.25f
#define MOVE_SPRINT_SPEED 9.0f
#define MOVE_CROUCH_SPEED 2.25f
/* Ground acceleration is Quake's: the addition is made *along the wish
   direction only*, and it is gated on the projection of current velocity onto
   that direction rather than on total speed. Perpendicular momentum is never
   deleted, so turning carves instead of pivoting, and friction - not the
   accelerator - is what removes the old direction. The per-second rate is this
   number times the target speed, so a sprint accelerates harder than a walk. */
#define MOVE_GROUND_ACCELERATION 10.0f
/* Unlike the ground rate above, this is a plain metres per second squared and
   is not multiplied by the target speed. It is also what actually governs the
   air strafe: the per-frame addition it allows stays well under
   MOVE_AIR_WISH_SPEED, so the headroom below is a ceiling the strafe rarely
   touches and this is the dial deciding how fast speed compounds. It was 20,
   which put the gain at 0.33 m/s per second against a drag ramp reaching 1.0 -
   a perfect strafe went backwards after two thirds of a second and converged
   on an air speed of 3.3 m/s. At 48 the gain is Quake's and the soft ceiling
   lands at 32 m/s. */
#define MOVE_AIR_ACCELERATION 48.0f
/* Air acceleration may only ever push the player up to this speed *along the
   direction they are asking for*. It is the whole air-strafe mechanic: holding
   a key straight ahead stops helping almost immediately, while turning the
   mouse so the wish direction stays near-perpendicular to velocity keeps a
   little headroom open every frame and compounds it into speed. The gain that
   falls out of this decays as 1/speed, so fast players gain less per second
   than slow ones and the curve has a soft ceiling instead of a hard cap. */
#define MOVE_AIR_WISH_SPEED 4.0f
/* Air strafing is how a player earns speed, but it is not how most players
   *steer*, and a jump whose direction cannot be corrected reads as a loss of
   control. This rotates velocity toward the wish direction without changing
   its magnitude, so it buys aim and never speed. It is scaled by how far the
   wish direction already points along the way the player is travelling and
   switched off entirely once it does not, which is exactly the case a pure
   air strafe lives in - so steering costs the strafe nothing. */
#define MOVE_AIR_STEER 2.0f
/* Friction is proportional to speed, not a constant deceleration, so it is an
   exponential settle rather than a brake pedal - this number is 1/tau, so 4.0
   bleeds to a third of the speed in a quarter second. Below the stop speed it
   is charged as if the player were at the stop speed, which is what still
   brings a slow walk to a crisp halt instead of an infinite crawl.

   It applies on every grounded frame, whether or not the player is asking to
   move, because Quake acceleration only ever adds along the wish direction and
   has no way to remove the old one. The pair is what makes the ground feel
   like ground; the answer to the cost is not to be on it, which is what the
   jump, the slide, and every wall verb are for. */
#define MOVE_GROUND_FRICTION 4.0f
#define MOVE_GROUND_STOP_SPEED 2.5f
#define MOVE_JUMP_SPEED 9.4f
#define MOVE_GRAVITY 18.0f
/* --- the jump kit ---
   Titanfall's pilots carry one and it is not a bonus, it is half the movement
   system: the verb that makes a committed jump recoverable. Without it every
   decision made leaving a surface is final, which punishes commitment instead
   of rewarding it.

   Refreshed by contact with anything - ground, wall run, wall climb, mantle -
   so a chained route always has one in hand and only a genuine flight through
   open air runs out. */
#define MOVE_AIR_JUMP_COUNT 1
/* Below MOVE_JUMP_SPEED, so leaving the ground is still the better jump, and
   set rather than added so it catches a fall however fast it had become. */
#define MOVE_AIR_JUMP_SPEED 8.0f
/* How far the kit turns the heading toward the player's input, at unchanged
   speed. This is the correction the move exists for; at 0 it is only height. */
#define MOVE_AIR_JUMP_STEER 0.35f
#define MOVE_COYOTE_TIME 0.11f
#define MOVE_JUMP_BUFFER_TIME 0.12f
#define MOVE_WALK_ANIMATION_SPEED 9.0f

/* --- leaning ---
   The player publishes a normalized pose; these constants decide how the
   different presentations interpret it without duplicating input logic. */
#define MOVE_LEAN_RESPONSE 12.0f
#define VIEW_LEAN_ROLL 11.0f
/* How far the view banks into a wall run. This is the single loudest signal
   that the player is on a wall rather than in the air, and it was previously a
   bare 7 in the camera - too shallow to read at a glance. */
#define VIEW_WALL_RUN_ROLL 16.0f
/* How far the eye slides off the body's centreline. This is the whole point of
   a lean - it is what puts the head past the corner while the body stays behind
   it - so it is sized to a head's width off centre rather than to a hint of
   one. The body's own bend below is aimed at the same displacement, so the
   drawn head arrives where the eye already is. */
#define VIEW_LEAN_OFFSET 0.32f
#define VIEWMODEL_LEAN_ROLL 9.0f

/* --- the leaning body ---
   A lean is a bend, not a tilt: the spine takes it, the feet stay planted, and
   the gun goes wherever the hands that hold it go. So the roll below is spread
   over the pelvis, the three spine joints, the neck and the head - see
   TWIST_BONES in character_animation.c for the shares - rather than applied to
   the whole body about a made-up waist point, which swung the legs out with it
   and looked like a body falling over.

   Twenty degrees of spine over the metre or so above the hips carries the head
   about a third of a metre sideways, which with the hip shift below lands it
   near enough where VIEW_LEAN_OFFSET puts the eye. */
#define CHARACTER_LEAN_ROLL 34.0f
/* And how far the hips slide underneath it. Small on purpose: the weight
   shifts onto the outside foot, but the feet themselves stay where they were,
   which is what separates a peek from a sidestep. */
#define CHARACTER_LEAN_OFFSET 0.06f
/* The box fallback body has no spine to bend, so it gets the old whole-body
   tilt. Only the seven-box rig ever sees these. */
#define CHARACTER_BOX_LEAN_ROLL 13.0f
#define CHARACTER_BOX_LEAN_TWIST 8.0f

/* How tall the character stands, measured from the box rig this replaced: its
   head box topped out here, and the camera's eye height and the weapon anchors
   were tuned against that. A loaded mesh is scaled to this height rather than
   drawn at its authored size, so swapping the model cannot quietly move the
   player's eyes. */
#define CHARACTER_RIG_HEIGHT 2.53f

/* --- the shape of a jump ---
   A jump pays out immediately and then costs, so the fast way through the
   arena is a lot of short hops off surfaces rather than one long float. The
   burst is added to the speed the player was already carrying, so it is worth
   more the faster they were going; from there both drag and gravity ramp with
   time spent off any surface, which is what turns hanging in the air from
   free into expensive. Touching anything - ground, wall run, wall climb,
   mantle - resets the ramp, so a chained route never pays it.

   Not applied to a slide jump, which has its own burst, or a wall jump, which
   has MOVE_WALL_JUMP_BURST. */
#define MOVE_JUMP_SPEED_BURST 1.5f
/* The burst is a flat addition, so on its own it is a pump with no ceiling:
   a perfectly timed hop is grounded for a single step and pays no friction at
   all, which is exactly what makes bunny hopping work and also what makes it
   unbounded. Every other burst in this file is bounded, and these are the
   bounds for the two that were not.

   As everywhere else here, the ceiling limits what the burst may *add* and is
   never a speed limit on the player: arriving above it means the jump simply
   pays nothing, and no speed is ever taken. Air strafing, wall chains, slides
   and the grapple all still run past it - they are what the space above the
   ceiling is for. Hopping alone is meant to be worth doing and not worth doing
   forever. */
#define MOVE_JUMP_BURST_CEILING 17.0f
#define MOVE_WALL_JUMP_BURST_CEILING 26.0f
/* Horizontal speed lost per second, at the start of a jump and at the end of
   the ramp. Vertical is the same idea: gravity starts at MOVE_GRAVITY and
   builds to MOVE_AIR_GRAVITY_END over the same time.

   The ramp is deliberately near-free for the first half second or so, which is
   most of an ordinary jump - a sprint hop hangs for 0.98s: a trajectory the
   player aimed has to
   land where they aimed it, and drag that bites inside a normal hop makes
   every jump undershoot and reads as the air fighting them. The cost is meant
   for a genuine float, not for a hop. Setting DRAG_END to 0 and
   AIR_GRAVITY_END equal to MOVE_GRAVITY turns the whole idea off. */
#define MOVE_AIR_DRAG_START 0.0f
#define MOVE_AIR_DRAG_END 0.6f
#define MOVE_AIR_GRAVITY_END 22.0f
#define MOVE_AIR_RAMP_TIME 1.4f
/* Overspeed needs no special case any more. Quake acceleration simply stops
   contributing once the player is already faster than the target *along the
   direction they asked for*, and the friction above bleeds the surplus as an
   exponential settle rather than snapping to the target. Steering still works
   at any speed, because a wish direction across the current one always has
   headroom. */

/* --- wall running --- */
/* The speed a wall run drives toward, not a floor under it - the run
   accelerates any arrival slower than this and never slows a faster one. Both
   sit above their ground equivalents on purpose: a wall has to be worth
   getting onto, and in Titanfall it is the surface you go fast on rather than
   the one you survive on. The accelerator is sized to cover the gap from a
   ground sprint inside about a second, well within MOVE_WALL_MAX_TIME. */
#define MOVE_WALL_WALK_SPEED 9.0f
#define MOVE_WALL_SPRINT_SPEED 15.0f
#define MOVE_WALL_ACCELERATION 7.0f
#define MOVE_WALL_CHAIN_BONUS 0.75f
#define MOVE_WALL_MAX_TIME 1.8f
#define MOVE_WALL_MIN_HEIGHT 0.35f
/* How much of the wish direction has to run ALONG a wall for the contact to be
   a run. It is the sine of the angle off the wall's normal, so 0.25 accepts
   anything more than about 15 degrees away from driving straight into it.

   This and MOVE_WALL_CLIMB_APPROACH split every contact between the two wall
   verbs, and the split used to be in the wrong place: the climb took everything
   within 35 degrees of head-on, which is to say everything from about 55
   degrees of straight-at-the-wall onwards. A player going for a wall ride looks
   at the wall and holds forward, and the wish direction is camera relative, so
   that is exactly the approach they make - they were being handed a
   half-second climb, pushed back off, and left to fall. Now only a genuinely
   square approach climbs and everything else rides. */
#define MOVE_WALL_TANGENT_MIN 0.25f
/* The along-wall speed a contact needs before it may *start* a run, in metres
   per second. The tangent gate above asks whether the player is pointing along
   the wall; this asks whether they are actually travelling along it.

   Without it a contact square to the player's travel is still taken as a run,
   and since the run carries only the along-wall component there is nothing for
   it to carry: the player is pinned to the surface at a standstill by a wall
   they merely collided with. Low enough that a walk into a wall at the
   steepest angle the tangent gate allows still attaches - about 1.3 m/s along
   the surface. Checked only when attaching; a run under way is never dropped
   by it. */
#define MOVE_WALL_ATTACH_ALONG_MIN 1.0f
#define MOVE_WALL_REATTACH_DELAY 0.25f
/* Steering this far away from the wall lets go of it. The run overwrites
   velocity with the along-wall tangent every frame, so without an explicit
   rule a player pushing away is projected straight back onto the surface and
   stays glued until their input points almost exactly at the normal. This is
   the dot product of the wish direction with the outward normal: 0 is along
   the wall, 1 is straight away, and 0.55 is about 33 degrees off the surface. */
#define MOVE_WALL_DETACH 0.55f
#define MOVE_WALL_DETACH_PUSH 2.0f
/* The kick off the wall is the biggest single displacement in the game, so it
   is three separate numbers and not one: SPEED is the vertical launch, which
   buys the airtime the jump spends travelling; PUSH is the shove along the
   outward normal, which is what actually carries the player away from the
   surface; BURST is added to the speed they were already carrying, so a wall
   jump always pays out more than it costs and a chain accelerates. */
#define MOVE_WALL_JUMP_SPEED 11.0f
#define MOVE_WALL_JUMP_PUSH 7.5f
#define MOVE_WALL_JUMP_BURST 3.5f
/* How far the wall jump steers the carried speed toward where the player is
   looking. The kick off the normal is fixed; this is what makes a wall jump a
   routing decision rather than whatever the wall happened to hand back. */
#define MOVE_WALL_JUMP_LOOK 0.55f

/* A wall run is an arc, not a slide: the player attaches with a fixed climb
   rate, rises while wall gravity is still weak, apexes around two thirds of
   the way through, and is sinking by the time the timer drops them. Attaching
   normalizes vertical speed so the arc reads the same however they arrived.
   As tuned: apex 3.38m at 67% through, ending 2.14m above the attach point.

   Wall gravity ramps over the *fraction* of MOVE_WALL_MAX_TIME elapsed, not
   over wall-clock seconds, so these three do not scale with world gravity and
   cannot be read independently of the run length. Lengthening the run while
   raising the ramp is what turns the arc upside down - the added time is all
   spent near the top of the ramp - so a longer run needs a *gentler* end, not
   a stiffer one. That is why END is below world gravity and below what it was
   when the run was shorter. Change MOVE_WALL_MAX_TIME and these move with it. */
#define MOVE_WALL_ATTACH_RISE 5.0f
#define MOVE_WALL_ATTACH_SINK 1.5f
#define MOVE_WALL_GRAVITY_START 2.5f
#define MOVE_WALL_GRAVITY_END 7.5f
#define MOVE_WALL_SINK_MAX 6.0f
/* --- wall climbing --- */
/* Running head-on at a wall climbs it. This is the trade the wall run is not:
   horizontal speed is spent to buy height, so a fast approach climbs higher. */
#define MOVE_WALL_CLIMB_TIME 0.55f
#define MOVE_WALL_CLIMB_ENTRY_SPEED 4.0f
/* The cosine of the angle off the wall's normal, so 0.94 is about 20 degrees:
   a climb now needs a nearly square run at the wall, which is what running up
   one should cost. Everything shallower is a wall run - see
   MOVE_WALL_TANGENT_MIN, which this is paired with. */
#define MOVE_WALL_CLIMB_APPROACH 0.94f
#define MOVE_WALL_CLIMB_CONVERSION 0.62f
#define MOVE_WALL_CLIMB_MIN_SPEED 4.0f
#define MOVE_WALL_CLIMB_MAX_SPEED 7.0f
#define MOVE_WALL_CLIMB_HOLD 1.0f
#define MOVE_WALL_CLIMB_EXIT_PUSH 1.2f
/* MAX_SPEED caps the climb rate, so an arrival faster than the rate can absorb
   has speed the climb will not convert. That surplus is handed back along the
   wall instead of being deleted - without this, a fast approach came out slower
   than a slow one and a four-degree change in aim decided whether a run kept
   its speed or lost half of it. A dead-square approach has no along-wall
   direction to receive the surplus, so there it lifts the climb instead; this
   is the along-wall speed over which it is fully lateral, blended below so
   neither end is a cliff of its own. */
#define MOVE_WALL_CLIMB_SURPLUS_ALONG 1.0f
/* There is no square-on exclusion band any more. A dead-on approach used to be
   called an impact and have its velocity zeroed, which meant the most natural
   input in the game - run at the wall - produced the worst outcome in it, and
   it deleted momentum as a side effect, which nothing here is allowed to do.
   Running straight at a wall now runs up it, which is what the climb is for;
   MOVE_WALL_CLIMB_SURPLUS_ALONG was already written for the dead-square case.
   A contact too slow to buy a climb is handled by collision clipping instead,
   which redirects the player along the surface rather than stopping them. */

/* --- sliding --- */
#define MOVE_SLIDE_MIN_SPEED 5.25f
#define MOVE_SLIDE_BURST 3.5f
#define MOVE_SLIDE_MAX_SPEED 16.0f
/* A slide is a way to carry speed across ground, not a way to make it. On a
   slope it would be a generator and gravity would pay for it; this arena is
   flat boxes on a flat plane, so there is no slope anywhere and a slide that
   accelerates is simply a faster walk with no cost. Holding forward used to do
   exactly that, which made crouch-and-forward strictly better than sprinting
   and turned a twelve-metre-a-second slide into one that never ended.

   So both cases decay and only the rate differs: holding forward is a long
   committed line, hands off is a shorter one, and back on the stick is the
   brake. From twelve metres a second that is about four seconds of slide held
   and three left alone, which is a route rather than a way of life. */
#define MOVE_SLIDE_FRICTION 4.2f
#define MOVE_SLIDE_FORWARD_FRICTION 3.0f
#define MOVE_SLIDE_STEERING 2.8f
/* How hard the direction of view pulls the slide line, as a length added to
   the unit captured line - so it caps the deviation at atan(0.25), about 14
   degrees, no matter how far the player turns. See player_slide.c. */
#define MOVE_SLIDE_LOOK_STEER 0.25f
#define MOVE_SLIDE_BRAKE 34.0f
#define MOVE_SLIDE_JUMP_BURST 1.25f
#define MOVE_SLIDE_REENTRY_DELAY 0.2f

/* --- grapple --- */
#define MOVE_GRAPPLE_RANGE 24.0f
#define MOVE_GRAPPLE_PULL 28.0f
#define MOVE_GRAPPLE_MAX_SPEED 36.0f
#define MOVE_GRAPPLE_REEL 6.0f
#define MOVE_GRAPPLE_MIN_LENGTH 2.5f
#define MOVE_GRAPPLE_MAX_CORRECTION 1.0f

/* --- vertical traversal --- */
#define MOVE_STEP_UP_HEIGHT 0.45f
#define MOVE_STEP_DOWN_HEIGHT 0.5f
#define MOVE_MANTLE_MIN_RISE 0.35f
#define MOVE_MANTLE_MAX_RISE 2.3f
#define MOVE_MANTLE_REACH 0.35f
#define MOVE_MANTLE_APPROACH 0.4f
/* How far past the lip the mantle deposits the player, on top of the
   PLAYER_RADIUS needed to stand clear of it. arena_support_height holds a
   player whose circle merely overlaps the box, so this is margin rather than a
   requirement, and it is the only part of the scripted move that is a choice -
   the rest is just closing whatever gap the player had left to the wall. Small,
   because a mantle should leave the player standing on the edge they grabbed
   and not somewhere out on the roof. */
#define MOVE_MANTLE_INSET 0.05f
/* A mantle scripts the player's position, so every frame of it is a frame they
   are not driving. That is the whole cost of the move and the reason it is
   short: long enough to read as a climb, short enough that it never feels like
   the game took the controller away. */
#define MOVE_MANTLE_DURATION 0.22f
/* The whole speed a mantle exits at, not a floor on the speed carried in - a
   mantle keeps nothing. See begin_mantle in player_mantle.c.

   Zero, so a mantle ends standing still on the ledge. It also removes a
   discontinuity: the climb scripts position with velocity pinned at zero, so
   any non-zero exit was a step change in velocity at the instant the script
   handed back, and the player was fired forward off a move that had visibly
   just stopped. At zero the whole thing eases to rest and the player leaves
   under their own acceleration. Raise it to push them off the top again. */
#define MOVE_MANTLE_EXIT_SPEED 0.0f
/* How far the exit off the top is bent toward where the player is asking to
   go, rather than straight off the ledge they happened to grab. A mantle that
   always spits the player out perpendicular to the wall is a mantle they have
   to correct for afterwards.

   Inert while MOVE_MANTLE_EXIT_SPEED is 0 - there is no exit velocity to aim.
   Kept because it is the shape of the exit, not its size, and it applies again
   the moment that speed is non-zero. */
#define MOVE_MANTLE_EXIT_STEER 0.5f

/* --- view feedback --- */
/* Continuous speed-to-FOV cannot signal a transition, only a magnitude. These
   are the one-shot kicks that tell the player a state actually changed.

   The landing dip is a spring, so what a landing supplies is a velocity in
   metres per second and not a displacement: at this stiffness and damping the
   view bottoms out at roughly a third of the impulse. A plain jump touches
   down at 9.7 m/s and dips about 7cm; anything past 15 m/s dips about 11cm and
   no further. */
#define VIEW_LAND_DIP_PER_SPEED 0.25f
#define VIEW_LAND_DIP_IMPULSE_MAX 3.8f
/* Underdamped on purpose - critical damping here would be about 27.6, and the
   slight overshoot is what makes a landing read as an impact. */
#define VIEW_DIP_STIFFNESS 190.0f
#define VIEW_DIP_DAMPING 19.0f
#define VIEW_SLIDE_FOV_KICK 7.0f
#define VIEW_WALL_JUMP_FOV_KICK 5.0f
#define VIEW_WALL_JUMP_ROLL_KICK 6.5f
#define VIEW_CLIMB_RISE 0.12f
/* How far the eyes sink while hanging off a ledge - enough to read as holding
   on below the lip, not so far that the ledge fills the screen. */
#define VIEW_LEDGE_HANG_DROP 0.38f
#define VIEW_IMPULSE_DECAY 7.5f
/* A step-up moves the player's feet instantly, which is right - but moving the
   eyes with them turns every kerb and crate into a jolt. The camera keeps the
   height it had and catches up at this rate, so the body steps and the view
   glides. Only a real step is smoothed, never a fall or a landing. */
#define VIEW_STEP_SMOOTH_RATE 14.0f
#define VIEW_STEP_SMOOTH_MAX 0.6f
/* The resting field of view. Speed widens it and the scope narrows it, both
   measured from here. */
#define VIEW_BASE_FOV 75.0f

/* How far up and down the player may look. Effectively all the way: this game
   is played on walls and roofs, so the two directions a player most needs are
   straight up at the lip they are about to grapple and straight down at the
   street they are about to drop into, and both were being taken away from them.

   The old pair was -35 and +70, which is a third-person orbit's range - a
   camera that hangs above and behind a body cannot go under the floor, so it
   was never allowed to. In first person that argument does not apply and what
   is left is a controller that would not let the player look at their own feet.

   Stopping a degree short of vertical rather than at it. At exactly ninety the
   view axis is the world's up axis, cos(pitch) is zero and yaw stops meaning
   anything - the basis stays orthonormal, so nothing breaks, but the screen
   spins about the aim as the mouse moves sideways and every downstream thing
   that measures a heading against the look direction is reading a number that
   has no direction in it. One degree is not a degree anybody can feel, and it
   keeps the pole out of the reachable range entirely rather than making every
   consumer of the look angles handle it.

   Symmetric, because there is no reason for it not to be. Anything that cannot
   follow the view this far clamps gracefully on its own: the body's aim layer
   saturates at the -78 and +84 degrees its authored poses reach, and a grapple
   fired at the floor finds no wall and does nothing. */
#define VIEW_PITCH_MIN (-89.0f)
#define VIEW_PITCH_MAX 89.0f
#endif
