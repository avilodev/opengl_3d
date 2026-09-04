# Titanfall-style Movement Shooter

A first-person movement shooter in C with OpenGL and GLFW. Movement is the
primary verb: wall running, wall jumping, sliding, and grappling chain together
into routes, and combat is meant to happen while moving. See
[INTENT.md](INTENT.md) for the design goals and the roadmap.

The current executable opens full screen at the primary monitor's native
resolution, on a perspective 3D scene with depth testing, lighting, the
Floodgate K-12 6v6 PvP blockout, and a fully playable movement controller with a first-person camera
(third person behind or in front of the player, cycled with V). Escape opens the pause menu, which has an exit.
There are two weapons defined -- an assault rifle and an auto pistol -- and the
player carries the rifle, drawn as a textured M4 carbine mesh with a textured
EOTech holographic sight mounted on its rail, with the circle-and-dot
reticle that goes with it. Both have a crosshair and an ammo
counter.
Eight non-AI calibration mannequins stand around the map on representative
routes and heights. Rounds land
on them, banded by where they hit, and the damage flies off as a figure
anchored where the round struck. They turn to face whoever is shooting, they
regenerate once the shooting stops, and they die when the pool runs out -- and
so does the player, who is one of the same bodies and comes back at a slot the
map chose.

Still missing on the combat side: any opponent that shoots back. Until there is
one, `K` takes health off you so the death and respawn loop can be driven by
hand.

Some renderer-independent modules under `src/` and `include/` are parked for
future integration and are not linked into the build.

## Requirements

- Windows 10 or 11 (64-bit)
- A GPU and driver with OpenGL support
- WSL 2 with Ubuntu
- GNU Make and MinGW-w64 GCC

The repository includes the Windows GLFW headers and static libraries.

## Build and run

From WSL:

```sh
make
./bin/Game.exe
```

You can also run `make.bat` from a Windows command prompt configured with
MinGW Make.

Gameplay controls are semantic actions loaded from `data/keybinds.cfg` at
startup. Edit that file and restart to remap keyboard or mouse actions; missing
or invalid entries retain their defaults. Mouse buttons 1-8 are supported.

## Controls

- Mouse: look/orbit around the player. Pitch runs to one degree short of
  straight up and straight down -- this game is played on walls and roofs, and
  the two directions most worth having are the lip you are about to grapple and
  the street you are about to drop into. The degree that is held back keeps the
  view axis off the world's up axis, where yaw would stop meaning anything.
- W/A/S/D: move relative to the camera
- Left or Right Shift: sprint while moving
- Space: jump
- Wall run: move alongside a wall while airborne; hold Shift for a faster run.
  Steering away from the wall lets go of it
- Wall climb: run head-on into a wall while airborne to run up it; a faster
  approach climbs higher, and a wall can only be climbed once per airtime
- Wall jump: press Space during a wall run or a wall climb; the jump is steered
  toward where you are looking
- Left or Right Ctrl: slide while carrying sprint momentum; crouch otherwise.
  Crouch is a ground-only verb -- pressing it in the air does nothing
- Slide jump: press Space during a fast slide to carry momentum into the jump
- Left Mouse: hold to fire the rifle. It fires in every movement state -- there
  is no state in which the gun refuses to work
- K: development control -- takes 25 health off you. Nothing in the world can
  shoot the player yet, so this is what drives the death and respawn loop by
  hand until the session layer arrives with an opponent
- Right Mouse: hold to aim through the holographic sight. The reticle is not
  painted on the screen: it is a collimated mark on the gun, seen through the
  optic's glass, so it appears as the window reaches the eye rather than the
  instant the button goes down. The sight is one power, so
  aiming costs nothing at all -- no narrower view, no slowdown, and every
  traversal move still works
- R: reload. Pulling the trigger on an empty magazine reloads as well
- Q / E: lean left / right. A peek, not a tilt: the eye slides off the body's
  centreline, and the drawn body bends its spine to follow it, so the head, the
  shoulders and the rifle in its hands come out past the corner while the feet
  stay planted. The camera, weapon, sight and body all read the same eased pose
- F: hold to grapple the surface under the crosshair; the rope goes taut and
  swings, and holding F reels it in
- Mantle: while airborne, push into a ledge to hang off it, and press Space to
  climb up. Hanging occupies the hands: the gun lowers and the sight is refused
  until you let go
- Low ledges are stepped onto automatically while running
- V: cycle the camera -- first person, third person behind, third person in
  front, looking back at the player
- B: jump straight to the view from the front, and back to first person
- Escape: open or close the pause menu
- Enter: resume from the pause menu

The window takes the monitor's own video mode, so it comes up borderless at
the desktop resolution with no mode switch. The pause menu is authored at
720p and scaled with the viewport, so it stays the same apparent size at
1080p, 1440p or 4K rather than shrinking as the resolution rises.

## Weapons

Every number that makes one gun differ from another is a `WeaponSpec` in
`src/gameplay/weapon_specs.c`. The weapon code has no numbers of its own, so
adding a gun is a new entry in that file rather than a new branch anywhere.

| | Auto Pistol | Assault Rifle |
|---|---|---|
| rate | 700 rpm | 600 rpm |
| magazine | 30 | 30 |
| reload | 2.05s | 2.40s |
| cone at rest | 0.18 deg | 0.14 deg |
| cone held down | 1.10 deg | 1.35 deg |
| kick per magazine | 7.8 deg | 25.5 deg |
| sight | holographic, 1x | holographic, 1x |

