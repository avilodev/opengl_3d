#include "gameplay/weapon_spec.h"
#include "gameplay/sight_spec.h"

#include <stddef.h>

/* The MP7. Forty rounds of 4.6mm at 950 a minute, on a rail, held by a folding
   vertical foregrip.

   The fastest gun in the game and the one that asks least of the player's aim
   and most of their position. Where the auto pistol was the easy gun, this is
   the CLOSE gun, and the difference is in the cone rather than the rate: this
   opens faster than anything here and closes faster too, so it is exact for
   the first four rounds and a spray after twelve. What that buys is that the
   first four arrive in a quarter of a second.

   Read beside the assault rifle, which is the comparison it exists for. The
   rifle is 600 a minute at 33 damage - three rounds and 0.2 seconds to a
   chest. This is 950 at 21 - five rounds and 0.25 seconds. Slower to the kill
   by a hair, and it gets there while moving, through a cone that was tight
   when it started. That is a submachine gun. */
static const FirearmSpec MP7_FIREARM = {
    .magazine = 40,
    .fire_interval = 0.0632f,           /* 950 rounds per minute */
    .reload_time = 2.10f,

    /* Tighter at rest than the auto pistol and looser at the ceiling, and it
       travels between them faster than anything else here: 0.085 a round at
       15.8 rounds a second is 1.34 of cone a second, against the rifle's 1.05.
       Twelve rounds is one held second and reaches the top of it.

       The recovery is the other half and the more important one. 6.6 a second
       against the rifle's 5.2, off a delay shorter than any other gun's,
       means a burst of four and a beat is genuinely a burst of four - the cone
       is back at rest before the finger is. This gun is meant to be tapped
       hard and often, not held. */
    .bloom_min = 0.16f,
    .bloom_max = 1.55f,
    .bloom_per_shot = 0.085f,
    .bloom_recovery = 6.6f,
    .bloom_recovery_delay = 0.090f,
    /* The deepest halving here, and it is what makes the gun worth aiming at
       all: at 0.34 of a cone that starts tighter than the rifle's, an aimed
       burst out of this is exact at a range the numbers above have no business
       reaching. */
    .ads_bloom_scale = 0.34f,

    /* Per round it is the softest gun in the game - a 4.6mm cartridge in a
       three-kilogram gun barely moves it. Per second it is nearly the worst:
       0.30 at 15.8 a second is 4.7 of climb, against the rifle's 8.5 and the
       carbine's 6.9. Low, fast and relentless, which reads on screen as a gun
       that shakes rather than one that kicks. */
    .recoil_pitch = 0.30f,
    .recoil_yaw = 0.11f,
    .ads_recoil_scale = 0.58f,

    /* The tube red dot, off the carbine's own mesh and on its third gun now.
       That is the claim sight_spec.h makes being used rather than asserted:
       nothing about this optic moved to fit an MP7, and nothing about the MP7
       moved to take it. The gun states its rail and the sight states its
       reticle height, and weapon_spec_sight_height adds them.

       Which optic, and why this one: a small window suits a gun used at the
       range this one is used at, and it comes up a shade slower than the
       EXPS2 for a reason that costs nothing here - a submachine gun is not
       reaching for its glass at forty metres. The rifle carries the same dot
       and the comparison between them is worth having.

       Measured, not chosen: 0.0516 of receiver, printed by
       tools/gun_to_glb.py, plus the dot's own 0.0314 from sights.c, for a
       mount of 0.0830. That is a tall rail - the MP7's receiver is deep,
       because the bolt runs above the barrel - and the number is the mesh's,
       not an estimate.

       Forward is the one free number. The rail runs from 0.02 to 0.35 forward
       and 0.14 puts the clamp behind the ejection port, where a dot goes. */
    .sight = &SIGHT_RED_DOT,
    .rail_height = 0.0516f,
    .rail_forward = 0.14f,

    /* Negative, like every other shouldered gun here, and the same framing
       argument: the dot's 17.7mm window has to come back to the eye far enough
       to be worth looking through. These are the rifle's numbers, and they
       carry across for the reason the rifle's entry gives - the framing
       belongs to the glass, not to the gun, so the same window at the same
       distance through the same frustum reads the same on either. */
    .view_ads_forward = -0.40f,
    .view_ads_fov = 44.0f,

    /* Measured off the mesh by tools/gun_to_glb.py. The muzzle is 0.408
       forward of the back of the receiver, which is where this mesh's origin
       is - not the buttplate, which is another 0.230 behind it with the stock
       out. See the manifest for why the receiver is named on this gun. */
    .muzzle_up = 0.0f,
    .muzzle_forward = 0.408f,

    /* The magazine comes out during a reload. It is one node in the source and
       shares its atlas with the rest of the gun, so the import gives it a
       material of its own purely so that it can be named here - see
       models/weapons/mp7/import.json. There are no loose rounds modelled
       inside it, so rounds_part stays null and the reload drops a closed box,
       which is what this mesh has to show. */
    .magazine_part = "mp7_magazine",
    .rounds_part = NULL,
};

