#include "gameplay/weapon_spec.h"

#include <stddef.h>

/* The .357 Magnum. A six-inch revolver, eight rounds in the cylinder, and the
   hardest single hit of any gun here.

   The M9's argument taken one step further, which is the only reason to carry
   both. That gun asks for an aimed shot and pays 34; this one asks for a
   slower one and pays 62, which is the number that matters: it is over 50, so
   a chest takes two rounds from the M9 and one from this. Everything else
   about the gun is the price of that line - eight rounds instead of fifteen, a
   reload nearly twice as long, and a kick that has to be ridden out rather
   than held down.

   Eight and not six, because the mesh has eight chambers. The artist modelled
   an eight-shot .357 - a 627 rather than a Python - and the spec follows the
   model rather than the calibre's reputation, the same way its length does. */
static const FirearmSpec MAGNUM_357_FIREARM = {
    /* Eight, off the mesh: models/weapons/magnum_357/import.json counts the
       loaded rounds the artist put in the cylinder. */
    .magazine = 8,
    .fire_interval = 0.230f,            /* 260 a minute - double action, hurried */
    /* Eight rounds pressed in one at a time, or with a speedloader and still
       slower than a magazine. The longest reload in the game and the gun's
       real weakness: three seconds is most of a fight. */
    .reload_time = 3.30f,

    /* Nearly the M9's cone at rest, and it opens harder. A revolver is not a
       target pistol and this one has a heavy double-action pull; what keeps it
       usable is the same thing that keeps the M9 usable, which is that four
       rounds a second gives the cone time to close between them. */
    .bloom_min = 0.10f,
    .bloom_max = 0.95f,
    .bloom_per_shot = 0.095f,
    .bloom_recovery = 4.2f,
    .bloom_recovery_delay = 0.260f,
    .ads_bloom_scale = 0.44f,

    /* The hardest kick in the game per round, by a wide margin - two and a
       half times the rifle's - and still the second softest per second, at 5.7
       against the rifle's 8.5. That is what a magnum is: the round is the
       event, and there is time between them.

       It bounces rather than climbs, like everything else here, so the answer
       is to wait for the sights to come back rather than to memorise a pull.
       On this gun the wait is long enough to feel, which is the point. */
    .recoil_pitch = 1.30f,
    .recoil_yaw = 0.28f,
    /* Barely helps, and should not. Bracing a magnum does not make it kick
       less; it makes you readier for it. */
    .ads_recoil_scale = 0.80f,

    /* A reflex on the frame, where this gun used to carry nothing. See the
       M9's entry for why nothing stopped working - the short version is that
       the argument for it depended on a crosshair the HUD was fading out.

       On the TOPSTRAP and not the barrel rib, which is the choice this gun
       has and the M9 does not. The rib is longer and flatter and would take
       the mount happily, but it is 140mm out: the further the glass sits from
       the eye the smaller its window is on screen, and a 28mm pane at half a
       metre is a keyhole. The topstrap puts it as far back as a revolver
       allows.

       Measured off the exported mesh:

         0.0188 is the top of the frame above the bore, over the 75-115mm
         stretch the mount covers. It varies by under a millimetre across that
         run, so this is the highest point it touches and the mount floats
         under a millimetre at the low end rather than sinking into the high.

         0.129 puts the sight's 51mm foot between 75 and 126mm - onto the
         topstrap, clear of the hammer behind it, and stopping before the frame
         steps down at 115mm rather than hanging over the cylinder. */
    .sight = &SIGHT_REFLEX,
    .rail_height = 0.0188f,
    .rail_forward = 0.129f,

    /* The M9's, because it is held the same way and for the same reasons its
       entry gives at length: zero, so aiming only drops the gun onto the line
       of sight; and 53 rather than 46 because 52 is where the arm solver
       stops being able to blend the sprint carry into the aim. */
    .view_ads_forward = 0.0f,
    .view_ads_fov = 53.0f,

    /* Measured off the mesh by tools/gun_to_glb.py. The bore here is NOT the
       middle of the muzzle end - that would be a centimetre under the hole, in
       the middle of a barrel that is rib on top and ejector shroud below - so
       the importer takes it off the loaded round in the top chamber, which is
       the one thing in the mesh that is unambiguously on the bore line. See
       the manifest. 0.285 is the whole gun: origin at the back of the grip,
       muzzle at the front of a six-inch barrel. */
    .muzzle_up = 0.0f,
    .muzzle_forward = 0.285f,

    /* A revolver has no magazine to drop. The eight rounds stay in their
       chambers where the artist put them, drawn as part of the gun, because
       there is nothing for a reload to draw them apart FROM. */
    .magazine_part = NULL,
    .rounds_part = NULL,
};

const WeaponSpec WEAPON_SPEC_MAGNUM_357 = {
    .name = ".357 Magnum",
    .kind = WEAPON_CLASS_FIREARM,
    .firearm = &MAGNUM_357_FIREARM,

    /* One under a chest kill in one, and the arithmetic is worth spelling out
       because it is the same cliff the carbine sits under from the other side.
       The pool is 100 and the chest multiplier is flat, so between 50 and 99
       every number does exactly the same thing: two rounds. 62 is not a tuned
       value in the usual sense - it is a position in that band, chosen high
       enough that a limb hit still counts for something and low enough that
       the gun is not a one-round answer to anything.

       A request to make this hit harder is answered by 100 and by nothing in
       between, and 100 would make eight rounds into eight kills. */
    .damage = 62.0f,
    .range = 300.0f,

    .mesh_path = "models/weapons/magnum_357/magnum_357.glb",

    /* Right and down are the sidearm corner's, as on the M9. Forward is that
       gun's arithmetic run again: the firing hand wants to land 0.450 in front
       of the eye, this grip is at model z -0.035, so 0.415 puts it there. The
       gun being 7cm longer than the M9 shows up at the muzzle, which is where
       it is. */
    .view_right = 0.164f,
    .view_down = 0.142f,
    /* 0.310, in from 0.415, as the M9's was and for the same reason: a handgun
       held at arm's length covers very little screen and reads small. A
       centimetre further out than the M9's 0.300 because this gun is 68mm
       longer and would otherwise put its muzzle that much closer to the eye.

       view_right and view_down scaled with it - see the M9's note on why they
       have to, being offsets in metres and not angles. */
    .view_forward = 0.310f,

    .neck_follow = 0.30f,

    /* Both hands at the grip, as on any handgun. Measured off the mesh: the
       grip hangs 134mm below the bore and runs from the backstrap forward
       65mm, so the firing fist sits 60mm down and 35mm forward, and the
       support hand takes the M9's offset from it. */
    .hold = WEAPON_HOLD_HANDS_TOGETHER,
    .grip_right = {0.0f, -0.060f, -0.035f},
    .grip_left = {0.0f, -0.090f, -0.075f},

    .clips = {{ "Pistol_Aim_Down", "Pistol_Aim_Neutral", "Pistol_Aim_Up" }},
};