The rifle is the issued weapon. Everything about it trades rate for weight: it
is a hundred rounds a minute slower, and its kick is more than three times the
pistol's, which is what actually separates them -- the pistol can be held down
and the rifle cannot. The kick decays fully back to the player's own aim, so it
bounces rather than climbing, and the answer to it is bursts rather than a
memorised pull-down. In exchange its cone at rest is tighter than the pistol's
and the sight cuts that in half again -- without magnifying, so what the sight
buys is a steadier gun rather than a closer target.

The auto pistol is still in the code and still passes every test; it is simply
not handed to the player yet.

Damage is flat at every distance for both. There is deliberately no falloff
curve, because a movement shooter should reward taking an angle rather than tell
the player the angle they earned was the wrong one.

Accuracy is a single cone whose half-angle is drawn by the sight, so what the
sight covers is what the gun can hit. It opens only with sustained fire. Moving
does not open it, and neither does range, because a weapon that punished either
would fight the game.

Right mouse raises the sight. The gun slides to the centre of the screen and
drops by exactly the height of its own reticle, so the dot lands on the middle
of the screen, and the cone tightens. Aiming costs nothing else: no slowdown,
and every traversal move still works.

Both weapons carry the same sight, an EOTech EXPS2 at one power. It does not
magnify -- the world through the glass is exactly the size it is around it --
so what aiming buys is a clean aim point and a steadier gun rather than a
closer target. For a game whose fights happen in the air off a wall, keeping
the room while you shoot into it is the better trade. Aiming does still cancel
the field-of-view widening that speed applies, so the view settles as the sight
comes up.

The sight lines up with the eye by construction rather than by tuning, and it is
its own piece of data rather than part of a gun. A `SightSpec` in
`src/gameplay/sights.c` holds what belongs to the optic -- how far its reticle
stands above its own rail seat, how wide its glass is, what it magnifies, how
fast it comes up -- and a weapon holds only `rail_height` and `rail_forward`,
which is where its rail is. Neither half knows about the other, so any sight
fits any gun and the same optic can sit at a different height on each.

The mount height is the sum of the two, and `weapon_spec_sight_height` is the
only place it is taken. The mesh is authored around its own reticle:
`tools/scope_to_glb.py` puts the origin on the centre of the window, on the
window's plane, and prints how far that sits above the mount's rail seat.
`tools/fbx_to_glb.py` prints how far the rifle's rail sits above its bore. Their
sum -- 0.0319 + 0.0374 = 0.0693m -- is what the gun drops by when it comes up,
and the optic is drawn about exactly that point. Adding those two by hand into a
single field, which is what this used to be, welded the sight's half into the
gun's: mounting the same optic on a second gun meant re-deriving the figure with
nothing to check the arithmetic against.

The check that the height is right is the front iron post, which lands a hair
under the dot: that is a co-witness, and it only happens if both measurements
are honest. The rear backup sight is folded away at import, because at 60mm
from the eye a raised rear leaf is not a thin blade in the sight picture, it is
a wall across the bottom half of it -- which is exactly why a real rifle with a
sight at that height has it folded.

`rail_forward` is the one number that is a choice rather than a measurement:
it is where along the rail the sight is clamped. At 0.12 the mount seat lands
between z -0.143 and -0.053, which is receiver rail all the way.

The window is a real translucent surface, not a hole: the importer keeps the
combiner as its own material at 0.16 alpha and the static-model renderer draws
translucent materials in a second pass, blended, with depth writes off. It also
does a second job. Stencilled, that same pane is the aperture the reticle is
seen through.

The reticle is the EXPS2's own, and it is a holographic one rather than a red
dot's: a circle with a small dot centred in it, the coarse mark and the fine mark
together. The circle is drawn dimmer than the dot on purpose, because at equal
brightness the bigger shape wins the eye and a precise sight would read as a wide
one.

Its size is exaggerated, at about twice true, and the circle-to-dot ratio is
compressed from the real 68:1 to roughly 5:1. Drawn honestly at the world's 75
degree field of view, a 68-minute circle is 4.6 pixels of radius in 1.2 pixels of
line at 720p and the one-minute dot is a fifth of a pixel: correct and invisible.
The mark this replaced was already exaggerated the same way -- 19 minutes for a
dot that is nominally one -- so the question was never whether to exaggerate but
whether to do it to the whole shape or to part of it. What survives the
compression is a coarse circle around a fine dot, which is the part that makes
the sight recognisable.

It is collimated, the way a holographic sight's actually is: fixed in angle to
the sight body, projected far enough away that moving the sight does not move the
mark. So it is drawn in the viewmodel pass, under the gun's own
transform, and it goes wherever the gun goes -- including wherever the gun's
physics take it later. Masked to the glass, it can only be seen where the window
really is: coming up from the hip the window is off to the side and there is no
reticle at all, and as the gun converges the glass sweeps across and the mark
emerges from behind the hood. Nothing fades it in. It used to be the HUD's,
painted at the centre of the screen, where it switched on the instant the aim
button went down and hung in front of a sight that was still at the hip.

Recoil is the gun's, not the camera's, for the same reason. A collimated reticle
points where the gun points while the bullet leaves the eye, and those agree
only if one pair of numbers drives both -- so `gun_pitch_degrees` and
`gun_yaw_degrees` on the weapon carry the kick, the shot is composed from them,
and the reticle is drawn along them. A burst walks the muzzle up and walks the
reticle up with it. Kicking the camera instead left the gun free to rotate
underneath a reticle that knew nothing about it.

The magnified prism sight is still in the code, drawn from boxes, with the
stencil pass that gives it its own field of view inside its own circle. Its
chevron stays in the HUD, because it is painted against a magnified second pass
of the world rather than against the viewmodel. No weapon carries it now; it is
what `models/attachments/sight_mid` is waiting for.

