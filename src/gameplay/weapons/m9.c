#include "gameplay/weapon_spec.h"

#include <stddef.h>

/* The M9. A Beretta 92FS: fifteen rounds of 9mm, fired one pull at a time,
   over iron sights.

   The first weapon here that is not a variation on the others. Every gun in
   this file's neighbourhood so far has been automatic and has worn glass, and
   the three of them differ by rate and cone; this one differs by not being
   either. What it asks of the player is a shot at a time, aimed, and what it
   gives back is a cone that barely opens and a kick that is gone before the
   next round can be sent.

   Rate is stated as an interval like everything else, but the number means
   something different here: 0.15 is not a fire rate, it is how fast a trigger
   can be pulled deliberately. A real 92FS will run faster than that in a hurry
   and will not put the rounds anywhere. Nothing in the build models a
   semi-automatic action - the trigger is held and rounds leave - so the
   interval is where "one shot at a time" has to live, and it is why the cone
   figures below are the tightest in the game and can afford to be. */
static const FirearmSpec M9_FIREARM = {
    .magazine = 15,                     /* 15+1, and the chamber is not modelled */
    .fire_interval = 0.150f,            /* 400 a minute, which is a fast finger */
    .reload_time = 1.85f,               /* a pistol magazine, and the quickest here */

    /* The tightest at rest of anything here and the slowest to open, which
       together are the whole argument for the gun. A fifth of a degree of
       growth per round at 6.7 rounds a second is 0.33 of cone a second,
       against the auto pistol's 0.91 - so a magazine emptied as fast as the
       finger allows still ends inside the rifle's resting cone. What it cannot
       do is put fifteen rounds anywhere in the time the auto pistol puts
       thirty, and that is the trade, whole. */
    .bloom_min = 0.08f,
    .bloom_max = 0.70f,
    .bloom_per_shot = 0.050f,
    .bloom_recovery = 4.4f,
    /* Longer than the interval on purpose, and by more than the others: at
       0.18 against a 0.15 trigger, a player pulling as fast as they can never
       reaches the recovery at all and one who paces themselves does. That
       gap is where the aimed shot is paid for. */
    .bloom_recovery_delay = 0.180f,
    .ads_bloom_scale = 0.42f,

    /* A hard kick for a 9mm, and deliberately harder per round than the auto
       pistol's 0.26. There is a third of a second between rounds to absorb it,
       so what it costs is not control but the sight picture, which has to be
       found again - and finding it again is the beat this gun is built around.
       Per second it is the softest gun in the game: 3.0 against the auto
       pistol's 3.0 at more than twice the rate. */
    .recoil_pitch = 0.45f,
    .recoil_yaw = 0.13f,
    .ads_recoil_scale = 0.55f,

    /* A slide-mounted reflex, and this gun used to carry nothing.

       Nothing was a defensible state and stopped being one. The argument for
       it was that aiming brings the gun to centre without dropping it, so the
       bore lands on the crosshair and the irons are only a way of finding what
       the bore already points at. Every step of that is true. What it needed
       was a crosshair, and the HUD fades its own out as the aim comes up on
       the assumption the weapon has a reticle taking over - so a sightless
       aim went to an empty screen. The fade is fixed at its source in
       src/render/hud.c; this is the other half, because a handgun that can
       wear one of four optics should wear one.

       The reflex rather than either tube: its mount is flat-bottomed and sits
       on the slide, where a tube dot's clamp jaws wrap a rail that is not
       there and would bury 8mm of themselves in the slide.

       rail_height and rail_forward were zero, on the grounds that they were
       half of a sum whose other half did not exist. They are measured now, off
       the exported mesh:

         0.0184 is the top of the slide above the bore. It is the flat the
         mount rests on, and this gun's is not continuous - a 92's slide is
         open past 55mm and the top drops to the exposed barrel - so the plate
         bridges the ejection port and rests on the high points at 30-45,
         65-70 and 90-95mm, all of which are at this same height. That is what
         a real optic plate on this pistol does too.

         0.085 is where along the slide the reticle sits, chosen so the sight's
         own 51mm foot (2.7mm to 53.7mm behind its glass, per its
         measured.json) lands between 31 and 82mm - clear of the dovetail at
         the back and of the barrel at the front. */
    .sight = &SIGHT_REFLEX,
    .rail_height = 0.0184f,
    .rail_forward = 0.085f,

    /* Zero: aiming this pistol does not move it toward or away from the eye
       at all, it only drops it so the glass comes up to the line of sight.

       It used to be -0.10, a nudge in, on the argument that a pistol is
       brought to the eye by the arms straightening. The argument was right and
       the sign was wrong - straightening your arms pushes a handgun AWAY from
       your chest, not toward your face - and it was also what stopped the hip
       pose being fixed. The two distances are independent: the hip is
       view_forward and the aim is view_forward plus this. What the arms
       actually constrain is the AIMED distance, which cannot come inside about
       0.28m before the crossfade breaks; the hip is free below that. So the
       hip came in to 0.300 and this went to zero to leave the aim where it
       was, which is the whole of the fix for a gun that read small. */
    .view_ads_forward = 0.0f,
    /* 53, down from 55, and that is as far as it goes. Every weapon's aimed
       viewmodel FOV is wider than the 43 it sits at from the hip, which trades
       the gun filling the screen for being able to see past it; narrowing it
       makes the aimed gun bigger. 46 would have been better looking and does
       not work: at 52 and below the arm solver can no longer place the
       shoulders for the aimed pose anywhere near where it places them for the
       sprint, jumps to a placement half a metre forward, and the crossfade
       between the two folds both elbows to 0.43 of their reach. 53 passes and
       52 fails, measured by tools/viewmodel_probe.c, so this is the edge of
       what the arms allow rather than a number that was liked. */
    .view_ads_fov = 53.0f,

    /* Measured off the mesh by tools/gun_to_glb.py, which writes them to
       measured.json beside it. The bore is the model's own y = 0, so the flash
       sits on it, and 0.217 is the whole length of the gun: the origin is at
       the back of the slide and the muzzle is at the front of it. */
    .muzzle_up = 0.0f,
    .muzzle_forward = 0.217f,

    /* Neither, and the mesh is why. This gun arrived as one node under one
       material - see its import.json - so there is no magazine to name and
       nothing for a reload to draw apart from the frame. The reload still
       takes its time and still plays; what it does not do is show the
       magazine leaving, because this mesh cannot. */
    .magazine_part = NULL,
    .rounds_part = NULL,
};

