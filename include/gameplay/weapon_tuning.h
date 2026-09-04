#ifndef WEAPON_TUNING_H
#define WEAPON_TUNING_H

/* What every weapon shares. Anything that differs between one gun and the next
   is a field on WeaponSpec instead - see weapon_spec.h and weapon_specs.c.

   The accuracy model those specs describe is deliberately one-sided: a bullet
   does the same damage at 200 metres as it does at two, and where it goes is
   decided only by how fast the player is pulling the trigger. Range costs
   nothing and movement costs nothing. A weapon that punished moving would
   fight the game - pillar 4 is that combat happens mid-air - so the only thing
   that opens a cone is sustained fire, which is a choice the player makes and
   can stop making. */

/* How fast the view settles after a kick. A property of the camera rather than
   of any one gun, which is why it is not on the spec: two weapons firing into
   the same view should not settle at different rates. */
#define WEAPON_RECOIL_DECAY 9.0f

/* --- effects --- */
/* A tracer is a streak, not a bullet: it is drawn for the whole flight path on
   the frame the shot resolves and then fades. */
#define WEAPON_TRACER_LIFETIME 0.06f
#define WEAPON_IMPACT_LIFETIME 0.55f
#define WEAPON_IMPACT_SIZE 0.11f
#define WEAPON_MUZZLE_FLASH_TIME 0.045f

/* --- the prism sight's housing ---
   Shape, not feel: how fat the box fallback's body is when an optic has no
   mesh. How WIDE its window is is not here - that is the optic's own glass,
   and it lives on the SightSpec, because it is what a reticle is seen through
   and what a magnified pass is masked to. Kept here it meant a prism sight's
   radius decided the size of a holographic sight's window. */
#define WEAPON_ACOG_WALL 0.020f
#define WEAPON_ACOG_DEPTH 0.20f

/* --- the viewmodel ---
   How the gun rides in the hands, which is the renderer's business and the
   same for everything the player can hold. */
/* The shove back along the barrel as a round leaves. There is no rotation to
   go with it any more: a rotation moves where the gun points, and where the
   gun points is the bullet's business now, so it is WEAPON_RECOIL_DECAY and
   the spec's recoil_pitch that own it. */
#define WEAPON_VIEW_KICK_BACK 0.045f
#define WEAPON_VIEW_KICK_DECAY 11.0f
#define WEAPON_VIEW_BOB 0.012f
/* Ground speed at which the bob reaches full amplitude. */
#define WEAPON_VIEW_BOB_SPEED 6.0f
#define WEAPON_VIEW_SWAY 0.055f
/* Camera-space drop that takes the viewmodel off the bottom of the screen, and
   how long it takes to get there and back. Sized against view_down in the
   specs, which is about a tenth of this. */
#define WEAPON_VIEW_STOW_DROP 0.55f
#define WEAPON_VIEW_STOW_TIME 0.18f

/* How far the gun is canted toward the player at the hip, in degrees, unwound
   to nothing as the sight comes up. Cosmetic: it moves the mesh and the points
   taken off it, and never the round. It sat as a literal in the renderer's
   transform, which meant the tracer origin had no way of knowing about it. */
#define WEAPON_VIEW_HIP_CANT (-3.0f)

/* --- the sprint carry ---
   The gun brought in and down against the body, kept UPRIGHT - top rail to the
   sky - and laid roughly level across the view with a little cant to the right.
   That is how somebody actually runs carrying a rifle, and it is a different
   thing from lowering the gun: what says "sprint" here is that the weapon has
   come in close and off the aim line, not that it has been rolled over.

   One pose for everything the player can hold, reached through
   weapon_spec_sprint_pose so a gun can state its own without any caller
   noticing.

   Stated as WHERE THE GUN IS HELD and how it is angled, never as where its
   origin lands. The two are not interchangeable. A ViewmodelPose turns about the
   model's origin, which the spec puts on the bore at the BACK of the receiver,
   so every degree of angle swings the M4's 0.838m about its own back end and
   walks the weapon across the frame - enough of it and the gun leaves the
   screen entirely, whatever the offsets say. Anchored at the grip the hand stays
   put and the gun swings about it, so the three angles below can be set for how
   the carry should LOOK and the framing follows instead of fighting.

   It also means the shared carry already adapts: the anchor is a hand position,
   the gun's own grip is on its spec, and a shorter weapon hangs off the same
   hand without a number changing. */
