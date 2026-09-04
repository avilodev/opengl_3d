#include "gameplay/weapon_spec.h"
#include "gameplay/sight_spec.h"

#include <stddef.h>

/* The tuning surface for the guns themselves. A number that changes how a
   weapon feels belongs in this file. */

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

    /* The same optic the rifle carries. Nothing about the sight changes to
       fit here - only the rail does, which is the point: a pistol has less gun
       under the glass, so its rail is lower, and mounting it is one number.
       0.0626 of slide plus the sight's own 0.0374 is the 0.100 this used to
       state as a single figure. */
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
    .view_fov = 55.0f,

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

    /* An EOTech EXPS2 on the receiver rail. One power: aiming does not make
       the target bigger, it makes the gun honest - centred, steady, and with
       a reticle where the round goes. Everything a magnified optic bought at
       range, this gives back as the room around the shot, which for a game
       whose fights happen in the air off a wall is the better trade. How fast
       it comes up and what it magnifies are the sight's, not the gun's.

       Measured, not chosen: the M4's rail sits 0.0319 above its bore, printed
       by tools/fbx_to_glb.py. The sight's own 0.0374 is stated once, in
       sights.c, and added in by weapon_spec_sight_height - so the 0.0693 this
       used to carry as a hand-summed figure is now nobody's to get wrong. The
       front iron post ends up a hair under the reticle, which is what a
       co-witness looks like and is the check that the two heights are right.

       Forward is the one free number: where along the rail it is clamped.
       At 0.12 the mount seat lands between z -0.143 and -0.053, which is
       receiver rail all the way. */
    .sight = &SIGHT_HOLOGRAPHIC,
    .rail_height = 0.0319f,
    .rail_forward = 0.12f,

    /* Negative: aiming pulls the rifle back toward the eye rather than pushing
       it out. A red dot's window is 36mm across, and how much of the screen
       that covers is decided here and by view_ads_fov, not by the world's
       field of view - the gun is drawn through its own projection. At 0.16m
       from the eye and 50 degrees the window is a bit over a third of the
       viewport height, which is a sight picture you aim with rather than
       peer at. */
    .view_ads_forward = -0.34f,
    .view_ads_fov = 50.0f,
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

    .mesh_path = "models/Guns/M4/m4_carbine.glb",
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
    .view_fov = 43.0f,

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
    .grip_right = {0.0f, -0.090f, 0.030f},
    .grip_left = {0.0f, -0.060f, -0.330f},

    /* The holographic sight is clamped over the front post, so the irons are
       furniture nobody can see past the optic - and what little of them shows
       pokes up through the glass. Off, by name, from the gun's own mesh. */
    .hidden_parts = {"IronSightMat"},
    /* The library authored these for a pistol, which is the closest thing in
       it to a rifle: both hands meet at the grip, and on these models they
       land on the magwell rather than out on the handguard. */
    .clips = {{ "Pistol_Aim_Down", "Pistol_Aim_Neutral", "Pistol_Aim_Up" }},
};

/* Getting the katana out of the saya and back into it, in camera space.

   Three facts fix these numbers and only the fourth is taste. The koiguchi is
   where the spec's own scabbard offsets put it, taken through the half turn
   the arms take. The blade axis is where the cant points it, which for this
   cant is back, left and a little down. And the blade is 0.79m long, so the
   grip has to travel that whole distance along that axis before the tip is
   clear - there is no shortcut, and a draw that swings the sword forward
   before then is a blade going through the scabbard wall. The ready pose is
   where Sword_Idle puts the hand, so the track hands over to the hold with
   nothing to cross-fade.

   That the middle of a katana draw goes behind the camera is not a mistake in
   the numbers. It is where the sword is: it is beside and behind the hip, and
   it comes forward only once it is out. */
static const ActionKey KATANA_DRAW_KEYS[] = {
    /* The hand at rest by the side, which is where a draw starts from. */
    { .at = 0.00f, .ease = ACTION_EASE_SMOOTH,
      .hand_frame = ACTION_IN_VIEW, .hand = { 0.10f, -0.85f, -0.20f } },
    /* Down and across to the tsuka, and closed on it. */
    { .at = 0.28f, .ease = ACTION_EASE_SMOOTH,
      .hand_frame = ACTION_IN_VIEW, .hand = { -0.143f, -0.752f, -0.170f } },
    /* Out along the blade's own axis, the whole 0.79m of it. SNAP because a
       draw's energy is all at the start: the arm is already moving when the
       blade begins to leave, and it slows as the tip comes clear. */
    { .at = 0.62f, .ease = ACTION_EASE_SNAP,
      .hand_frame = ACTION_IN_VIEW, .hand = { -0.265f, -0.877f, 0.604f } },
    /* And round into the ready pose, which is where Sword_Idle holds it. */
    { .at = 1.00f, .ease = ACTION_EASE_SMOOTH,
      .hand_frame = ACTION_IN_VIEW, .hand = { 0.203f, -0.652f, -0.058f } },
};

