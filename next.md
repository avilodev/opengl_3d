# The neck, and a sword you can see

## What it is

The camera is the eye. It has been since `viewmodel_clip_arms_eye` landed, and
that was right. What it is not is the eye of a body: everything the player
holds is drawn under `glLoadIdentity()` in camera space, welded to the eye, so
pitch moves the world and moves nothing else. Look up, look down, look
straight ahead - the gun sits in the same place on the screen at all three.

A real eye is mounted on a head, the head pivots on a neck, and the arms hang
off a torso that stayed where it was. Look up and the thing in your hands
leaves your view. Look down and you see your own hands. That relative motion is
most of what makes a first-person view read as a body rather than as a camera
with a gun taped to it, and this codebase currently has none of it.

Two pieces of work, in order. The neck is first because it is self-contained,
it serves both weapon kinds, and it is arithmetic a probe can check. The
katana's carries are second because they depend on nothing in the neck but are
a great deal more work.

## What already exists

More than the gap suggests.

The line between what may lie and what may not is already drawn, and drawn in
the right place. `viewmodel_pose.h` states it outright: the carry is cosmetic,
it moves the mesh and anything that sits on the mesh, and it must never reach
the bullet or the crosshair. The kick is not cosmetic and lives on the other
side. The neck is a carry. It goes on the side that is allowed to lie, and
nothing about where a round lands changes.

The crosshair is already safe by construction. `hud.c` draws it where the GUN
is pointing, off `weapon_aim_angles` - the aim, not the drawn carry. A rotation
applied to the drawn viewmodel does not move it.

`weapon->ads` is already the eased 0-to-1 the whole viewmodel blends on, and
`ViewmodelBlend` already carries it into the arms. Fading the neck out as the
sights come up needs no new state.

And there is a warning already in the tree. `VIEWMODEL_LEAN_ROLL` is one
body-relative rotation of the viewmodel, and it is applied by hand in three
separate places: the GL draw in `weapon_renderer.c:832`, the stencil's optic
projection in `renderer3d.c:274`, and the tracer origin in `weapon.c:699`. Each
one recomposes the same angle from the same input. The neck is the same kind of
rotation with the same three consumers, and adding it the same way would make
six copies of an arithmetic that has to agree or the scope window drifts off
the glass it is supposed to be cut into.

## What does not exist

No first-person sword hold. Not one that needs tuning - none at all.

Every candidate hold clip in both rigs, sampled through the eye anchor, hand
in camera space where `+z` is behind the eye:

    Sword_Idle            +0.23  -0.53  +0.65
    Sword_Block           +0.29  -0.71  +0.35
    Sword_Dash            +0.29  -0.72  +0.34
    Sword_Regular_Combo   +0.29  -0.71  +0.35
    Sword_Attack          +0.22  -0.53  +0.65
    Idle_Shield_Loop      +0.21  -0.72  +0.43

Fifty-three to seventy-two centimetres below the eye and thirty-three to
sixty-five behind it, every one. These are third-person idles in which the arm
hangs down and back at the hip, and they are perfectly good ones - seen from
behind a body. Seen from inside its skull there is nothing in frame.

This matters because it rules out the cheap answer. A stance system that picks
between existing clips returns "invisible" for every stance it can pick,
including the one that most wants to be visible. The visible guard has to be
built, not selected.

## The work

### 1. The neck

A new module, `render/viewmodel_neck`, owning one rotation and the three ways
it has to be applied.

**The angle.** The neck supplies the first part of a look and the spine
supplies the rest. Model it as a saturation rather than a clamp, because a
clamp puts a corner in the middle of a mouse movement:

    head = HEAD_PITCH_LIMIT * tanhf(pitch / HEAD_PITCH_LIMIT)

`HEAD_PITCH_LIMIT` starts at 45 degrees, which is roughly what a neck gives
before the back joins in.

Near zero this is the identity - small looks are all neck, which is true. At
the pitch limits it approaches `HEAD_PITCH_LIMIT` and stops, which is also
true: past about forty-five degrees you are not craning your head further, you
are leaning your whole back, and a leaning back carries the arms with it. The
saturation is what stops the viewmodel rotating eighty-nine degrees out of the
world at the pitch clamp and never coming back.

