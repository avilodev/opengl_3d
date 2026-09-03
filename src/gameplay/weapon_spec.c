#include "gameplay/weapon_spec.h"
#include "gameplay/movement_tuning.h"
#include "gameplay/weapon_tuning.h"

#include <stddef.h>

/* The numbers a gun no longer states, derived from the halves that do state
   themselves. Each one lives here and only here, so there is nowhere for a
   second copy to drift from. */

const FirearmSpec* weapon_spec_firearm(const WeaponSpec* spec) {
    return spec && spec->kind == WEAPON_CLASS_FIREARM ? spec->firearm : NULL;
}

const MeleeSpec* weapon_spec_melee(const WeaponSpec* spec) {
    return spec && spec->kind == WEAPON_CLASS_MELEE ? spec->melee : NULL;
}

/* The four below are all a sight's, and a weapon with no gun half has no
   sight. They answer zero rather than dereferencing, because every one of
   them is read on a frame the player might be holding anything - and a sight
   height of zero is a weapon that does not drop when it aims, which is what a
   weapon with no sight should do. */

float weapon_spec_sight_height(const WeaponSpec* spec) {
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    if (!gun || !gun->sight) return 0.0f;
    return gun->rail_height + gun->sight->reticle_above_seat;
}

float weapon_spec_sight_forward(const WeaponSpec* spec) {
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    return gun ? gun->rail_forward : 0.0f;
}

float weapon_spec_ads_fov(const WeaponSpec* spec) {
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    if (!gun || !gun->sight) return VIEW_BASE_FOV;
    /* One power divides by one, so an unmagnified sight lands exactly on the
       base field of view rather than near it. */
    return VIEW_BASE_FOV / gun->sight->magnification;
}

float weapon_spec_view_ads_fov(const WeaponSpec* spec) {
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    return gun ? gun->view_ads_fov : spec->view_fov;
}

float weapon_spec_ads_time(const WeaponSpec* spec) {
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    return gun && gun->sight ? gun->sight->ads_time : 0.0f;
}

/* The shared carry. Assembled rather than stated, because the angles and the
   position are not independent: the pose turns about the model's origin, which
   sits on the bore at the back of the receiver, so the position that frames the
   gun depends entirely on how far it has been swung. Authoring both by hand
   means re-deriving one every time the other moves, and getting it wrong looks
   like the gun falling out of the world.

   So the carry states where the HAND is and how the gun is angled, and the
   origin is solved: put the gun's own grip at the hand, and let the rest of it
   hang off that. The hand is what has to stay on screen - it is holding the
   thing - and this is what keeps it there at any angle.

   Not cached, because the answer depends on the spec's grip and the spec can
   change under a weapon swap. It is nine multiplies on a pose that is already
   rebuilt every frame. */
const ViewmodelPose* weapon_spec_sprint_pose(const WeaponSpec* spec) {
    static ViewmodelPose carry;
    carry.pitch = WEAPON_VIEW_SPRINT_PITCH;
    carry.yaw = WEAPON_VIEW_SPRINT_YAW;
    carry.roll = WEAPON_VIEW_SPRINT_ROLL;

    /* Where the firing hand goes, camera space. Forward and down are stated
       positive in the header and negated here, as the specs' own offsets are. */
    const float hand[3] = {WEAPON_VIEW_SPRINT_HAND_RIGHT,
                           -WEAPON_VIEW_SPRINT_HAND_DOWN,
                           -WEAPON_VIEW_SPRINT_HAND_FORWARD};
    /* The grip once the gun has been swung, as an offset from the origin. The
       origin then goes wherever puts that offset onto the hand. */
    float turned[3];
    carry.x = carry.y = carry.z = 0.0f;
    viewmodel_pose_transform_direction(&carry, spec->grip_right, turned);
    carry.x = hand[0] - turned[0];
    carry.y = hand[1] - turned[1];
    carry.z = hand[2] - turned[2];
    return &carry;
}