static const ActionTrack KATANA_DRAW = {
    .keys = KATANA_DRAW_KEYS,
    .count = (int)(sizeof KATANA_DRAW_KEYS / sizeof KATANA_DRAW_KEYS[0]),
};

/* The same in reverse, and not merely the same played backwards: sheathing is
   the harder motion of the two and the beat that says so is the tip finding
   the mouth. That key gets DRIVE - it exists in order to hit something - and
   the run down into the scabbard after it is one long smooth push. */
static const ActionKey KATANA_SHEATHE_KEYS[] = {
    { .at = 0.00f, .ease = ACTION_EASE_SMOOTH,
      .hand_frame = ACTION_IN_VIEW, .hand = { 0.203f, -0.652f, -0.058f } },
    /* Sword brought back and lined up with the mouth, tip first. */
    { .at = 0.42f, .ease = ACTION_EASE_SMOOTH,
      .hand_frame = ACTION_IN_VIEW, .hand = { -0.265f, -0.877f, 0.604f } },
    /* Home. */
    { .at = 0.86f, .ease = ACTION_EASE_DRIVE,
      .hand_frame = ACTION_IN_VIEW, .hand = { -0.143f, -0.752f, -0.170f } },
    /* And the hand off it, back to the side. */
    { .at = 1.00f, .ease = ACTION_EASE_SMOOTH,
      .hand_frame = ACTION_IN_VIEW, .hand = { 0.10f, -0.85f, -0.20f } },
};

static const ActionTrack KATANA_SHEATHE = {
    .keys = KATANA_SHEATHE_KEYS,
    .count = (int)(sizeof KATANA_SHEATHE_KEYS / sizeof KATANA_SHEATHE_KEYS[0]),
};

/* The katana. Its landmarks come from tools/katana_to_glb.py, which prints
   them every time it runs: the origin is the middle of the tsuka, the guard
   sits at z -0.1131, the tip at z -0.9069, and the saya is 0.7907m deep. */
static const MeleeSpec KATANA_MELEE = {
    /* The cutting edge only: from just forward of the guard to the tip. The
       last few centimetres before the guard are the habaki and the collar,
       which nothing is cut with. */
    .blade_base = { 0.0f, 0.0f, -0.14f },
    .blade_tip  = { 0.0f, 0.0f, -0.907f },

    .scabbard_path = "models/Swords/Katana/katana_saya.glb",
    /* Left hip, edge up, blade running back along it so the tsuka sits forward
       where the right hand can cross to it.

       The rig's own left is +X, measured off it rather than assumed: thigh_l
       and hand_l both sit at positive x in the bind pose. Stated in the rig's
       frame, and the first-person draw takes the same numbers through the same
       half turn the arms do, so one set of angles means one physical thing in
       both views.

       The cant puts the blade at (+0.15, -0.16, -0.98) in rig space: back,
       and a little down and out, which is how one hangs. */
    .scabbard_offset = { 0.16f, -0.02f, 0.06f },
    .scabbard_pitch = -9.0f, .scabbard_yaw = -9.0f, .scabbard_roll = 180.0f,

    /* The guard sits at z -0.1131 on this mesh, so a sword whose guard is at
       the mouth has its origin that far back from it. The blade then reaches
       0.7939m inside a saya that is 0.7907m deep, which is in as far as it
       goes. */
    .sheathed_offset = 0.1131f,

    .attacks = {
        /* 0-2: the light chain. The active windows are the middle of each
           clip - a swing is live while the edge is moving, not while the arms
           are winding up or following through. Set by eye against the clip
           and checked by tools/melee_probe.c. */
        { "Sword_Regular_A", "Sword_Regular_A_Rec", 0.30f, 0.55f, 1.0f,  1 },
        { "Sword_Regular_B", "Sword_Regular_B_Rec", 0.28f, 0.52f, 1.0f,  2 },
        /* C ends the chain. The library ships no Sword_Regular_C_Rec, which
           is why this one recovers into the hold pose rather than into a clip
           that does not exist. */
        { "Sword_Regular_C", NULL,                  0.32f, 0.60f, 1.2f, -1 },
        /* 3: the heavy. Slower to arrive and worth more. */
        { "Sword_Heavy_Combo", NULL, 0.40f, 0.70f, 2.0f, -1 },
        /* 4: the block. A hold, not a swing: active_to below active_from is
           what says it has no live window at all, rather than a zero-length
           one at the start that a rounding error could open. */
        { "Sword_Block", NULL, 1.0f, 0.0f, 0.0f, -1 },
    },
    .attack_count = 5,
    .light_attack = 0, .heavy_attack = 3, .block_attack = 4,

    /* Nothing, and that is the answer rather than an unfinished tuning pass.

       This used to be forward 0.40 and up 0.22, sized to drag a wind-up that
       goes behind the shoulder back into frame. It bought that by putting the
       shoulder line 40cm in FRONT of the camera, which is not a place a
       shoulder can be: the upper arms came out ahead of the eye and the
       forearms lay across the near plane, and what filled the screen was not a
       badly placed arm but a solid wall of one. The anchor is the eye now -
       see viewmodel_clip_arms_eye - and the rig's own neck and clavicles put
       the shoulders where they belong without being told.

       So what is left for this field is a genuine per-weapon nudge, and the
       katana wants none. A wind-up that carries the hand behind the eye is
       allowed to: that is where a wind-up goes, and hiding it by shoving the
       body forward hid the arms as well. Applied to the ARMS as well as the
       sword - a shift given to one and not the other is how a hilt ends up
       floating in front of a fist reaching for where it used to be. */

    .draw_track = &KATANA_DRAW,
    .sheathe_track = &KATANA_SHEATHE,
    /* Fast enough to be a commitment rather than a cutscene, slow enough that
       the sword is seen leaving the sheath. The sheathe is longer because it
       is the harder motion and looks careless done quickly. */
    .draw_time = 0.70f,
    .sheathe_time = 0.85f,

    .view_clip_offset = { 0.0f, 0.0f, 0.0f },
};

