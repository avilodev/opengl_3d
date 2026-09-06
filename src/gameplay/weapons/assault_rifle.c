#include "gameplay/weapon_spec.h"
#include "gameplay/sight_spec.h"

#include <stddef.h>

/* The assault rifle. Everything about it trades rate for weight.

   600rpm against the pistol's 700, so a magazine takes three seconds rather
   than two and a half and every round is a decision the player can feel. The
   kick is more than three times the pistol's, which is what actually separates
   the two: the pistol can be held down, and this cannot. It bounces rather
   than climbs - the kick decays fully back to the player's own aim - so the
   answer to it is bursts and not a memorised pull-down.

   In exchange the cone at rest is tighter than the pistol's and the sight
   halves it again, so a controlled burst reaches further than anything the
   sidearm can do. Note that the sight buys that reach without magnifying: it
   is one power, and what it steadies is the gun, not the distance. Fast and
   forgiving up close, slow and precise at range: that is the whole of the
   difference. */
static const FirearmSpec ASSAULT_RIFLE_FIREARM = {
    .magazine = 30,
    .fire_interval = 0.100f,            /* 600 rounds per minute */
    .reload_time = 2.40f,

    /* Tighter at rest than the pistol and quicker to open. Twelve rounds of
       held fire reach the ceiling; the first three or four are near enough
       exact. */
    .bloom_min = 0.14f,
    .bloom_max = 1.35f,
    .bloom_per_shot = 0.105f,
    .bloom_recovery = 5.2f,
    .bloom_recovery_delay = 0.125f,
    .ads_bloom_scale = 0.40f,

    .recoil_pitch = 0.85f,
    .recoil_yaw = 0.22f,
    .ads_recoil_scale = 0.62f,

    /* A tube red dot on the receiver rail. One power: aiming does not make the
       target bigger, it makes the gun honest - centred, steady, and with a
       reticle where the round goes. Everything a magnified optic bought at
       range, this gives back as the room around the shot, which for a game
       whose fights happen in the air off a wall is the better trade. How fast
       it comes up and what it magnifies are the sight's, not the gun's.

       The rifle and the marksman's carbine have swapped glass. The optic is
       not a property of the gun and never was - a rail is a rail, and the
       whole reason sight_spec.h keeps the two halves of a mount apart is that
       either one can change without the other knowing. This is that claim
       being used rather than asserted: nothing about the M4 moved to take a
       different sight, and nothing about the sight moved to fit the M4.

       Measured, not chosen: the M4's rail sits 0.0319 above its bore, printed
       by tools/fbx_to_glb.py. The dot's own 0.0314 is stated once, in
       sights.c, and added in by weapon_spec_sight_height - so the 0.0633 the
       two now come to is nobody's to get wrong, and nobody had to work it out.

       Forward is the one free number: where along the rail it is clamped. At
       0.12 this mount's seat lands between z -0.141 and -0.097, inside the
       stretch the EXPS2 used to cover and receiver rail all the way. */
    .sight = &SIGHT_RED_DOT,
    .rail_height = 0.0319f,
    .rail_forward = 0.12f,

    /* Negative: aiming pulls the rifle back toward the eye rather than pushing
       it out. How much of the screen the window covers is decided here and by
       view_ads_fov, not by the world's field of view - the gun is drawn
       through its own projection.

       Both numbers moved with the glass, and had to. A tube dot's window is
       17.7mm against the EXPS2's 36mm, so at the geometry that framed the
       EXPS2 - 0.16m from the eye at 50 degrees - this one would cover 0.12 of
       the viewport height where that covered 0.24: the same sight picture at
       half scale, which reads as a worse sight rather than a different one.
       Pulling the gun in to 0.10m and closing the frustum to 44 degrees puts
       it back at 0.22. The window is smaller and stays smaller; what these two
       fix is that it is not ALSO further away. */
    .view_ads_forward = -0.40f,
    .view_ads_fov = 44.0f,
    /* Measured off the mesh by tools/fbx_to_glb.py, which prints these when
       it runs. The bore is the model's own y = 0, so the flash sits on it. */
    .muzzle_up = 0.0f,
    .muzzle_forward = 0.613f,           /* a longer barrel than the pistol */

    /* The magazine comes out during a reload, so it is drawn apart from the
       rest of the gun. Both names are off the mesh - see the parts list the
       model loader prints. */
    .magazine_part = "M4Mag_Mat",
    .rounds_part = "Bullet_Mat",
};

const WeaponSpec WEAPON_SPEC_ASSAULT_RIFLE = {
    .name = "Assault Rifle",
    .kind = WEAPON_CLASS_FIREARM,
    .firearm = &ASSAULT_RIFLE_FIREARM,

    .damage = 47.0f,
    .range = 300.0f,

    .mesh_path = "models/weapons/m4_carbine/m4_carbine.glb",
    /* The gun on screen, about 1.6x the size it used to be. Two thirds of
       that is the narrower viewmodel projection and one third is the gun
       sitting closer to the eye, because neither would do it alone: this
       mesh is 0.838m long with its origin at the back of the receiver, so
       pulling it in far enough to carry the whole magnification on its own
       puts the buttplate through the 0.02m near plane and opens the stock
       up like a cutaway. Right and down are re-derived from the new
       projection rather than kept, so the gun grew where it already sat
       instead of walking off the corner of the screen as it grew. */
    .view_right = 0.148f,
    .view_down = 0.125f,
    .view_forward = 0.38f,

    /* How much of the head's pitch the gun gives up to the neck - see
       render/viewmodel_neck.h, and weapon_spec.h for why this is a scalar.

       A third, which is the number this whole field exists to make arguable.
       At 1.0 the rifle is a rifle held by a body, and it is also a rifle that
       leaves the frame every time the player looks up a wall to grapple it,
       which in this game is constantly. At 0.0 it is exactly what shipped
       before the neck existed. A third keeps most of what the motion buys -
       the hands rising into frame on a look down, the barrel dipping on a look
       up - while a look at the sky still leaves a gun on screen. The aim fades
       it out entirely, so nothing here reaches the sights. */
    .neck_follow = 0.30f,

    /* Measured off the mesh. The pistol grip hangs behind the magwell around
       z +0.03 and reaches down to y -0.153, so the middle of it - which is what
       a fist closes on - is about 0.09 below the bore. The handguard is a 0.06m
       tube running z -0.20 to -0.44 with its axis on the bore, so mid-handguard
       is z -0.33 and the support wrist sits just under it at -0.06. */
    .hold = WEAPON_HOLD_TWO_POINT,
    .grip_right = {0.010f, -0.100f, 0.0f},
    .grip_left = {0.0f, -0.060f, -0.330f},

    /* No hidden_parts. The irons used to be listed here and hidden at draw
       time; they are dropped at import now, so there is no name left to hide.
       See models/weapons/m4_carbine/import.json. */
    /* The library authored these for a pistol, which is the closest thing in
       it to a rifle: both hands meet at the grip, and on these models they
       land on the magwell rather than out on the handguard. */
    .clips = {{ "Pistol_Aim_Down", "Pistol_Aim_Neutral", "Pistol_Aim_Up" }},
};