Rounds are traced from the eye, so the sight is always honest, but the visible
streak leaves the barrel of the gun on screen -- off to the right at the hip,
dead centre when aiming. The renderer and the weapon read one shared viewmodel
pose so those two can never drift apart, and that pose carries the recoil as
well as the carry: the gun is drawn through the kick, so a point taken off it
has to travel through the kick too, or the streak leaves the barrel's unkicked
position while the barrel is a centimetre above it and climbing.

The gun on screen is not always the viewmodel. In third person the viewmodel is
not drawn at all, and a streak from its barrel came out of thin air half a metre
beside the player's head, swinging around them as they turned. So the frame
publishes where the body's own rifle has its muzzle -- the same transform that
rifle is drawn through, out of `character_transform.c` -- and the weapon starts
the streak there instead. `make bodyprobe` checks it lands a barrel's length
from the hand holding it.

Firing takes the gun out of the sprint carry, which aiming has always done. The
carry pulls the weapon in against the chest and rolls it a quarter turn over,
and a player holding the trigger at a sprint was watching rounds leave a rifle
plainly pointed somewhere else. It comes back out faster than it went in
(`WEAPON_VIEW_SPRINT_RAISE_TIME`), because by then the round is already
leaving.

The rifle itself is a textured M4 carbine mesh, in the player's hands in first
person and in the body's right hand in third, where the body holds it in an
aiming stance rather than letting its arms swing (see below). The optic is a
second mesh bolted onto its rail rather than part of it, because that is what
it is -- one sight any rail can carry -- and because it is also what the HUD
paints a reticle into. Both renderers draw it, so the gun does not grow an
optic when the camera moves. The rifle is 0.838m from muzzle to buttplate and
the sight 0.0965m front to back; the spec's muzzle and sight heights are
measured off them, so the flash, the tracer's origin and the dot all sit where
the gun actually is. The pistol has no mesh yet and keeps the boxes it was
drawn from; so does the rifle if its file will not load, and so does any sight
with no mesh of its own.

Movement includes buffered jumps, coyote time, momentum-preserving air strafing,
wall-to-wall chaining, speed-reactive FOV, and wall-run camera roll. Speed above
the sprint cap is kept and bled off slowly, so a chain of slides, wall jumps and
mantles compounds instead of resetting.

Air acceleration is capped along the requested direction rather than at the
ground speed, so holding a key in the air does nothing but turning the mouse
against your velocity compounds into speed. A wall run is an arc: you attach
with a climb, apex about two thirds of the way through the timer, and are
already sinking when it drops you.

Every box in the arena is solid on all sides and standable on top, so the level
is traversed vertically as well as horizontally.

## The reload

A keyframe track rather than code (`gameplay/reload_sequence.h`, with the keys
themselves in `weapon_spec.c`). Everything else the viewmodel does is a blend --
hip to sprint to aim, poses crossfaded on eased numbers -- and that works
because those are things the player can be halfway into. A magazine change is
not: the hand is on the handguard, then on the magazine, then empty, then at the
chest, then back, and each of those is a place rather than a proportion.

Two things decide whether it reads as a person doing it, and neither is where
the hand goes.

**How the hand gets to each key.** Every segment used to run at a constant rate,
so every motion had zero acceleration at both ends and then changed direction
instantly at the key. That is a machine following waypoints. Each key now states
the shape of the motion into it, and which shape it is says what the motion *is*
-- a magazine ripped out of a well leaves fast and slows as it comes clear
(`SNAP`), and one driven into a well builds up and arrives hard (`DRIVE`). Those
are opposite curves, and running both at a constant rate was most of why the
whole thing looked generated. Shaped inside `reload_sequence_sample`, so the
hand, the shoulder solved behind it and the magazine held in it cannot end up at
three different points along the same segment.

**The pacing, which is not even.** Against the rifle's 2.40s: the hand comes off
the handguard onto the magazine in 0.34s, presses the release in 0.12s, and rips
the magazine clear in 0.12s. Crossing to the pouch and back with a fresh one is
0.94s. Offering it to the well, rocking it straight and driving it home is
0.19s, and then 0.31s of coming off the floorplate, slapping it, and holding
against it. So the spent magazine is out of the gun in under a quarter of a
second and the fresh one takes three times as long to go in and be confirmed:
coming out is a release and a rip, which is fast because nothing has to be
aligned, and going in is a fit, which is slow because something does.

**The tap** is the part with no gameplay in it, and it is the part that sells it.
A magazine that arrives at the well and is simply there was seated by nobody. A
hand that comes off the floorplate and slaps it back is a person making sure.
The gun takes a small knock on both the seat and the tap -- up, back toward the
eye, and a touch of muzzle, because a knock along one axis is a slide -- with a
beat between them, which is what makes the second read as a confirmation of the
first rather than as part of the same push. The knock is on the viewmodel *pose*
rather than layered on by the renderer, so the muzzle, the optic's glass and the
hands all take it; a jolt that moved the mesh alone would put the tracer's origin
off the barrel for as long as it lasted.

The magazine also rocks fore and aft about its own mouth, which is where a real
one pivots. Front lip into the well first, bottom rocked back toward the shooter,
and straightened as it seats -- without it a curved rifle magazine goes in like a
card into a slot.

**Where the magazine comes from.** The two halves of a reload want opposite
answers about being on screen, and the pouch used to be given the well's answer.
It sat 14cm below the eye, which is not a chest -- it is under the chin, and
eighteen centimetres *above* the shoulder line the viewmodel's arms hang from.
The hand went backward toward the receiver at almost constant height and came
back, so the magazine read as coming off the base of the gun; the pouch was 28cm
from the magazine well and 3cm above it. It was also on the wrong side, at +0.10,
which had the support hand crossing the chest toward the side the gun is held on.