/* Where the firing hand sits, camera space, forward and down stated positive -
   in toward the middle, below the eye, and closer than the hip's 0.38.

   DOWN is the number the framing is most sensitive to, and the one worth
   measuring rather than eyeing. It carries the gun and both hands together, and
   what it trades against is how much hand is left on screen: the SUPPORT grip
   sits at screen y -0.94 at 0.13 and -1.02 at 0.15, so past about 0.14 the grip
   itself is below the bottom edge and what remains visible is the back of the
   hand and the wrist reaching up to it. That still reads as held - the hand mesh
   stands well above the grip point - but there is no margin left, and by 0.17
   the receiver above the bore is in view with nothing holding it.

   It is also coupled to the PITCH, which swings the grips down as it steepens,
   so the two want re-checking together rather than one at a time. Ask
   tools/viewmodel_probe.c rather than guessing; that is what it is for. */
#define WEAPON_VIEW_SPRINT_HAND_RIGHT 0.14f
#define WEAPON_VIEW_SPRINT_HAND_DOWN 0.15f
#define WEAPON_VIEW_SPRINT_HAND_FORWARD 0.34f
/* YAW is the one that does the work, and it is the largest of the three. It
   swings the muzzle across the body, which is the whole read: at 40 degrees the
   bore spans screen x -0.75 to +0.68, so the barrel crosses the middle of the
   view and the weapon is unmistakably carried rather than merely lowered. Under
   about 25 it stops crossing and sits off to the right instead; past about 50 it
   lies flat across the entire screen and reads as the gun being presented to the
   camera. Positive swings the muzzle left, which is across the body of a
   right-handed shooter.

   PITCH stays small. The carry is level across the view, and a muzzle swung down
   at the ground reads as a lowered gun, which is the opposite of what this is
   for. It is also the axis the framing is most sensitive to - a few degrees more
   and the support hand drops off the bottom of the screen.

   ROLL is deliberately the SMALLEST. It turns the gun about its own bore, and
   past about twenty degrees the top rail stops facing the sky and the weapon
   starts reading as carried on its side - a different pose, and not this one.
   Negative tips the top to the right: the roll axis runs along the bore at -z,
   so a negative angle carries the gun's up vector toward +x. */
#define WEAPON_VIEW_SPRINT_PITCH (-20.0f)
#define WEAPON_VIEW_SPRINT_YAW 40.0f
#define WEAPON_VIEW_SPRINT_ROLL (-20.0f)
#define WEAPON_VIEW_SPRINT_TIME 0.22f
/* And how fast it comes back OUT when the player shoots. Quicker than it went
   in, because it is now late: the round is already leaving, and every frame the
   gun spends on its way up is a frame of rounds coming out of a rifle that is
   still rolled over against the chest. Aiming gets the same urgency for free -
   weapon_sprint_blend multiplies the carry out by the sight as it rises - and
   this is the equivalent for a trigger. */
