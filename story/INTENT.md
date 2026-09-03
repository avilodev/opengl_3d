# Intent

## What this is

A first-person movement shooter in the Titanfall lineage, written in C with
OpenGL and GLFW.

The thesis: **movement is the primary verb, and shooting is expressed through
movement.** Aim skill matters, but positioning, route choice, and speed matter
more. A player who wall-runs a flank and slide-jumps out of it should beat a
player who stands still and aims better.

## Pillars

1. **Momentum is earned and never silently taken away.**
   Every traversal action either preserves speed or trades it for something the
   player chose. Nothing resets velocity as a side effect.

2. **Traversal chains.** Wall run into wall jump into slide into slide-jump into
   grapple. The interesting skill ceiling is in the transitions, not in any
   single move.

3. **First person, readable at speed.** Camera roll, FOV kick, and view height
   communicate state. The player should feel what the controller is doing
   without reading a HUD.

4. **Combat happens mid-air.** Weapons must be usable while wall-running,
   sliding, and grappling. A weapon that forces the player to stop is a weapon
   that fights the game.

5. **Tunable by numbers, not by rewrites.** Feel lives in named constants that
   can be swept, not scattered through control flow.

## Target loop

Enter an arena → read the geometry for a route → chain traversal to reach an
advantageous angle → engage → break contact by leaving through a route the
opponent cannot follow → repeat.

## Current state

The movement layer is the real work and it substantially exists:

- Ground movement with acceleration, friction, sprint
- Jump with coyote time and input buffering
- Air strafing that preserves momentum
- Wall running: time-limited, tangent-gated, with a wall-to-wall chain bonus and
  a reattach cooldown that forbids re-running the same wall, and a detach rule
  so steering away from a wall leaves it instead of being projected back on.
  The run is an arc -
  attach, rise, apex, sink - driven by wall gravity that ramps over the timer
- Wall climbing: a head-on approach runs up the wall instead, spending the
  into-wall part of the player's speed to buy height, once per wall per airtime
- Wall jump that boosts along current velocity, pushes along the wall normal,
  and steers toward the look direction; it also exits a wall climb
- Slide: entry burst, steering, forward accel, brake, and a slide-jump that
  carries speed
- Landing into a slide when crouch is held at speed. Crouch is otherwise a
  ground-only verb: airborne the key changes no state and no pose
- Grapple: crosshair raycast, a rope that goes taut and swings rather than
  pulling in a straight line, with a reel-in while held
- Standable geometry: every box has a solid top, with step-up onto low ledges,
  ledge drops, and an opt-in mantle onto anything within reach (airborne, and
  the player must ask for it with jump)
- Camera: mouse look, speed-reactive FOV, wall-run roll, crouch view height,
  first/third person toggle, and one-shot view feedback on the transitions -
  a sprung landing dip, a wall-jump roll and FOV punch, a slide-entry FOV kick
- Weapons: two, defined as data in `weapon_specs.c` — an assault rifle, which
  the player carries, and an auto pistol, which exists and is tested but is not
  yet equipped. Hitscan against the shared arena geometry, 30 rounds each, and
  flat damage at every range — there is deliberately no falloff. Accuracy is one
  cone that opens only with sustained fire and recovers within half a second of
  the trigger coming off, and the sight draws that cone at its true angular
  size. Firing kicks the view; the kick is composed into the look angles rather
  than layered over them, so the sight cannot disagree with the bullet, and it
  decays fully so it never becomes a pull to hold against. The rifle trades a
  hundred rounds a minute and three times the kick for a tighter cone. Both
  fire in every movement state, with no gates
- Sights: an EOTech red dot at one power, carried by both weapons — an open
  window with a translucent combiner and a dot projected into it. It does not
  magnify, so aiming buys a clean aim point and a steadier gun rather than a
  closer target, and the room the player is about to move through stays exactly
  as big as it was. The mesh is authored around its own reticle and the spec
  mounts it by measured heights, so the metal, the aim point and the drop that
  aiming applies are all one number. A magnified prism sight is still in the
  code — it draws the world twice, once wide for the periphery and once narrow
  into a stencil mask shaped and placed from its real geometry — but no weapon
  carries it yet
- HUD: a crosshair that opens with the cone, a red dot projected into the
  sight's window when aiming, and an ammo counter
- Combatants: eight bodies standing around the arena, wearing the player's own
  rig off one import. A cylinder hitbox as tall as the drawn body, banded by
  height into legs, chest and head at 0.8, 1.0 and 2.0 times the weapon's flat
  damage. Traced after the geometry, so a body behind a wall is behind the wall.
  They turn to face whoever is shooting and they do not die: the pool drains,
  and three seconds after the last round it comes back. Placed by route rather
  than by spread - one on the plaza floor and every other one behind a
  different traversal verb across Gatehouse, Turbines and Spillway,
  which is what makes "combat happens mid-air" a thing that can be checked