The well has to be visible, because that is where the interesting part happens
and a reload conducted below the bottom edge is a two-second pause with a word
printed over it. The pouch must not be: a support hand dropping out of frame and
coming back up holding a magazine is what a reload looks like from behind a
rifle. So the pouch is now at belt height, slightly left of centre, and the grab
happens off screen.

That needed the route fixing, not just the destination. The support shoulder
solves to 36cm below the eye and 28cm forward, and the well and the belt both sit
at 0.68 of the arm's reach from it -- **105 degrees apart around it**. A straight
line between two points that far apart on a sphere cuts the chord, and the middle
of that chord is 0.41 of reach: an arm folded double, halfway through the move,
every reload. So the hand is routed through two waypoints, which are the slerp
between the well and the pouch at a third and two thirds. What comes out is not
what a guess put there -- the hand drops almost straight *down* in front of the
chest, staying out at 60cm, and only comes in toward the belt at the bottom.
Swinging it out to the side traded the fold on the way down for a worse one
coming back up (0.02 of reach, an arm at its own shoulder).

The waypoints are passed *through* rather than stopped at -- a distinct ease from
a dwell, because easing in and out at each would put two stops in the middle of
one sweep. And coming clear of the pouch is a fraction of the way back along the
arc rather than a lift: straight up moves the hand toward the shoulder it hangs
from, and the solver answered a hand 9cm above the belt by blading the torso 60
degrees to get out of its way, which put a 60-degree swing of the whole stance in
the middle of an otherwise still sequence.

The whole track now holds 0.65 of reach or better at every one of 200 samples,
against 0.60 before any of this.

Section 13 of `make probe` covers the pacing (that getting one in takes over half
again as long as getting one out), that the hand actually comes off the
floorplate and drives back into it, that the gun is knocked exactly twice and not
at all at rest, that the well is in frame and the pouch is below the shoulder
line and out of it, and that the hand never jumps -- the largest step it takes
between two of two thousand samples, which is what catches an ease that failed to
reach its key and would leave the hand short of it and then teleport. Section 6
prints the solved shoulder placement and the on-screen height of every key, which
is what made the fold diagnosable at all.

## Combatants and damage

Eight bodies stand around the arena and can be shot. They are training dummies
and they say so: the pool is large enough to soak a sustained burst, and it
comes back on its own three seconds after the last round lands, so a player can
keep working a route and reading their hits without waiting on anything.

They do die. Emptying the pool takes a body down, the trace stops resolving
against it and the renderer stops drawing it, and a timer stands it back up
where the arena authored it. That is one state and one timer on `Combatant`,
shared by every body in the game including the player's -- see **Health, death
and respawn** below.

They are placed by route, not by spread: on the Gatehouse floor and pump roofs,
inside the Turbines wall-jump route, on a cross-link roof, and inside Spillway.
A ring of targets on flat ground would test aim and nothing this game is about.

**The hitbox is a cylinder**, standing on the body's feet, as tall as the drawn
body -- it takes `CHARACTER_RIG_HEIGHT` rather than restating it, so a rescaled
rig cannot leave the box the player shoots at a different size from the body
they see. A cylinder and not one of the arena's axis-aligned boxes because a
body turns and a box does not: a box is wider across its diagonal than across
its faces, so a target would be quietly easier to hit from one bearing than
another, and these bodies turn while they are being shot at. `make probe`
checks exactly that, from sixty-four bearings.

Three bands by height -- legs below 0.47 of it, head above 0.86 -- paying 0.8,
1.0 and 2.0 times the weapon's flat damage. The spread is narrow at the bottom
and wide at the top on purpose: a player who hits the body they aimed at is
never sorry, and the head is worth crossing a room for.

**Bodies are traced last**, against the distance the arena's geometry has
already cut the ray to. Reversed, a round would pass through a tower to reach a
dummy on the far side of it -- which is not something anyone would notice by
playing, except as targets dying through walls.

**The figure is anchored in the world**, at the point the round struck, and
projected to the screen every frame through the same view transform the world is
drawn with. So it stays on the shoulder it came off while the player wall-runs
past, instead of being a piece of HUD that slides around near the target. The
head band is the only one that changes its colour: a player shown three colours
learns three colours, and a player shown one learns the thing that matters.

**Rounds inside a fifth of a second on the same body merge into one figure.**
The rifle fires ten rounds a second, and one figure per round is ten a second on
one target, of which the player reads none. Accumulated, the number climbs and
what they read is the total -- which is what they wanted to know. Each addition
re-pops the figure, so a merged number still reads as a stream of hits rather
than as one.

Three other pieces of feedback fall out of the same event. An impact mark on the
target is bigger, redder and dimmer than the spark concrete throws, and it is
the only confirmation that appears where the player is already looking. A hit
marker flashes on the crosshair -- derived from the newest figure rather than
from a timer of its own, so a round landing and a marker appearing cannot
disagree. And a body that has been hit rocks away from the round, on the rig's
lean channel: the spine bends and the feet stay planted, which is the shape of a
flinch and the only hit reaction a rig with no hit clip can give.

A small pool bar appears over anything that is not at full health, and is not
drawn otherwise -- which makes it a readout of a fight rather than a permanent
label on every body in the arena. The player's own pool is the exception: it is
drawn at full health too, in the corner opposite the ammo counter, because a
player who cannot find their own health bar has no way to tell missing from
full.

### Health, death and respawn

Death is one state on `Combatant` and one timer beside it, and it is the same
state for every body in the game. Emptying a pool takes the body down, clears
its flinch and starts the count. While it is down, `combatant_raycast` skips it
-- rounds pass through the dead, so a corpse is never cover the player cannot
see -- and `combatant_renderer` stops drawing it. The rig has no death clip and
nothing to fall with, so the honest choice between a corpse standing at
attention and no corpse is no corpse.