#define WEAPON_VIEW_SPRINT_RAISE_TIME 0.08f
/* --- the reload ---
   Where the gun is held while a magazine is changed: brought up and in toward
   the chest, rolled so the magazine well faces the support hand, and turned a
   little across the body so the player can see what their hands are doing.
   Absolute camera space and degrees, like the sprint carry, and anchored at the
   FIRING grip for the same reason - the hand that stays on the weapon through
   the whole sequence is the one worth pinning the pose to.

   ROLL is the number that makes this readable, and its SIGN is the whole of it.
   Rolling turns the gun about its own bore, so the barrel keeps pointing where
   the pitch and yaw put it and only the gun's faces change - which face is
   toward the eye is entirely this number. The magazine well is on the
   underside, so the gun has to roll the way that brings the underside round to
   the player: negative. At -50 the well sits 0.63m from the eye and the optic
   0.67m behind it, so the magazine is the nearer face and the change happens
   where it can be watched. Positive rolls the same amount the other way, shows
   the player the top rail, and hides the entire magazine change behind the
   receiver - which is what this did at first.

   Set against the MAGAZINE and not against the gun, because the magazine is
   what the player is being shown - and set high enough that there is screen
   left BELOW it for the spent one to fall through. A well sitting at -0.64 is
   on screen and still wrong: the magazine drops out of frame in the first
   fifteenth of a second and the change reads as the gun swallowing it. At these numbers the well lands at screen
   (+0.43,-0.45) with the floorplate at -0.61, comfortably inside the frame; the first pose tried put it at
   -1.08, with the entire magazine change happening below the bottom edge, which
   is a two second pause with a word printed over it - what this already was.

   FORWARD is the number that keeps the arms off the screen, and it is why this
   is half a metre out rather than the hip's 0.38. The hands have to come up
   almost to eye level to bring the weapon into view, and the closer they are the
   further forward of the eye the shoulders must sit to reach them at a bend -
   at 0.37 the upper arms filled the bottom right of the frame. Pushed out, the
   shoulders fall back behind the camera where they belong and the arms read as
   arms. If the pose moves, check the arms and not just the gun. */
#define WEAPON_VIEW_RELOAD_HAND_RIGHT 0.14f
#define WEAPON_VIEW_RELOAD_HAND_DOWN 0.08f
#define WEAPON_VIEW_RELOAD_HAND_FORWARD 0.50f
#define WEAPON_VIEW_RELOAD_PITCH 4.0f
#define WEAPON_VIEW_RELOAD_YAW 22.0f
#define WEAPON_VIEW_RELOAD_ROLL (-50.0f)

/* The knock the gun takes when a magazine is seated and again when it is tapped
   home. Small, and it has to be: this is the weapon being shoved by a hand, not
   fired, and at anything above a centimetre it stops reading as an impact and
   starts reading as the gun being thrown around.

   Up, back toward the eye, and a touch of muzzle. All three, because a knock
   along one axis is a slide - what says something was struck is that the whole
   thing moves and turns at once. The same magnitude for both moments: the beat
   between them is what makes the second read as a confirmation of the first,
   and sizing them differently is a refinement nobody can judge without
   watching it. */
/* Where the spare magazines are carried, in camera space: right of the eye,
   below it, forward of it. The one point in a reload that belongs to the BODY
   rather than to the gun, which is why it is stated here in the camera's own
   axes and why the reload track marks those keys ACTION_IN_VIEW.

   Below the eye is the number that matters and it was wrong. It used to be
   0.14, which is not a chest - it is under the chin, eighteen centimetres ABOVE
   the shoulder line the arms are hung from (ARMS_SHOULDER_DOWN). The hand went
   backward toward the receiver at almost constant height and came back, so the
   magazine read as coming off the base of the gun, because that is very nearly
   where it came from: the pouch sat 28cm from the magazine well and 3cm above
   it.

   It was put there to keep the hand on screen, and that argument belongs to the
   magazine WELL and not to the pouch. The well has to be visible - that is
   where the interesting part happens, and a reload whose whole business is
   below the bottom edge is a two second pause with a word printed over it. The
   pouch is the opposite: a support hand dropping out of frame and coming back
   up holding a magazine is exactly what a reload looks like from behind a
   rifle, and it is what every one of them looks like in life. So this is set
   from where a chest rig actually sits and from what leaves the arm at a
   comfortable bend, and the grab happens off the bottom of the screen.

   Slightly LEFT of centre, which is the other half of the same mistake. The
   support hand is the left one - see ARMS_FACING_DEGREES - and its shoulder is
   on the screen's left. A pouch at +0.10 had that hand crossing the chest
   toward the side the gun is held on, which is both the wrong way round for
   anybody's kit and the direction that makes it look like it is reaching for
   the weapon. */