/* The magazine well, from the mesh: M4Mag_Mat spans y -0.207 to -0.011 and
   z -0.175 to -0.079, so this is the middle of the magazine's body - the part a
   hand actually closes on, rather than the seam it sits in. Measured off the
   asset like every other point on the gun. */
static const float RELOAD_MAG_GRIP[3] = {0.0f, -0.150f, -0.127f};

/* The magazine change, laid out on the reload's own clock.

   Read down the `at` column and it is the action: settle, take hold of the
   magazine, press the release, rip it clear, cross to the chest with an empty
   hand, come back with a fresh one, offer it up to the well at an angle, drive
   it home, come off the floorplate and slap it, hold, and back to the
   handguard.

   The proportions are the point, and they were wrong before. Every segment used
   to run at roughly the same length and at a constant rate, which is what made
   the whole thing read as generated rather than performed - a real magazine
   change is not evenly paced anywhere. Against the rifle's 2.40s:

     0.10 -> 0.24   0.34s   hand off the handguard onto the magazine
     0.24 -> 0.29   0.12s   the release pressed, the magazine broken free
     0.29 -> 0.34   0.12s   RIPPED clear, and after that gravity has it
     0.34 -> 0.51   0.41s   the hand crossing to the pouch, and empty at it
     0.51 -> 0.73   0.53s   a fresh one out, up, and offered to the well
     0.73 -> 0.81   0.19s   rocked straight and DRIVEN home
     0.81 -> 0.94   0.31s   off the floorplate, the tap, and held

   So the spent magazine is out of the gun in under a quarter of a second and
   the fresh one takes three times as long to go in and be confirmed. That is
   the asymmetry the old track did not have: coming out is a release and a rip,
   which is fast because nothing has to be aligned; going in is a fit, which is
   slow because something does.

   The tap is the part with no gameplay in it at all, and it is the part that
   sells it. A magazine that arrives at the well and is simply there was seated
   by nobody. A hand that comes off the floorplate and slaps it back is a person
   making sure - and the gun's own jolt fires on both the seat and the tap, so
   there are two knocks with a beat between them rather than one arrival.

   The chest is the one point not on the gun, and where it sits is not free.
   It has to be reachable while the firing hand stays on a pistol grip that the
   reload pose has brought up almost to eye level, and the two pull against each
   other: a pouch low on the belly leaves the firing arm at 0.61 of its reach
   with the torso bladed to the 80 degrees the search will allow and no further,
   and the swing to get there dips the support arm to 0.46 halfway across. Held
   HIGH - 0.14 below the eye rather than 0.30 - both arms come out at 0.74 and
   0.70 with the shoulders barely turning, which is also the shorter journey and
   so the smoother one. It is a pouch high on a plate carrier, which is where
   magazines are actually carried, and the geometry and the reference agree.

   The floorplate is measured off the mesh like everything else here: M4Mag_Mat
   bottoms out at y -0.207, so a hand under it to slap it sits at about -0.235
   and comes off it to -0.272. */
/* The points at and on the way to the pouch, off the one set of tuning numbers
   so that moving the kit moves all of them together. Camera space, and y is
   downward-negative here, which is why every DOWN is negated. */
#define POUCH_INSIDE WEAPON_VIEW_POUCH_RIGHT, \
                     -(WEAPON_VIEW_POUCH_DOWN + WEAPON_VIEW_POUCH_REACH), \
                     -WEAPON_VIEW_POUCH_FORWARD
/* Clear of the pouch, a fraction of the way back along the arc rather than
   straight up - see WEAPON_VIEW_POUCH_CLEAR. Written out as a lerp between the
   mouth of the pouch and the first waypoint back, so it follows either of them
   if they move. */