`combatant_tick` runs one body's clock -- flinch, quiet time, regeneration, and
the count while it is down -- and *reports* that the count ran out rather than
standing the body up itself. Where a body comes back is a map question and
`combatant.c` does not know the map. Bodies in the set come back where the arena
authored them, which is the only rule a calibration mannequin needs.

**The player is one of these bodies.** Their combatant is a variable of the
mode's, not a slot in the set -- the way their `CharacterInstance` is their own
-- and `player_combatant.c` fills it from the controller, position and facing
only, the exact counterpart of what `player_visual_state.c` does for the rig.
Health never touches the controller, which stays a function of state, input and
time with no opinion about being shot. A player arriving over a wire would skip
that file entirely, which is the same promise the visual state makes.

Coming back goes through `spawn_select.c`, and so does entering the map for the
first time -- there is no reason for those to be different acts. It takes a team
and the live bodies and returns the slot whose nearest live body is furthest
away, out of the set `arena_team_spawns` publishes; no coordinate is ever named
outside the map. `player_controller_place` then clears every mechanic state, so
a player killed on a wall does not come back still wall running with a grapple
anchored across the map, and the weapon is re-initialised for a fresh magazine.

`make deathprobe` checks all of it without a window: that a pool empties into a
state and not past it, that a dead body is gone from the trace and does not
regenerate, that the count is the length it was tuned to and fires exactly once,
that the selector lands on one of the map's own slots and picks the one with the
furthest nearest body, and that placing the player leaves nothing running.

Every body wears the player's own rig. The library loads it once, so a second
combatant costs a `CharacterInstance` and not a second eight-megabyte import; a
body past 150m or behind the camera is not skinned, though its clip clock keeps
running so nothing pops when it comes back into view.

## The map

Floodgate K-12 is a 150m-square 6v6 blockout in the Varos Corridor. Bastion
enters from an armored railhead in the north and Axiom from a medical filtration
annex in the south. The layout is rotationally fair, with three distinct routes:

| Zone | Where | What it is for |
| --- | --- | --- |
| Gatehouse | centre | Compact gunfights around pump housings and an exposed high block |
| Turbines | west | Close fights and five-metre wall-jump shafts |
| Spillway | east | Slides, fast lateral wall chains and grapple pylons |

It is laid out from the movement constants rather than from arbitrary shapes.
The number that governs its wall routes is how far apart two facing walls have
to be for a wall-jump chain between them to gain height:

| Gap between facing walls | What it is |
| --- | --- |
| 4-6m | Climbs 1.2-1.9m per cycle at any speed -- a shaft you go up |
| 7-8m | Climbs only above about 13 m/s -- a skill-gated way up |
| 9-10m | Holds height or bleeds it slowly -- a lateral chain |
| 12m+ | Descends -- cross it by grapple or by rooftop jump |

The full fiction, encounter flow, timings, callouts, art direction and playtest
checklist are in `story/maps/FLOODGATE_K12.md`.

## Build

```sh
make          # build bin/Game.exe
make build    # create a distributable build/ folder with Game.exe and assets
make clean    # remove obj/ and the binary
make probe    # headless: the viewmodel, the arms, the reload, what a round costs
make bodyprobe   # headless: the character rig's own arithmetic
make deathprobe  # headless: health, death, respawn and spawn selection
make run      # build and launch
```

Run the packaged game from inside `build/` so its relative asset paths resolve:

```sh
cd build
./Game.exe
```

The Makefile compiles every `.c` under the directories listed in `SRC_DIRS`.
Adding a source file to an existing module needs no Makefile change. Modules
under `src/` that are not in `SRC_DIRS` are parked and deliberately excluded.

## Active code

Application shell:

- `src/main.c` owns the window and application loop.

Player movement, one file per mechanic, coordinated by the controller:

- `include/gameplay/player_controller.h` groups ground, slide, wall-run,
  wall-climb, grapple, and mantle state under their owning sub-structures;
  shared kinematics and pose remain on the controller.
- `src/input/actions.c` maps configurable keyboard and mouse bindings into
  semantic actions once per frame.
- `src/input/input.c` exposes generic keyboard and mouse-button states with
  held, pressed, and released edges, plus cursor and scroll data.
- `src/input/commands.c` decodes those actions into focused player, camera, and
  weapon commands so gameplay systems receive only the decisions they own.
- `src/gameplay/player_controller.c` owns player state, decodes semantic actions
  for movement, and fixes the order the subsystems run in.
- `src/gameplay/player_locomotion.c` ground/air acceleration, friction, jumping,
  horizontal integration, and what the player is standing on.
- `src/gameplay/player_wallrun.c` finding and holding a runnable wall.
- `src/gameplay/player_wallclimb.c` running up a wall taken head-on.
- `src/gameplay/player_slide.c` slide entry, steering, and exit.
- `src/gameplay/player_grapple.c` the rope constraint and swing.
- `src/gameplay/player_mantle.c` ledge detection and the scripted climb.
- `include/gameplay/player_internal.h` is the contract between them.
- `include/gameplay/movement_tuning.h` is the only place feel is tuned.

World and camera:

- `src/gameplay/arena.c` the level's boxes, shared by rendering, collision, and
  raycasting.
- `src/gameplay/traversal.c` reusable wall raycasts used by the grapple.
- `src/gameplay/reload_sequence.c` the magazine change as a keyframe track: the
  sampling, the ease shapes that decide how the hand arrives at each key, and
  the two knocks the gun takes when a magazine is seated and tapped home. The
  keys themselves are data in `weapon_spec.c`.