const WeaponSpec WEAPON_SPEC_M9 = {
    .name = "M9",
    .kind = WEAPON_CLASS_FIREARM,
    .firearm = &M9_FIREARM,

    /* Two to the chest and one is left over, which is the shape this gun
       wants: a pistol that takes three is a pistol nobody chooses, and one
       that takes two rewards the aim it already demands. Against the auto
       pistol's 18 it is nearly three times the round, at a fifth of the rate.

       Note where that sits relative to the rifles: both of those kill a chest
       in three and this does it in two, which is not a mistake. They kill in
       three in a third of the time. */
    .damage = 34.0f,
    .range = 300.0f,

    .mesh_path = "models/weapons/m9/m9.glb",

    /* Right and down are the auto pistol's, which is the same claim the
       carbine makes about the rifle: a sidearm hangs in the corner of the
       frame where sidearms hang, and there is nothing about this one that
       moves it.

       Forward is arithmetic and not taste, worked the way the carbine's was.
       What has to be constant between two pistols is where the FIRING HAND
       ends up, because that is what the arms are solved to and it is what the
       eye reads as the gun's distance. The auto pistol holds at model z +0.010
       and sits 0.46 forward, so its hand lands 0.450 in front of the eye; this
       gun's hand is at -0.025, so 0.425 puts it in the same place. The 3.5cm
       of difference is the M9's grip being further forward of the back of its
       slide than the boxes' grip is of theirs, and it belongs at the grip
       rather than being spread over the whole gun. */
    .view_right = 0.155f,
    .view_down = 0.134f,
    /* 0.300, in from 0.425. A pistol at arm's length is honest and reads as a
       toy: 217mm of gun pointing away from the eye covers very little screen,
       and that is most of why this one looked small. 12.5cm closer is about
       40% larger.

       view_right and view_down came in with it, in proportion. They are
       absolute offsets rather than angles, so pulling the gun toward the eye
       without scaling them swings it toward the edge of the frame - 0.22 right
       at 0.300 away is 36 degrees off centre, which is off the side of the
       screen. Kept in proportion, the gun grows where it already sat. */
    .view_forward = 0.300f,

    /* The rifle's, for the rifle's reasons - see the assault rifle's entry. A
       sidearm is carried off the same shoulders. */
    .neck_follow = 0.30f,

    /* Both hands at the grip, the support hand wrapped around the firing one,
       which is what a pistol leaves them - there is nothing forward to hold.

       Measured off the mesh rather than copied off the auto pistol, which is
       the only reason to prefer a real model to boxes. The grip runs from the
       back of the frame forward 60mm and hangs 120mm below the bore; a fist
       closes on the upper half of that, 55mm down and 25mm forward. The
       support hand goes where the auto pistol's goes relative to the firing
       one - 30mm lower and 40mm further forward, wrapping the front of it. */
    .hold = WEAPON_HOLD_HANDS_TOGETHER,
    .grip_right = {0.0f, -0.055f, -0.025f},
    .grip_left = {0.0f, -0.085f, -0.065f},

    /* The library's pistol poses, on an actual pistol for the first time. */
    .clips = {{ "Pistol_Aim_Down", "Pistol_Aim_Neutral", "Pistol_Aim_Up" }},
};