const WeaponSpec WEAPON_SPEC_MP7 = {
    .name = "MP7",
    .kind = WEAPON_CLASS_FIREARM,
    .firearm = &MP7_FIREARM,

    /* Five to a chest, against the rifles' three and the M9's two, and it is
       the only figure in the band that makes this gun what it is. At 25 it
       would be four and it would beat the assault rifle to a kill outright at
       every range the rifle is for; at 17 it would be six and the magazine
       would run out inside two kills. Five rounds of 21 at 950 a minute is
       0.25 seconds, which is a hair behind the rifle and half a second ahead
       of anything the player can do about it up close. */
    .damage = 21.0f,
    .range = 300.0f,

    .mesh_path = "models/weapons/mp7/mp7.glb",

    /* Right and down are the rifle's - a shouldered gun hangs in the corner
       shouldered guns hang in.

       Forward is the long guns' arithmetic, and it is the reason that rule was
       worth stating on the carbine. What stays constant between shouldered
       weapons is where the BUTTPLATE lands, because that is what the shoulder
       reads: the rifle and the carbine both put theirs 0.155 in front of the
       eye, out of a 0.838m gun with its origin 0.225 from the butt and a
       0.745m gun with its origin at 0.158. This mesh's origin is 0.230 from
       its buttplate with the stock extended, so 0.385 lands it in the same
       place, and the 20cm this gun is shorter than the rifle shows up at the
       muzzle, which is where it is. */
    .view_right = 0.148f,
    .view_down = 0.125f,
    .view_forward = 0.385f,

    .neck_follow = 0.30f,

    /* Two points, and the forward one is a real vertical foregrip rather than
       a place on a handguard: the artist modelled the MP7's folding handle
       deployed, as its own node, so the support hand has something to close on
       and it is measured rather than guessed.

       Both come off the mesh in the model's own frame. The pistol grip and the
       magazine are one column on this gun - the magazine feeds through the
       grip - and the hand closes on the top of it, 60mm below the bore at
       0.168 forward. The foregrip hangs from the bore line down to 132mm below
       it at 0.313 forward, and a fist takes the middle of that. */
    .hold = WEAPON_HOLD_TWO_POINT,
    .grip_right = {0.0f, -0.060f, -0.168f},
    .grip_left = {0.0f, -0.075f, -0.313f},

    /* No hidden_parts. The flip-up irons stood 79mm above the bore against a
       dot at 83mm in a window of 8.9mm radius - not a co-witness, not close:
       the rear sight would have filled the glass. They were hidden by name;
       they are dropped at import now, by node, because this gun is one
       material end to end and a node is the only seam in it. See
       models/weapons/mp7/import.json. */

    /* The library's rifle poses are the pistol ones - see the rifle's entry
       for what that costs. On this gun it costs less than on either rifle: the
       hands are closer together on an MP7 than on an M4, which is nearer to
       what the clips were staged for than anything else here. */
    .clips = {{ "Pistol_Aim_Down", "Pistol_Aim_Neutral", "Pistol_Aim_Up" }},
};