#define POUCH_LERP(a, b) ((a) + ((b) - (a)) * WEAPON_VIEW_POUCH_CLEAR)
#define POUCH_DRAWN  POUCH_LERP(WEAPON_VIEW_POUCH_RIGHT, \
                                WEAPON_VIEW_POUCH_VIA2_RIGHT), \
                     -POUCH_LERP(WEAPON_VIEW_POUCH_DOWN, \
                                 WEAPON_VIEW_POUCH_VIA2_DOWN), \
                     -POUCH_LERP(WEAPON_VIEW_POUCH_FORWARD, \
                                 WEAPON_VIEW_POUCH_VIA2_FORWARD)
/* The two waypoints on the reach arc between the well and the belt. */
#define POUCH_VIA1   WEAPON_VIEW_POUCH_VIA1_RIGHT, \
                     -WEAPON_VIEW_POUCH_VIA1_DOWN, \
                     -WEAPON_VIEW_POUCH_VIA1_FORWARD
#define POUCH_VIA2   WEAPON_VIEW_POUCH_VIA2_RIGHT, \
                     -WEAPON_VIEW_POUCH_VIA2_DOWN, \
                     -WEAPON_VIEW_POUCH_VIA2_FORWARD

static const ReloadKey RELOAD_KEYS[] = {
    /*  at   ease                 hand frame       hand                        drawn loaded  mag frame      mag                    roll  pitch */
    { 0.00f, RELOAD_EASE_HOLD,    RELOAD_ON_GUN,   { 0.000f, -0.060f, -0.330f },  1, 1, RELOAD_ON_GUN,   { 0.0f,  0.00f,  0.00f },  0.0f,  0.0f },
    /* The gun swinging in. The hand has not moved yet - it is still on the
       handguard, going where the weapon goes. */
    { 0.10f, RELOAD_EASE_HOLD,    RELOAD_ON_GUN,   { 0.000f, -0.060f, -0.330f },  1, 1, RELOAD_ON_GUN,   { 0.0f,  0.00f,  0.00f },  0.0f,  0.0f },
    /* The hand arrives on the magazine before anything moves, and settles onto
       it rather than stopping dead on it. */
    { 0.24f, RELOAD_EASE_SMOOTH,  RELOAD_ON_GUN,   { 0.000f, -0.150f, -0.127f },  1, 1, RELOAD_ON_GUN,   { 0.0f,  0.00f,  0.00f },  0.0f,  0.0f },
    /* The release, pressed. A short hard push that ends in a click, so it
       DRIVES - and the magazine is already a couple of centimetres out of the
       well by the end of it, because that is what a released magazine does
       under its own weight before the hand has done anything. */
    { 0.29f, RELOAD_EASE_DRIVE,   RELOAD_ON_GUN,   { 0.000f, -0.178f, -0.127f },  1, 0, RELOAD_ON_GUN,   { 0.0f, -0.028f, 0.00f },  0.0f,  0.0f },
    /* Ripped clear, straight down the well before it is let go - a magazine
       that started tumbling inside the receiver would be wrong. Fast off the
       mark and slowing as it comes free, which is the shape of a pull. */
    { 0.34f, RELOAD_EASE_SNAP,    RELOAD_ON_GUN,   { 0.000f, -0.268f, -0.127f },  1, 0, RELOAD_ON_GUN,   { 0.0f, -0.118f, 0.00f },  4.0f,  2.0f },
    /* Let go, and the hand starts down the arc for the belt. The magazine keeps
       falling on its own, which is what makes it read as dropped rather than
       put down. Both waypoints are PASSED THROUGH: this is one sweep, and a
       hand that stopped twice on the way down would be worse than the fold the
       waypoints exist to avoid. */
    { 0.40f, RELOAD_EASE_SMOOTH,  RELOAD_IN_VIEW,  { POUCH_VIA1 },                1, 0, RELOAD_ON_GUN,   { 0.0f, -0.26f,  0.02f }, 11.0f,  6.0f },
    { 0.46f, RELOAD_EASE_THROUGH, RELOAD_IN_VIEW,  { POUCH_VIA2 },                1, 0, RELOAD_ON_GUN,   { 0.0f, -0.42f,  0.03f }, 18.0f, 10.0f },
    /* Down into the pouch, empty. One key, and the whole point of the track. */
    { 0.51f, RELOAD_EASE_SMOOTH,  RELOAD_IN_VIEW,  { POUCH_INSIDE },              0, 0, RELOAD_ON_GUN,   { 0.0f, -0.58f,  0.04f }, 24.0f, 14.0f },
    /* Up out of it with a fresh one. The rise is what says the magazine was
       drawn from somewhere rather than appearing in the fist. */
    { 0.58f, RELOAD_EASE_SMOOTH,  RELOAD_IN_VIEW,  { POUCH_DRAWN },               1, 1, RELOAD_IN_HAND,  { 0.0f,  0.00f,  0.00f },  0.0f,-16.0f },
    /* And back up the same arc, carrying it. */
    { 0.64f, RELOAD_EASE_SMOOTH,  RELOAD_IN_VIEW,  { POUCH_VIA2 },                1, 1, RELOAD_IN_HAND,  { 0.0f,  0.00f,  0.00f },  0.0f,-15.0f },
    { 0.70f, RELOAD_EASE_THROUGH, RELOAD_IN_VIEW,  { POUCH_VIA1 },                1, 1, RELOAD_IN_HAND,  { 0.0f,  0.00f,  0.00f },  0.0f,-13.0f },
    /* Offered up to the mouth of the well, bottom rocked back toward the
       shooter - the angle a wrist actually presents a curved magazine at, front
       lip in first. The hand arrives short of seated, not at it. */
    { 0.76f, RELOAD_EASE_SMOOTH,  RELOAD_ON_GUN,   { 0.000f, -0.245f, -0.140f },  1, 1, RELOAD_IN_HAND,  { 0.0f,  0.00f,  0.00f },  0.0f,-11.0f },
    /* Rocked straight and DRIVEN home. This is the seat: the jolt fires here,
       and the ease is what makes it an arrival rather than a slide. */
    { 0.83f, RELOAD_EASE_DRIVE,   RELOAD_ON_GUN,   { 0.000f, -0.150f, -0.127f },  1, 1, RELOAD_ON_GUN,   { 0.0f,  0.00f,  0.00f },  0.0f,  0.0f },
    /* Off the floorplate. A short flick down to make room for the tap, and it
       leaves fast because the hand is bouncing off what it just hit. */
    { 0.87f, RELOAD_EASE_SNAP,    RELOAD_ON_GUN,   { 0.000f, -0.272f, -0.124f },  1, 1, RELOAD_ON_GUN,   { 0.0f,  0.00f,  0.00f },  0.0f,  0.0f },
    /* The tap. Up into the base of the magazine, hard, and the second jolt
       fires here. */
    { 0.92f, RELOAD_EASE_DRIVE,   RELOAD_ON_GUN,   { 0.000f, -0.235f, -0.124f },  1, 1, RELOAD_ON_GUN,   { 0.0f,  0.00f,  0.00f },  0.0f,  0.0f },
    /* Held against it for a beat. Confirmed - which is the only thing this key
       is for, and without it the tap reads as a twitch. */
    { 0.95f, RELOAD_EASE_HOLD,    RELOAD_ON_GUN,   { 0.000f, -0.235f, -0.124f },  1, 1, RELOAD_ON_GUN,   { 0.0f,  0.00f,  0.00f },  0.0f,  0.0f },
    /* And back to the handguard as the gun goes out. */
    { 1.00f, RELOAD_EASE_SMOOTH,  RELOAD_ON_GUN,   { 0.000f, -0.060f, -0.330f },  1, 1, RELOAD_ON_GUN,   { 0.0f,  0.00f,  0.00f },  0.0f,  0.0f },
};