- `src/gameplay/third_person_camera.c` mouse-look camera state, and the one
  place a world point is turned into a screen point -- the camera owns the
  orbit, the lean and the projection, so anything that has to put a mark on a
  thing in the world rather than on the screen comes here for it.

Combat:

- `src/gameplay/combatant.c` the bodies that can be shot: the spawn table, the
  cylinder hitbox and its bands, the facing, what a round takes off, and the one
  state and one timer that are death and respawn for every body in the game.
  Placed as two coordinates and a facing, with `arena_support_height` finding
  whatever is under each, so a body on a roof stands on the roof and no entry
  restates a height the arena already owns.
- `src/gameplay/player_combatant.c` fills a combatant from the player's
  controller, the exact counterpart of `player_visual_state.c` -- which is what
  keeps health off the controller entirely.
- `src/gameplay/spawn_select.c` which of a team's deployment slots to come back
  at, taken from `arena_team_spawns` and the live bodies. The mode never learns
  a coordinate, which is the seam the PvP session layer is meant to arrive
  through.
- `src/gameplay/damage_number.c` the figure that flies off a body, and the
  merging that keeps a burst to one of them. Knows nothing about a combatant --
  it compares an owner tag it never interprets, which is what lets the same pool
  serve melee or a blast later.
- `src/gameplay/combatant_visual_state.c` fills the rig's payload from a
  combatant, the exact counterpart of `player_visual_state.c`.
- `include/gameplay/combat_tuning.h` the third tuning header. A headshot
  multiplier is not a property of the rifle and not a property of the
  controller, so it lives with neither.
- `src/render/combatant_renderer.c` one `CharacterInstance` and one payload per
  combatant -- `main.c`'s player body pair, in the plural -- and the distance
  cull that decides which of them are worth skinning.

Rendering:

- `src/render/renderer3d.c` scene composition and OpenGL state.
- `src/render/primitives.c` the only place raw cube geometry is emitted.
- `src/render/character_renderer.c` a character's rig: proportions, colours, and
  the drawing of one -- GL state, the box fallback, and the per-body update that
  advances a clip and skins a mesh.
- `src/render/character_transform.c` where a body and the rifle in its hand are,
  as matrices: the position, the heading, the tilts a slide or a wall run put on
  the whole of it, and the grip the gun hangs off. No GL, because the answers
  are wanted by things with no business linking a renderer -- a tracer needs to
  know where that rifle's muzzle is. The lean is not here either: a peek is a
  bend of the spine, and bones are character_animation's. The model is shared and
  loaded once; everything per body lives in the caller's `CharacterInstance`.
- `src/render/character_animation.c` which clip the body plays, the cross-fade
  between clips, and the aim layer over the top. Reads movement state, never
  writes it.
- `include/render/character_state.h` the whole of what the rig reads about a
  body: position, velocity, look direction, a dozen flags and the one-shot
  event bits. The local player's is filled from their controller by
  `src/gameplay/player_visual_state.c`; nothing else about a body reaches the
  rig, which is what lets a body driven by a packet use the same code.
- `src/render/renderer.c` the batched UI renderer (2D quads and text).
- `src/render/hud.c` the combat overlay: the crosshair whose gap is the weapon's
  real cone, the ammo counter, the player's own pool and respawn count, and
  everything the player is told about what they just hit -- the figures, the hit
  marker and the pool bars. Half of it is anchored in the world and drawn on the
  screen, which is why it takes the player as well as the camera.
- `src/states/pause_menu.c` the pause overlay and its actions.
- `src/input/input.c` frame-based keyboard and mouse input.
- `include/utils/math_utils.h` shared scalar helpers.
- `include/utils/mat4.h` matrices and quaternions, for skinning.

The character is a rigged glTF, loaded and animated in four files split so the
first three need no GL context and can be exercised headlessly:

- `src/render/glb.c` the container half of a binary glTF reader: bounds-checked
  accessor spans, and nothing about what they mean.
- `src/render/skinned_model_glb.c` the meaning laid over it: which nodes are
  bones, which vertices they move, what the clips do to them over time.
- `src/render/skinned_pose.c` sampling a clip, blending two of them, and
  skinning the mesh on the CPU - which is what a fixed-function renderer
  leaves available, there being no vertex shader to hand joint matrices to.
  Skinning reads the model and writes a `SkinnedInstance`, so the model stays
  an asset two bodies can share and only the vertices are per body.
- `src/render/skinned_model.c` the draw, and the only part of the four that
  touches GL.

The guns are static props, and read through the same container:

- `src/render/static_model_glb.c` triangles, a material colour and a base
  colour map, with no skeleton and no per-frame work. Runs headlessly, like
  the three loaders above it.
- `src/render/static_model.c` the draw and the texture upload, which are the
  parts that need a context. Mip levels are built by hand because GL 1.1 has
  no `glGenerateMipmap`.
- `src/render/stb_image.c` the PNG decoder, compiled from the vendored
  `stb_image.h` with only PNG and only the from-memory entry points.
- `src/render/weapon_model.c` owns the gun meshes, because the viewmodel and
  the third-person body draw the same gun and loading it twice would be two
  copies of a four megabyte texture.

The third-person body holds the rifle through an aim layer: an upper-body pose
laid over whatever the legs are doing, so no locomotion clip has to know the
rifle exists. `Pistol_Aim_Down`, `_Neutral` and `_Up` in the library are not
animations but an aim offset -- three static poses whose muzzle pitch runs from
-78 to +84 degrees, near enough linearly in the blend between them -- and the
body picks a point along them from where the camera is looking.