#define WEAPON_VIEW_POUCH_RIGHT (-0.100f)
#define WEAPON_VIEW_POUCH_DOWN 0.750f
#define WEAPON_VIEW_POUCH_FORWARD 0.300f
/* How much deeper the hand goes to get hold of one, and how far back up the arc
   it has come once it is clear of the pouch carrying one - a FRACTION of the way
   to the first waypoint, not a height.

   A fraction because straight up is the wrong direction. Lifting the hand
   vertically out of the pouch takes it toward the shoulder it hangs from, and
   the solver answered a hand 9cm above the belt by blading the torso 60 degrees
   to get the shoulder out of its way - which put a 60-degree swing of the whole
   stance in the middle of an otherwise still sequence and folded the arm to
   0.52 on the way through it. Coming out ALONG the arc keeps the radius, so the
   shoulder never has to move.

   Without it the magazine appears at the same height it was gripped at, and one
   that never comes out of the pouch was never in one. */
#define WEAPON_VIEW_POUCH_REACH 0.045f
#define WEAPON_VIEW_POUCH_CLEAR 0.45f
/* The way round, as two points the hand is routed THROUGH.

   Not a stylistic choice - it is the arc, and it has to be. The support
   shoulder solves to about 36cm below the eye and 28cm forward of it, and both
   the magazine well and the pouch sit at 0.68 of the arm's reach from there.
   They are 105 degrees apart around it. A straight line between two points that
   far apart on a sphere cuts the chord, and the middle of that chord is 0.41 of
   reach: an arm folded double, halfway through the move, every single reload.
   That is the failure this codebase has been bitten by three times, because a
   folded arm throws its elbow across the sight picture.

   Split into three hops of 35 degrees each, the deepest point of any chord is
   0.66 of reach. These two are the slerp between the well and the pouch at a
   third and two thirds, and what comes out is not what a guess would have put
   there: the hand drops almost straight DOWN in front of the chest, staying out
   at 60cm, and only comes in toward the belt at the bottom. Swinging it out to
   the side - which is what a guess did put there - trades the fold on the way
   down for a worse one on the way back up.

   Stated as absolute points rather than as offsets from the pouch because that
   is what they are: derived from where the shoulder actually solves, not from
   where the kit is worn. Move the pouch far and these want recomputing, and
   section 6 of `make probe` is what says so - it walks the whole track at two
   hundred samples and reports the worst bend anywhere on it. */
#define WEAPON_VIEW_POUCH_VIA1_RIGHT 0.017f
#define WEAPON_VIEW_POUCH_VIA1_DOWN 0.419f
#define WEAPON_VIEW_POUCH_VIA1_FORWARD 0.605f
#define WEAPON_VIEW_POUCH_VIA2_RIGHT (-0.002f)
#define WEAPON_VIEW_POUCH_VIA2_DOWN 0.634f
#define WEAPON_VIEW_POUCH_VIA2_FORWARD 0.491f

#define WEAPON_VIEW_SEAT_JOLT_UP 0.009f
#define WEAPON_VIEW_SEAT_JOLT_BACK 0.006f
#define WEAPON_VIEW_SEAT_JOLT_PITCH 1.3f

/* --- the spent magazine ---
   What happens to it after the hand lets go. It leaves with a little of the
   player's own motion and a push down and away from the weapon, falls under
   gravity, and then lies where it lands for REST_TIME before it is taken away.

   The rest is the point. A magazine that vanished the moment it left frame
   would make the reload a viewmodel trick; one still lying on the floor when
   the player turns round says the change happened in the world. Two seconds is
   long enough to walk a pace and look back at it, and short enough that a
   firefight does not end up carpeted in them. */