/* The handoff at 0.485 is the midpoint between the last key that draws a
   magazine and the empty one, which is exactly where the viewmodel's own
   magazine stops being drawn - so the real one appears in the same frame the
   held one disappears.

   The seat and the tap are the two keys the gun's own jolt fires on, named here
   rather than found by looking for keys that resemble arrivals. */
static const ReloadSequence RELOAD_SEQUENCE = {
    RELOAD_KEYS, (int)(sizeof RELOAD_KEYS / sizeof RELOAD_KEYS[0]),
    0.485f, 0.83f, 0.92f
};

const ReloadSequence* weapon_spec_reload_sequence(const WeaponSpec* spec) {
    (void)spec;
    return &RELOAD_SEQUENCE;
}

const ViewmodelPose* weapon_spec_reload_pose(const WeaponSpec* spec) {
    static ViewmodelPose held;
    held.pitch = WEAPON_VIEW_RELOAD_PITCH;
    held.yaw = WEAPON_VIEW_RELOAD_YAW;
    held.roll = WEAPON_VIEW_RELOAD_ROLL;

    /* Anchored at the firing grip, exactly as the sprint carry is, and for the
       same reason: the pose turns about the model's origin at the back of the
       receiver, so stating a position instead of a hand would have to be
       re-derived by hand every time one of the three angles moved. */
    const float hand[3] = {WEAPON_VIEW_RELOAD_HAND_RIGHT,
                           -WEAPON_VIEW_RELOAD_HAND_DOWN,
                           -WEAPON_VIEW_RELOAD_HAND_FORWARD};
    float turned[3];
    held.x = held.y = held.z = 0.0f;
    viewmodel_pose_transform_direction(&held, spec->grip_right, turned);
    held.x = hand[0] - turned[0];
    held.y = hand[1] - turned[1];
    held.z = hand[2] - turned[2];
    return &held;
}