The layer owns `spine_02` and everything descended from it, which is the torso,
both arms and the head, and leaves the pelvis and the legs to the clip below.
That boundary is why crouching composes for free: the aim poses rotate nothing
at or below `spine_01`, and a crouch is almost entirely pelvis and legs, so the
two never touch the same bone. Crouching and standing up need no cross-fade on
the upper body either, because from the layer's point of view nothing happened.

Two deliberate compromises. Off the sight the body follows the camera's pitch
at about seven tenths rather than exactly: the poses at the ends of the trio are
a lowered weapon and one held overhead, and driving the body all the way into
them reads as a stoop and a salute however right the muzzle angle is. On the
sight it tracks one to one, because that is what the sight is for -- a scoped
rifle pointing twenty degrees under the crosshair is the one case where the
flattery becomes something the player can see, since their own shots leave where
the camera says rather than where the body does. And off the sight the gun is
lowered by blending the *arms* toward the lowered pose rather than the whole
upper body, because that pose lowers the gun by folding the torso forward, which
bows the head into a stoop. Raising the sight brings the rifle up, on the
weapon's own aim timing.

### Facing

The body stands where the player is pointed. Not where they are shooting, not
where the sight is -- where they are pointed, sight down and trigger untouched,
because where someone is facing is not a detail of aiming. It is the single thing
everyone else reads them by, and a body that only turned to its camera once a
trigger was involved would spend most of a fight pointing somewhere nobody was
looking. So the chest, the arms, the head and the rifle they hold turn to the
camera, and whatever the feet are doing happens underneath that.

A slide, a mantle, a hang, a climb and a wall run take the heading back, and the
handover is eased over about a seventh of a second rather than switched. A
heading is a transform and not a pose, so the clip cross-fade that covers a
change of clip cannot cover this one: thrown in a single frame it is a body
snapping round.

Underneath is where the work is, because the library has no strafe and no
backpedal -- forty-three clips in each of two files and every locomotion loop in
them runs forwards. So the legs are turned rather than animated: the pelvis is
rotated about the model's vertical to put the legs on the movement, and the
three spine joints give the same rotation back to leave the chest on the aim.
Rotations about a common axis commute, so this is exact rather than close --
the gun points where the crosshair is to the degree, whatever the legs are
doing, and the aim layer above never learns that anything moved. The turn is
spread over three vertebrae rather than dumped on one because a waist that
turns eighty degrees at a single joint folds.

Two bounds keep it a body. The legs never sit more than eighty degrees round,
so a full sideways strafe holds the clamp and slides a foot by a hand's width
over a stride, which nobody sees, rather than wringing the torso out. And past
a hundred and twelve and a half degrees -- halfway between a strafe and a
backpedal, which is the one place in the range no key lands on, since a keyboard
can only ask for multiples of forty-five -- the legs come back to the near side
and the walk plays *backwards* instead. That is what a backpedal is: legs square
under a chest facing the crosshair, and the cycle in reverse carrying the body
away from it.

### The head look

The same mechanism one layer up: the neck and the head turn to the camera over
whatever the body is doing, with the chest leading a quarter of the way into it.
It is measured off the chest rather than off the world, which is what makes the
two layers compose with no arbitration between them -- wherever the body has
already gone to the aim, this is zero and costs nothing, and through a handover
the head covers exactly the part of the turn the body has not made yet.

What it is for is the wall run, which keeps its own heading and takes the aim
layer anyway. The body holds the wall, and the head is then the only thing left
to say where the player is actually looking -- and where they are looking is
where they are about to shoot from a wall. The clips that own the whole body
refuse the look along with the layer, because they are authored down to the
neck.

The clamp is eighty-five degrees, about a person's limit, and it saturates rather
than refusing: a body running away from where it is looking turns its head as far
as it goes and leaves it there, which is what someone does. Dead astern it keeps
whichever side it is already on, because there the short way round is a coin toss
and a heading wobbling a degree either side of it would otherwise swing the head
through the front every time it wobbled.

Standing still, the chest goes to the camera and the feet do not: they hold what
they were left with, so a mouse flick winds up the waist and the feet follow it
late, which is what a standing player does. Past about fifty degrees they give in
and take a whole step -- held until they are square again, so a player who keeps
turning gets steps rather than a permanent lag, and a twist too small ever to
have tripped one unwinds lazily instead. That is the difference between a body
relaxing and a body stepping, and with no turn-in-place clip in the library it is
the honest version of both.

A wall run refuses the yaw. The legs are on the wall -- that read is the clip
and the fourteen degrees of tilt on the transform together -- and a body turned
off the wall to face a crosshair is a body running on air. The upper body still
pitches and the head still looks; only the body's yaw is refused. The hang, the
slide, the mantle and the climb refuse both the facing and the look, for the same
reason they refuse the aim layer: those clips are authored for the whole body.

Hanging off a ledge gets a pose of its own, and it is authored rather than
animated. Of the eighty-six clips exactly one puts both hands above the head -
`Pistol_Aim_Up`, a weapon held overhead - and every other candidate,
`ClimbUp_1m` included, keeps them at or below it. So the hang is built by
pointing thirty-one bones along directions chosen by eye, on top of that clip
for its raised shoulders: left hand up on the lip with the fingers over the
edge and the thumb opposing them, right arm down with the rifle hanging
muzzle-first, body straight, knees a little bent. The fingers are in the table
because the base clip curls them round a pistol grip, which is what made the
first attempt look like it was holding a rifle in mid air.

The collarbones are deliberately *not* in it. A bone's direction is the one
thing pointing it controls, and a collarbone's job is not to point anywhere -
it is to hold the shoulder joint against the chest the mesh is weighted to.
Pointing it moves that joint, and the arm tears off the body.

