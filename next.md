# Player health, death and respawn

## What it is

The player can be killed, and comes back. Health is a pool that drains, empties,
and holds the body down for a fixed count before the map is asked where to put
it. Nothing about the mannequins' arithmetic changes; what changes is that the
bottom of a pool is now a place you can arrive at rather than a floor you rest
on.

This is the first item in INTENT's order of work, and it is first because PvP
needs it before AI does. A match is two teams killing each other and being put
back; none of that can be written against bodies that cannot die.

## What already exists

Most of it, which is the point.

`Combatant` is a pool, a shape to be hit in and a place to stand, and its header
already says what death would be: a state on the struct and a timer beside it,
changing nothing that calls in. `combatant_raycast` would skip a dead body;
`combatant_bodies_update` would stop drawing one. Both are one condition each.

`arena_team_spawns` already publishes both six-player sets, and `main.c` already
enters through one of them. What it does not do is CHOOSE - it takes slot zero
because there is nothing yet with an opinion.

`player_visual_state.h` is the pattern for the last piece. It reads a controller
and fills the rig's payload; a body off a wire skips it. Health is the same
shape: something reads a controller and fills a `Combatant`, and a body off a
wire skips that too.

What does not exist is anything that can hurt the player. The mannequins have no
AI and never will; a player's own muzzle starts inside their own cylinder, which
the raycast already refuses. So the loop is built now and driven by a debug key
and a headless probe, and it goes live when the session layer does.

## The work

### 1. Death on the combatant

Two fields - `alive`, and `respawn_timer` beside it. `active` keeps the meaning
it has: a slot that holds a body at all.

The arithmetic of being hit splits in two. `combatant_damage` takes one body, a
region and a figure, pays it in, clamps it to what is left, and takes the body
down at zero. `combatant_apply_damage` is that plus the set's bookkeeping - the
flinch, which needs the hit normal, and the figure thrown into the world, which
needs the set. The player's own body has the first and not the second: a number
flying off your own shoulder is not feedback, it is noise.

Regeneration waits on `alive` as well as on `quiet_time`, or a corpse heals back
to standing without ever getting up.

`combatant_tick` runs one body's flinch, quiet time, regeneration and respawn
timer, and RETURNS whether the timer just ran out. It does not place the body,
because where a body comes back is a map question and this file does not know
the map. `combatant_set_update` is the facing plus a tick per body, and puts its
own mannequins back where they were authored.

### 2. Two conditions elsewhere

`combatant_raycast` skips a body that is not alive - rounds pass through the
dead, which is the rule that stops a corpse from being cover.

`combatant_bodies_update` stops drawing one, and the HUD stops drawing a pool
bar over it. The rig has no death clip, so a dead body is not drawn at all. That
is a gap and it is named as one: it wants a clip and a ragdoll, and neither is
this.

### 3. The map chooses the spawn

`spawn_select.c`, new, small, and no GL: given a team and the live bodies, take
the slot whose nearest live body is furthest away. `arena_team_spawns` stays the
only place a coordinate lives, and the mode never learns one - which is exactly
what INTENT asks for, and it is why this is a file rather than three lines in
`main.c` that would have to move again when the session layer arrives.

The first spawn goes through the same call as every later one. There is no
reason for entering the map to be a different act from re-entering it.

### 4. The player becomes a combatant

`player_combatant.c`, mirroring `player_visual_state.c` exactly: it reads a
`PlayerController` and fills a `Combatant` - where the body is, which way it
faces - and nothing else. Health lives on the combatant. The controller stays
what it is, a function of state, input and time that has never heard of damage.

The player's combatant is its own variable rather than a slot in the set, for
the same reason the player's `CharacterInstance` is: every consumer of the set
draws it or puts a bar over it, and the local player wants neither. One roster
is a PvP-layer question and it is cheaper to answer once than to carve two
exceptions now.

### 5. Coming back needs the controller cleared

`player_controller_place` - a position, a facing, and every mechanic state gone.
Without it a player killed on a wall respawns still wall running, with a slide's
forward vector and a grapple anchored across the map. It is `init` followed by a
pose, which is the honest implementation and also the one that cannot miss a
field the day a seventh mechanic is added.

The weapon is re-initialised on the same beat and handed its targets again. A
fresh magazine is part of coming back.

### 6. The mode

While dead the controller and the weapon are not stepped; the camera still is,
so the view does not lock up. The combatant still ticks, because the timer is
the only thing happening.

`K` pays a flat figure into the player, latched the way every other press is.
It is a debug bind and it is labelled one, and it is what makes the whole loop
something a person can sit down and feel before there is anything in the world
that shoots back.

### 7. The readout

A pool bar and a figure at the bottom left, mirroring the ammo counter at the
bottom right, and a count in the middle of the screen while the player is down.
INTENT lists a fuller state readout as a later thing; health is real now, so
this is the part of it that has to exist and no more.

### 8. The probe

`make deathprobe`, in the pattern `make probe` set - no window, no GL, host
compiler. What it checks is the arithmetic that cannot be seen by looking:

- a figure that empties the pool takes the body down, and one that does not
  leave it standing
- a dead body is invisible to `combatant_raycast`
- a dead body does not regenerate
- the timer fires at the tuned delay and not a step either side
- a respawn restores the whole pool and clears the flinch
- the selector avoids the live bodies, and lands on a slot the map published
- `player_controller_place` leaves nothing running

## What this does not do

- No death animation, no ragdoll, no corpse. The body is simply not drawn.
- No kill feed, no score, no assists. There is no session to keep them in.
- No damage from the world, no fall damage. The only source is the debug bind.
- No spawn protection and no team logic in the selector beyond distance. Both
  are session-layer decisions and both need twelve humans to tune.