#define WEAPON_MAG_DROP_DOWN 0.6f
#define WEAPON_MAG_DROP_AWAY 0.35f
#define WEAPON_MAG_GRAVITY 11.0f
#define WEAPON_MAG_SPIN 220.0f
#define WEAPON_MAG_TILT 140.0f
#define WEAPON_MAG_REST_TIME 2.0f
/* A cap, so a magazine dropped off a ledge with nothing under it is eventually
   collected rather than falling forever. */
#define WEAPON_MAG_FALL_LIMIT 4.0f
/* Half the magazine's depth: how far above the floor its origin sits when it
   comes to rest, so it lies ON the ground rather than half through it. */
#define WEAPON_MAG_REST_HEIGHT 0.045f

/* --- the gait ---
   What replaces the footfall bob at full sprint, and it is not one wave. A run
   has two rates in it: the body rolls and swings from side to side once per
   STRIDE - one full left-right cycle per pair of steps - while it rises and
   falls once per FOOTFALL, which is twice as often. Driving the lateral and the
   vertical off the same clock at those two rates traces a figure of eight, and
   that shape is the difference between a weapon that is being carried by
   somebody running and one sliding side to side on a rail.

   All four are stated against walk_time, which the bob already runs on, so the
   crossfade out of walking keeps its phase and there is no step in the middle
   of the handover.

   SWAY and HEAVE are metres, ROCK and NOD degrees. The two angles are what stop
   it reading as the gun being slid around in front of a still camera: a real
   carry rocks about its own bore as the shoulders alternate, and nods as the
   weight lands. They are small on purpose - a few degrees at a third of a metre
   is a lot of screen travel at the muzzle. */
#define WEAPON_VIEW_SPRINT_SWAY 0.030f
#define WEAPON_VIEW_SPRINT_HEAVE 0.017f
#define WEAPON_VIEW_SPRINT_ROCK 3.5f
#define WEAPON_VIEW_SPRINT_NOD 1.6f

/* How long one swing owns the weapon.

   Seconds and not the clip's own duration, because the clip's duration is
   whatever the library's animator felt like and the window fractions on
   MeleeAttack are stated against this. Short enough that a chain of three
   reads as a chain rather than as three separate decisions.

   The block instead runs until the player lets go, so its "duration" is only
   long enough that the clock never expires underneath a held button. */
#define MELEE_ATTACK_SECONDS 0.55f
#define MELEE_BLOCK_HOLD 3600.0f

/* And how long the settle after an unchained swing runs. Shorter than the
   swing: a recovery is the arm coming back, not another decision, and a long
   one reads as the weapon being taken away from the player. */
#define MELEE_RECOVER_SECONDS 0.30f

/* How long a drawn sword waits before putting itself away. Long enough to
   cross a room between two fights without it sheathing in the player's hands,
   short enough that walking away from a fight ends it. Counted from the end of
   the last attack, so a chain does not race it. */
#define MELEE_AUTO_SHEATHE_SECONDS 10.0f

/* Where in that wait the hands come down to MELEE_CARRY_REST, as a fraction of
   it. A fraction and not a second, so the two cannot be retuned apart: the
   hands dropping AFTER the sword had gone away would be a stance nobody ever
   sees, and there is no version of this where the order is a matter of taste.
   Both come off melee_quiet_time, which is what stops them being two clocks. */
#define MELEE_REST_AFTER 0.65f

/* How fast a carry blend crosses to the stance the player has asked for, as an
   exponential rate: a stance is about 97% arrived after 0.35s and half arrived
   after 0.07s.

   Fast enough that stepping onto a wall does not leave the blade hanging in
   the middle of the frame through the part of the run where seeing matters,
   slow enough to read as the hands moving rather than as the sword changing
   place. It is one number for all three stances because a hand is as quick
   going one way as the other. */
#define MELEE_CARRY_RESPONSE 10.0f

#endif