const WeaponSpec WEAPON_SPEC_KATANA = {
    .name = "Katana",
    .kind = WEAPON_CLASS_MELEE,
    .melee = &KATANA_MELEE,

    /* One clean cut is most of a person. The range is the reach of the arm
       and the blade together, and is what the swing sweep is bounded by. */
    .damage = 55.0f,
    .range = 2.0f,

    .mesh_path = "models/Swords/Katana/katana.glb",
    /* Copies of the rifle's, so the sword is on screen and roughly where a
       held thing goes. Tuned in Task 8, once it is actually on the hand
       socket and there is something to tune against. */
    .view_right = 0.148f,
    .view_down = 0.125f,
    .view_forward = 0.38f,
    .view_fov = 43.0f,

    /* Free, where the rifle's is a third.

       This was the rifle's number, held as a placeholder by a comment that laid
       out the argument for a larger one and then declined to make it: the honest
       amount "cannot be chosen until it has a carry worth looking at". There is
       one now - three solved stances on the hilt this spec names, in a fist the
       third-person body closes on the same point.

       What holds a GUN's neck down is the sight. A gun that follows its body too
       far takes the optic off the crosshair on the way up, and the aim fade
       exists to rescue exactly that at the one moment it matters. A sword is
       owed no such compromise, so what is left is only whether the blade stays
       on screen - and it does not, at a look up, and should not. The hands are
       at a guard in front of a chest, and a chest does not follow an eye to the
       ceiling. Looking up takes the sword out of frame and looking down brings
       it back, which is the motion of carrying one rather than of aiming it.

       Scales the turn as well as the nod. The yaw has its own, much shorter
       saturation - see VIEWMODEL_NECK_YAW_LIMIT - so a free neck here is a head
       that pitches off its body as far as it likes and leads a turn only by as
       much as the body needs to catch up. */
    .neck_follow = 1.0f,

    /* Two fists in a row along the tsuka, which is its own hold: neither hand
       has a pistol grip to close on and both do the same job.

       Measured off the mesh rather than copied off the rifle, which is what
       these used to be. The origin is the middle of the tsuka and the guard is
       at z -0.1131, so the grip runs from there back to about +0.113 and the
       two fists sit either side of the middle, a fist's width apart: the right
       hand forward under the tsuba, the left at the kashira. On the hilt's own
       axis and not below it - a sword has nothing hanging under its grip for a
       hand to drop onto, which is the whole difference from a pistol grip. */
    .hold = WEAPON_HOLD_HILT,
    .grip_right = {0.0f, 0.0f, -0.062f},
    .grip_left = {0.0f, 0.0f, 0.062f},
    /* One pose, named three times. A sword has no aim pitch to blend along, so
       the trio collapses rather than the blend growing a special case. */
    .clips = {{ "Sword_Idle", "Sword_Idle", "Sword_Idle" }},
};