/* Where the support hand closes on the magazine, in the gun's own space. Public
   through the sequence's keys, but the arms want it by name to know when the
   hand is holding the magazine rather than merely near it. */
const float* weapon_spec_mag_grip(const WeaponSpec* spec) {
    (void)spec;
    return RELOAD_MAG_GRIP;
}

void weapon_spec_rest_pose(const WeaponSpec* spec, float sprint, float reload,
                           float ads, ViewmodelPose* out) {
    /* At the hip: off to the right, low, and canted toward the player. */
    /* The trailing pair is the recoil rotation, which a rest pose never has:
       it is the weapon's, added once the carry has been blended. */
    ViewmodelPose hip = {
        spec->view_right, -spec->view_down, -spec->view_forward,
        0.0f, WEAPON_VIEW_HIP_CANT, 0.0f,
        0.0f, 0.0f,
    };
    /* Aimed: dead centre, dropped by exactly the mount height so the reticle
       lands on the middle of the screen, pulled forward, and square - a canted
       sight would point where it is canted, which is the one place that would
       stop being cosmetic. */
    /* A weapon with no gun half never aims - `ads` is zero for it - but the
       aimed pose is still built on the way past, so it has to be buildable.
       No pull-in is the honest answer for a weapon with nothing to bring to
       the eye: it stays where the hip put it. */
    const FirearmSpec* gun = weapon_spec_firearm(spec);
    ViewmodelPose aimed = {
        0.0f, -weapon_spec_sight_height(spec),
        -spec->view_forward - (gun ? gun->view_ads_forward : 0.0f),
        0.0f, 0.0f, 0.0f,
        0.0f, 0.0f,
    };

    /* Sprint, then the reload over the top of it, then the aim over both. The
       order is the precedence: a reload interrupts a carry, and shouldering the
       weapon overrides either - which is what keeps "aiming is never blocked"
       true of the pose and not only of the number. */
    viewmodel_pose_lerp(&hip, weapon_spec_sprint_pose(spec), sprint, out);
    viewmodel_pose_lerp(out, weapon_spec_reload_pose(spec), reload, out);
    viewmodel_pose_lerp(out, &aimed, ads, out);
}