- Hit feedback: the damage flies off as a figure anchored at the point the round
  struck and projected every frame, so it stays on the body rather than sliding
  around near it; rounds inside a fifth of a second on one body merge into one
  climbing figure, because ten a second is ten nobody reads. A hit marker on the
  crosshair, derived from that figure rather than from a timer of its own. An
  impact mark on a body that is redder and larger than the spark concrete
  throws. A flinch that rocks the body away from the round on the rig's lean
  channel, the spine bending with the feet planted. And a pool bar over anything
  not at full health, and over nothing else
- Health, death and respawn: a pool that empties into a state rather than onto a
  floor, a timer beside it, and a body that is skipped by the trace and by the
  renderer for as long as it is down. The player has one of these of their own.
  Where a body comes back is answered by the map's own team slots and never by
  the mode

Supporting systems that exist: a pause menu, a batched UI renderer with TrueType
text, frame-based input, and a shared arena definition used by both rendering
and collision.

## Not built yet

This is the honest gap between the name and the code.

- **PvP session layer.** There is no authoritative match state, remote player
  replication, teams, scoring or network transport yet. The standing bodies are
  calibration mannequins, not planned AI opponents. Spawn selection exists but
  is a distance rule with no notion of sight lines or spawn protection, and
  nothing in the world can damage the player: a debug key stands in for an
  opponent until there is one.
- **A second weapon.** One rifle is enough to prove firing works in every
  movement state. It is not enough to make weapon choice a decision.
- **HUD beyond the gun.** Health and the respawn count are there; there is no
  state readout beyond them, and no kill feed or score, because there is no
  session to keep either in.
- **Finished level content.** Floodgate K-12 is now a designed 6v6 blockout with
  faction entries, three route identities and Varos fiction. It still needs
  twelve-player validation and an art pass.
- **Slopes and ramps.** Collision is a disc against axis-aligned boxes, so every
  surface is flat and every wall is vertical.
- **Wall hanging.** A wall can be run up but not held at the top of the climb.
- **Ziplines and ground slam.**
- **Modern rendering.** The renderer is OpenGL 2.1 fixed-function immediate
  mode: no shaders, no buffers, no materials, no meshes, no shadows. The scope's
  periphery is shaded rather than blurred for want of a framebuffer to blur
  into; depth of field is one of the things shaders would buy.
- **Audio.** A module exists but is not linked or driven.
- **Movement tests.** `make probe` and `make bodyprobe` cover the viewmodel, the
  arm placements, the reload, the dropped magazine, the hitbox and its bands,
  what a round takes off, that a body behind a wall stays behind it, and that a
  world point projects where the view draws it. The CONTROLLER is still
  uncovered, and it is the thing whose feel a tuning change can quietly move.

## Order of work

1. ~~**Player health, death and respawn.**~~ Done. Death is a state on
   `Combatant` and a timer beside it; the player is one of those, filled from
   the controller by `player_combatant.c` the way the rig's payload is filled by
   `player_visual_state.c`. Respawn selection is `spawn_select.c`, which reads
   the team slots Floodgate publishes and never learns a coordinate of its own.
   Checked headless by `make deathprobe`. What it still wants is something that
   shoots back, which is item 4.
2. **Movement regression tests.** Lock the feel in before tuning drifts. The
   controller is a function of state, input, and dt; it does not need a GL
   context to test, and the combat checks in `make probe` are the pattern.
3. **Floodgate K-12 playtest and iteration.** Validate first-contact timing,
   two-way route fairness and spawn safety with twelve humans before artifying
   the blockout.
4. **PvP session and replication.** Start with one authoritative match, two
   six-player teams and replicated movement/combat state. AI is later content,
   not the first source of opposition.
5. **Shaders on the existing 2.1 context.** GLSL 1.20 plus framebuffer objects
   buy per-pixel lighting and speed post-processing without a rewrite. A full
   GL 3.3 core port is a later cleanup, not a prerequisite — at roughly 2,400
   vertices a frame the current renderer is not a bottleneck.

## Non-goals

- Titans, mechs, or a second scale of vehicle combat.
- Campaign, story, or dialogue systems.
- Progression, loadout unlocks, or economy.
- Cross-platform support. The build targets Windows via MinGW-w64.
- Engine generality. This is one game, not a reusable engine.

## Design rules

- **Tuning lives in `include/gameplay/movement_tuning.h`,** weapon tuning in
  `include/gameplay/weapon_tuning.h`, and what it costs to be hit in
  `include/gameplay/combat_tuning.h`. A constant that changes feel belongs in
  one of the three, not inline in a subsystem. The third is separate for the
  same reason the first two are: a headshot multiplier is not a property of the
  rifle - every weapon gets it - and it is not a property of the controller.
- **One movement mechanic per file.** `player_controller.c` owns state and the
  order things run in; each mechanic owns its own rules. Subsystems talk through
  `player_internal.h` and never read raw input.
- **`include/render/renderer3d.h` is the rendering boundary.** Gameplay never
  calls OpenGL. The renderer never mutates gameplay state.
- **Geometry has one source of truth.** `g_arena_walls` is shared by rendering,
  collision, and raycasting so anything visible is solid and anything solid is
  visible.
- **Anything that can be tested without a GL context should be.**

## Definition of a good change

It makes the player faster, or gives them a new route, or makes an existing
route more readable — and it does not slow down or interrupt a player who is
already moving well.
