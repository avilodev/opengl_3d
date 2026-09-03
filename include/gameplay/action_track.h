#ifndef ACTION_TRACK_H
#define ACTION_TRACK_H

#include "gameplay/viewmodel_pose.h"

/* A scripted viewmodel action, as a keyframe track rather than as code.

   Everything else the viewmodel does is a BLEND: hip to sprint to aim, three
   poses crossfaded on eased numbers, and that works because those are states
   the player moves between and can be in halfway. An action is not a state, it
   is a sequence - for a reload the hand is on the handguard, then on the
   magazine, then empty, then at the chest, then back; for a draw it is at the
   scabbard, then gripping, then out along the blade, then at the ready - and a
   sequence wants keys and a clock, not more nested lerps. Written as data so
   the timing can be moved without touching a line of the code that plays it.

   This was the reload's own type until a sword needed the same thing for its
   draw and its sheathe. Nothing about keys, easing curves, frames of reference
   or impact beats was ever about magazines; only the names were. The two
   helpers below that ARE about a magazine kept theirs.

   Reached through an accessor for the same reason the sprint carry is: see
   weapon_spec_reload_sequence. */

/* How the motion INTO a key is shaped.

   This is the difference between a hand and a robot arm, and it was the loudest
   thing wrong with the first version of this track: every segment ran at a
   constant rate, so every motion had zero acceleration at both ends and then
   changed direction instantly at each key. That is a machine following
   waypoints. A limb accelerates, and which way it accelerates says what the
   motion IS - a magazine ripped out of a well leaves fast and slows as it comes
   clear, and a magazine driven into one builds up and arrives hard. Those are
   opposite shapes, and running both at a constant rate is what made the whole
   thing read as generated.

   Stated per key rather than picked by the player, because the shape belongs to
   the movement: it is as much a property of "the magazine is stripped here" as
   the position is. Only what is on the far end of a segment matters, so a key's
   ease describes how the hand arrives AT it and the first key's is unused. */
typedef enum {
    /* Constant rate. For a dwell between two keys at the same place, where
       there is no motion to shape. */
    ACTION_EASE_HOLD,
    /* Constant rate again, and the same arithmetic, but it means the opposite
       thing: do NOT slow down here. For a key the hand is routed THROUGH rather
       than to - a waypoint on a curve, where easing in and out would put a stop
       in the middle of one continuous sweep. Named apart from HOLD because a
       reader has to be able to tell a dwell from a pass, and because the day
       either wants a different curve they must not both move. */
    ACTION_EASE_THROUGH,
    /* Smoothstep: away slowly, up to speed, and settling in. A hand crossing
       to somewhere and stopping there. The default. */
    ACTION_EASE_SMOOTH,
    /* Off the mark hard and decelerating in. A magazine ripped clear, a hand
       coming off the floorplate - motions whose energy is at the START. */
    ACTION_EASE_SNAP,
    /* Building and arriving hard, with no settle. Pressing the release, driving
       the magazine home, the tap that confirms it - motions that exist in order
       to hit something. */
    ACTION_EASE_DRIVE
} ReloadEase;

typedef enum {
    /* The point is in the GUN's own model space and rides the live weapon
       transform, so a hand on the magazine follows the gun through its bob and
       its kick. Every point on the weapon is one of these. */
    ACTION_ON_WEAPON,
    /* The point is in CAMERA space and belongs to the body, not the gun - the
       chest pouch, which stays where it is however the weapon moves. The
       distinction is the whole reason a frame is stated per key. */
    ACTION_IN_VIEW,
    /* The magazine only: carried, so it goes wherever the hand went. Saves
       stating the same travel twice and keeps the two from drifting apart. */
    ACTION_IN_HAND
} ReloadFrame;