The angle applied to the viewmodel is the negative of that, because the torso
did not move: relative to a camera that pitched up, the body pitched down.

    neck = -head * follow * (1 - ads)

`follow` is per weapon and lives on `WeaponSpec`. It is a scalar and not a
flag, deliberately. This game has a grapple and a wallrun and the player aims
upward constantly, which is precisely when the neck takes the gun out of frame;
that is honest and it is also a cost, and the cost is not the same for a katana
as for a carbine. A rifle at `0.3` keeps most of the feel and little of the
loss. A rifle at `0.0` is exactly what ships today. Tuning it is one number in
`weapon_specs.c` and not a revert.

The `(1 - ads)` term is what makes the sights work. At full aim the neck is
zero, the gun is welded to the eye as it is now, and the optic lands on the
crosshair at every pitch. This is also anatomically the truth: bringing a gun
up to aim is bringing the eye, the head and the gun into one line.

**The pivot.** The atlanto-occipital joint - where the skull actually turns -
sits behind and below the eye. Measured rather than invented: it is the head
bone's own origin, and `viewmodel_clip_arms_eye` already computes the step from
that bone out to the eye. Take that step once at rig load, negate it, and that
is the pivot. Stating it as a constant instead would work and would be the
third number in this file whose job is to stand in for anatomy; the two before
it both got it wrong by tens of centimetres.

**The three applications.** One module, three entry points, one angle:

  - `viewmodel_neck_push_gl` / `pop` - translate to pivot, rotate, translate
    back, around the existing viewmodel draw and around the arms draw. Both,
    with the same angle. A rotation given to the gun and not the hands is how a
    hilt ends up floating in front of a fist reaching for where it used to be,
    which is the failure `view_clip_offset`'s comment already names.
  - `viewmodel_neck_transform_point` - for the stencil's optic centre and the
    tracer origin, which need the drawn position and cannot read a GL matrix.
  - the lean folds in here too. It is the same shape of rotation with the same
    three consumers and it is currently copied three times; making the neck a
    fourth, fifth and sixth copy alongside it is how the scope window comes off
    the glass. The module composes both and the three call sites each lose their
    hand-rolled `cosf`/`sinf` pair.

The tracer takes the necked muzzle rather than the aim muzzle. The round still
leaves the eye along `weapon_aim_angles` and is untouched; what the streak has
to do is start at the muzzle the player can see.

**Checks.** All of it is arithmetic, so `tools/viewmodel_probe.c` can assert it
without a window:

  - at pitch 0 the gun origin is inside the frame; at +60 with `follow` 1 it is
    outside; at -60 it is inside and higher on the screen than at 0.
  - at `ads` 1 the sight axis lands on the crosshair at pitch -80, 0 and +80.
    This is the one that matters. If it fails, aiming is broken.
  - the point transform and the GL matrix agree, the way
    `check_pose_matches_gl` already checks for the pose. Six copies of a
    rotation is the failure this file exists to catch.
  - the angle saturates: pitch 89 and pitch 60 differ by less than a degree of
    neck.

Files: new `include/render/viewmodel_neck.h` and `src/render/viewmodel_neck.c`;
`weapon_renderer.c`, `renderer3d.c` and `weapon.c` lose their lean arithmetic
and gain the module; `weapon_spec.h` and `weapon_specs.c` gain `neck_follow`;
`viewmodel_probe.c` and the Makefile gain the checks.

### 2. The katana's carries

The katana becomes a weapon with both halves, which is the thing this section
is really about.

`viewmodel_arms.c` and `viewmodel_clip_arms.c` are already siblings that answer
one question two ways: a gun is *placed* where its spec says and the arms are
solved to reach its grips; a sword is *animated* and the weapon goes wherever
the hand went. The katana has only ever had the second. It needs the first as
well - not instead. Swings stay clips, because a swing is a drawing nobody is
going to reproduce with a solver. Holds become solved carries, because a hold
is a number somebody tunes and the library has none worth selecting.

So: the katana gains grip points on its spec, the way a rifle states its two,
and a set of `ViewmodelPose` carries reached through the existing IK -

  - **low rest.** Sword down at the side. Invisible, and invisible *because the
    hands are at the hip*, which is the distinction that makes it a stance and
    not a bug.
  - **guard.** Hands around navel height, forward, blade rising through the
    lower frame. This is the one you see.
  - **momentum.** The wallrun and slide angle, blade canted across.