Its awkward half is geometric rather than artistic. The player's feet stay
where they were when they grabbed, a ledge qualifies from a third of a metre to
two and a third above them, and the body is two and a half metres tall - so the
lip is always below head height and a body drawn at the player's own feet
stands in the air beside the ledge rather than hanging from it. The drawn body
therefore sinks until its hand can meet the lip. Only the drawing moves; the
collision capsule stays where the simulation put it. The drop is capped, which
covers every ledge from about a metre up - and a player rising toward one grabs
it the frame it comes into range, which is the top of the band.

The drop alone only fixes the height, and a ledge is also at whatever distance
the reach allowed, so the gripping arm is solved rather than posed: two bones
and the law of cosines put the wrist on the lip, with a pole vector deciding
which way the elbow breaks. The ledge search already knew where the lip was and
now reports it. That is also what buys the pose some life - the hand is solved
onto the ledge every frame, so the body can shift its weight without the grip
sliding, which a fixed pose could not do. The body hangs a little above a
straight arm so the reach has a bend to spend; at full extension it both locks
stiff and comes up short whenever the ledge sits a touch further out.

A slide, a mantle and a wall climb switch the aim layer off and let the clip
have the whole body -- but only while the sight is down. A slide leans the
pelvis back seventy degrees and a mantle has both hands on the ledge, and those
are poses worth having; raising the sight takes the upper body straight back,
because where the rifle points outranks any of them. A ledge hang is the one
that never yields, and it never has to: it is holding the body's whole weight
on its arms, and the weapon refuses the sight for it outright.

What makes that affordable is that the layer no longer inherits the hips. The
aim poses are authored relative to an upright pelvis, and a pelvis is never
still -- a stride swings it, a crouch tucks it, a slide throws it seventy
degrees back -- so every degree of that used to travel up into a chest that was
supposed to be holding a sight picture. Measured on this rig, that put the
rifle fourteen degrees off the crosshair at a jog and twenty-two down at a
crouch walk. The chest is now pinned to the orientation the aim pose was
authored with, by however far the sight is up, so an aimed rifle points at the
crosshair in every state and a carried one keeps all of the stride's own
motion. `make bodyprobe` prints the figure for each state.

The aim poses were authored for a pistol, which is the closest the library has
to a rifle: both hands meet at the grip, so the support hand lands on the
magwell rather than out on the handguard.

Mesh, skeleton and clips come from `models/characters/`. The two libraries
there share a skeleton: `UAL1_Standard.glb` supplies the body and forty-three
clips, and `UAL2_Standard.glb` is opened only for its own forty-three, matched
onto our bones by name. The `_RM` files beside them are the same animations
with root motion baked in, which is why they are not the ones loaded - the
controller owns where the body goes.

### Where assets live

Three buckets, by what the engine does with a thing rather than by what kind of
thing it is:

```
models/weapons/<name>/       m4_carbine, katana, ...   held weapons
models/attachments/<name>/   sight_close, ...          bolted onto a weapon
models/characters/                                     rigs and clip libraries
```

Flat inside each bucket, and deliberately not split into `Guns/` and `Swords/`.
That hierarchy would duplicate `WeaponClass`, which `weapon_spec.h` already
states, and would make the taxonomy an argument to have again for every asset
that does not fit cleanly on one side of it. A weapon folder holds its source
FBX as `source.fbx`, its maps in `textures/`, and the `.glb` the engine reads.

A weapon folder also holds two small JSON files, and they are different things:

- `import.json` — how to bake the `.glb`: source, scale, axes, which map goes
  on which material. Hand-written, read only by the importer.
- `measured.json` — what the importer measured off the finished mesh: the rail
  height above the bore, the depth of the scabbard, where the muzzle ends up.
  Machine-written, overwritten every import, never edited. It exists because a
  number a machine computed and a human retyped is a number that can be retyped
  wrong, and nothing downstream can tell.

### Tuning a weapon's placement

Finding where a hand closes on a new mesh is twenty rounds of nudge, rebuild,
restart, walk back. `make tune` builds `bin/GameTuning.exe`, which reads a
`placement.json` beside any asset and lets you skip the rebuild:

```sh
make tune
# edit models/weapons/katana/placement.json, then press F5 in game
# press F6 to print the result back as C, paste it into weapon_specs.c,
# and delete the placement.json
```

A file states only the fields it is changing — a whole pose or just its `roll`.
The fields are listed in `include/gameplay/weapon_placement_fields.h`, which is
also what drives the reader, the writer and `make tuneprobe`, so the four
cannot drift.

**The C is still the real thing.** `placement.json` is scaffolding and is meant
to come down: a release build has none of this compiled in, opens no file, and
cannot fail to parse one. The reason the numbers do not simply live in JSON is
that the comments around them in `weapon_specs.c` are most of what says why a
number is what it is, and JSON has nowhere to put them.

The rifle comes from `models/weapons/m4_carbine/`, and `tools/fbx_to_glb.py` is what puts
it in a form the engine reads. Nothing in the toolchain imports FBX, so that
script is the import step: it parses the binary FBX, triangulates it, bakes the
node transforms, rotates and scales the result into the viewmodel's own axes and
into metres, moves the origin onto the bore line, folds the artist's ambient
occlusion into the base colour, and writes a self-contained `.glb`. It is
checked in rather than run once and forgotten, because the numbers it bakes are
the ones `weapon_specs.c` is tuned against -- it prints them when it runs. The
rig in the source file, six bones for the bolt and trigger and magazine, is
dropped: nothing animates them yet.

```sh
python3 tools/fbx_to_glb.py    # needs Pillow, for the textures only
```

Parked modules include persistence, data parsing, keybinds, audio, textures,
timing, and player stats.


The current compatibility renderer is intentionally a small first milestone.
It can be replaced behind this boundary with a shader-based renderer using
modern buffers, mesh assets, and materials.