typedef struct {
    /* Where in the reload this key sits, 0 to 1 of the weapon's reload_time. */
    float at;

    /* How the hand gets here from the key before it. Unused on the first key,
       which has nothing before it to come from. */
    ReloadEase ease;

    /* The support hand. The firing hand never leaves the pistol grip, which is
       why it is not stated here. */
    ReloadFrame hand_frame;
    float hand[3];

    /* The magazine. `drawn` is zero for the moment the hand is empty between
       dropping the spent one and taking a fresh one - the one beat that says a
       magazine was actually changed rather than refilled in place.

       `loaded` picks whether the rounds are drawn with it. The spent magazine
       leaves without them and the fresh one arrives carrying them, which costs
       nothing: they are already a separate material on the mesh. */
    int drawn;
    int loaded;
    ReloadFrame mag_frame;
    float mag[3];
    /* Degrees about the bore. The spent magazine tips as it falls away; a
       magazine that dropped perfectly flat reads as a prop on a rail. */
    float mag_roll;
    /* Degrees about the gun's right, which rocks the magazine's bottom fore and
       aft. Negative brings the bottom back toward the shooter, which is the
       angle a wrist actually presents a curved magazine to a well at: the front
       lip goes in first and the body is rocked straight as it seats.

       Positive is the reverse and is what a spent one does going the other way.
       Both are about the magazine's own origin, which sits at its mouth, so
       this pivots it where a real one pivots - on the lip in the well. Without
       it a magazine goes in like a card into a slot, which is the other half of
       why the insertion read as machinery. */
    float mag_pitch;
} ActionKey;

typedef struct {
    const ActionKey* keys;
    int count;
    /* Where in the reload the spent magazine stops being the viewmodel's and
       becomes the world's. Stated here rather than derived from the keys
       because it is a handoff and both halves have to agree on when: before it
       the magazine is drawn in front of the eye on the gun's own transform,
       after it there is a real one falling in the arena, and either two
       magazines or none is worse than either alone. */
    float drop_at;

    /* The two moments in the track that are IMPACTS rather than movements: the
       magazine arriving at the top of the well, and the hand slapping its base
       to confirm it is locked.

       Stated here for exactly the reason drop_at is - they are agreements
       between two halves that have to hold. The keys put the hand there and the
       gun's own jolt fires off these, and a jolt derived from the keys by
       guessing which of them looked like an arrival would come loose the first
       time somebody moved one. */
    float seat_at;
    float tap_at;
} ActionTrack;

/* The two keys either side of `progress`, and how far between them it is.
   Returns the pair rather than a resolved point because resolving needs the
   live gun transform, which is the renderer's to hold and not this file's -
   and because the two consumers, the arms and the magazine, resolve different
   parts of the same key. Clamped at both ends. */
typedef struct {
    const ActionKey* from;
    const ActionKey* to;
    float t;
} ActionSample;

void action_track_sample(const ActionTrack* sequence, float progress,
                            ActionSample* out);

/* How much of the reload pose the gun is wearing at `progress`, 0 to 1: up at
   the start, held through the middle, back down at the end. Separate from the
   keys because the gun's own travel is a swell and not a path - it goes to one
   place and comes back, and stating that as keyframes would be four numbers
   pretending to be an animation. */
float reload_sequence_gun_blend(float progress);

/* The gun's own recoil off a magazine being seated and then tapped home, 0 to 1,
   spiking at each of the sequence's two impact moments and decaying straight
   out. Scaled and turned into an offset by whoever poses the gun.

   It is here rather than on the weapon because it is a property of the
   SEQUENCE - it fires at times this track states - and because it has to be
   readable by anything that takes a point off the drawn gun. A jolt that moved
   the mesh and not the muzzle would put the tracer's origin off the barrel for
   as long as it lasted.

   Two spikes rather than one continuous shape: a seat and a tap are two
   separate collisions with a gap between them, and that gap is the beat that
   makes the second one read as a confirmation of the first rather than as part
   of the same push. */
float reload_sequence_seat_jolt(const ActionTrack* sequence, float progress);

#endif