Which carry is worn is a function of player state, and visibility falls out of
it rather than being aimed at. `weapon_melee_clip` grows a sibling that returns
a carry instead of a clip name, and the blend between a carry and a swing is
`viewmodel_pose_lerp`, which already exists and already interpolates the
rotations.

Open, and deliberately not decided here:

  - whether the third-person body wears the same stances. Today one clip name
    serves both views specifically so they cannot drift; a solved first-person
    carry has no third-person counterpart, and either the body keeps
    `Sword_Idle` and the two views genuinely differ, or the body needs stance
    clips too. The first is cheaper and probably fine - nobody sees both at
    once.
  - what drives the transition. A timer since the last swing, a combat flag, or
    the traversal state directly.

These want their own pass once the neck is in and the sword can be looked at
honestly. Nothing in section 1 depends on the answer.

## Order

The neck ships first and alone. It is testable headless, it changes both
weapons, and `neck_follow` at `0.0` is a complete escape hatch if it turns out
to fight the traversal more than it is worth. The carries follow, against a
camera that by then already moves like a head.

## What shipped

Both, in that order. Probe sections 14, 15 and 16 are the arithmetic; what is
below is only what the plan left open or got wrong.

**The neck.** `render/viewmodel_neck` owns the angle, the measured pivot and
the lean, and hands out a matrix rather than pushing GL - the tracer's origin
is computed in `gameplay/weapon.c`, which the melee probe links without a
renderer, so a `glPushMatrix` in that module would be an undefined symbol in a
headless build. One matrix, multiplied onto the stack by the one call site that
draws and applied to a point by the two that cannot, which settles the
agreement question by construction rather than by check.

The pivot measures out 9cm below the eye and 10cm behind it. The saturation
does not do what this file claimed: at a 45 degree limit, pitch 60 and pitch 89
differ by 4.2 degrees of head, not by under one. The property that is true and
checked instead is that the same 29 degrees of look is worth 12.3 degrees of
head down at the bottom of the range and 4.2 at the top - a third as much,
which is a saturation, and it never arrives at the limit, which is what says
there is no corner.

The gun's ORIGIN leaves the frame at a 60 degree look up even at `0.30`, and
that reads worse than it is: the origin is the back of the receiver and the
muzzle is still on screen. The rifle tips out of frame from the stock end,
which is what a gun in the hands of a body looking up does. Both are printed.

**The carries.** The two open questions, decided:

  - the third-person body keeps `Sword_Idle`. The cheaper answer, as this file
    guessed - nobody sees both views at once.
  - the transition is `melee_quiet_time` and the traversal states directly, and
    no new flag. The drop to a low rest comes off the same clock that sheathes
    the sword, at `MELEE_REST_AFTER` of it, so the hands come down and the
    scabbard takes it out of one disengagement rather than out of two timers
    that could disagree.

The stance is carried as three weights rather than a from/to pair, because a
wall run can start while the guard is still coming up and a pair has to snap
its `from` to answer that. Measured: 9.5mm of movement on the interrupted
frame against the 50.8mm a pair would have jumped.

A katana turned out to need its own hold. `WEAPON_HOLD_HILT` is two fists in a
row on one shaft with no pistol grip for either, which means both hands are
grips walked back to wrists - on a rifle only the firing hand is, because the
support hand's point is an authored wrist already.

**Still open.** The seam between a solved carry and a clip swing is a switch,
not a blend. The two paths do not only pose the arms differently, they anchor
them differently - a clip is carried onto the eye it was sampled at and a solve
hangs off a searched shoulder placement - so there is no one pose to
interpolate. What closes the gap is that a recovery ends near the guard, and
that is a thing to look at rather than to reason about.

One thing this file did not name and the work turned up: the first-person
scabbard was drawn INSIDE the sword's own transform, so the belt swung with the
blade and travelled with it through a draw - every key of that track aiming at
a mouth that was moving with the hand. `tools/sheath_probe.c` has always
modelled it as a fixed point on the body and checked the track against that. It
is on the body now.
