#include "gameplay/weapon_spec.h"
#include "gameplay/sight_spec.h"

#include <stddef.h>

/* The auto pistol. 700rpm, a cone that barely moves, and a kick small enough
   that a full magazine stays on target. It is deliberately the easy gun: it
   asks nothing of the player except that they keep moving. */
static const FirearmSpec AUTO_PISTOL_FIREARM = {
    .magazine = 30,
    .fire_interval = 0.0857f,           /* 700 rounds per minute */
    .reload_time = 2.05f,

    .bloom_min = 0.18f,
    .bloom_max = 1.10f,
    .bloom_per_shot = 0.078f,
    .bloom_recovery = 5.0f,
    .bloom_recovery_delay = 0.11f,
    .ads_bloom_scale = 0.45f,

    .recoil_pitch = 0.26f,
    .recoil_yaw = 0.09f,
    .ads_recoil_scale = 0.6f,

    /* The same optic the marksman's carbine carries - it was the rifle's until
       the two long guns swapped glass, and this line moving is the whole cost
       of that on the pistol's side. Nothing about the sight changes to fit
       here either: only the rail does, which is the point. A pistol has less
       gun under the glass, so its rail is lower, and mounting it is one
       number. 0.0626 of slide plus the sight's own 0.0374 is the 0.100 this
       used to state as a single figure. */
    .sight = &SIGHT_HOLOGRAPHIC,
    .rail_height = 0.0626f,
    .rail_forward = 0.16f,

    /* Negative for the same reason the rifle's is: the sight has to come back
       to the eye far enough that its window is worth looking through. */
    .view_ads_forward = -0.30f,
    .view_ads_fov = 46.0f,
    .muzzle_up = -0.005f,
    .muzzle_forward = 0.56f,
};

const WeaponSpec WEAPON_SPEC_AUTO_PISTOL = {
    .name = "Auto Pistol",
    .kind = WEAPON_CLASS_FIREARM,
    .firearm = &AUTO_PISTOL_FIREARM,

    .damage = 18.0f,
    .range = 300.0f,

    /* No mesh yet: the sidearm is still drawn from boxes. */
    .mesh_path = NULL,
    .view_right = 0.22f,
    .view_down = 0.19f,
    .view_forward = 0.46f,

    /* The rifle's, and for the rifle's reasons - a sidearm is carried in the
       same hands off the same shoulders. See the assault rifle's entry. */
    .neck_follow = 0.30f,

    /* A pistol has nothing forward to hold, so both hands meet at the grip and
       the support hand wraps the firing one. Taken off the boxes the sidearm is
       still drawn from: the grip block sits at z +0.01 spanning y -0.115 to
       +0.005, so a fist closes on it about 0.055 below the bore. */
    .hold = WEAPON_HOLD_HANDS_TOGETHER,
    .grip_right = {0.0f, -0.055f, 0.010f},
    .grip_left = {0.0f, -0.085f, -0.030f},
    /* The library authored these for a pistol, which is the closest thing in
       it to a rifle: both hands meet at the grip, and on these models they
       land on the magwell rather than out on the handguard. */
    .clips = {{ "Pistol_Aim_Down", "Pistol_Aim_Neutral", "Pistol_Aim_Up" }},
};
