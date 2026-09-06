#include "gameplay/weapon_spec.h"
#include "gameplay/movement_tuning.h"

#include <stddef.h>

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
/* Two of these keys are anchored to the SCABBARD rather than to the view, and
   say so by adding VIEWMODEL_HIP_BEHIND_EYE to their depth: the grip at the
   koiguchi and the point out along the blade's axis are both facts about where
   the sheath is, and the sheath sits behind the eye as well as below it.

   Stated as the constant rather than folded into the literal so they move with
   it. They did not, once, and the copy of the scabbard placement in
   tools/sheath_probe.c did not either, so a draw whose first grip missed the
   sword by 25cm passed its own check. There is no ACTION_ON_SCABBARD frame to
   put them in - the honest fix - and this is what stands in for one. */
static const ActionKey KATANA_DRAW_KEYS[] = {
    /* The hand ON the tsuka, which is where a draw starts from now: a worn
       sword is carried with the weapon hand already on the handle, so a track
       that began by the side would snap the hand across to the hilt on the
       frame the draw started. Same point as the grip key below - the beat
       between them is the hand taking hold before it pulls. */
    { .at = 0.00f, .ease = ACTION_EASE_SMOOTH,
      .hand_frame = ACTION_IN_VIEW,
      .hand = { -0.143f, -0.752f, -0.170f + VIEWMODEL_HIP_BEHIND_EYE } },
    /* Down and across to the tsuka, and closed on it. */
    { .at = 0.28f, .ease = ACTION_EASE_SMOOTH,
      .hand_frame = ACTION_IN_VIEW, .hand = { -0.143f, -0.752f, -0.170f + VIEWMODEL_HIP_BEHIND_EYE } },
    /* Out along the blade's own axis, the whole 0.79m of it. SNAP because a
       draw's energy is all at the start: the arm is already moving when the
       blade begins to leave, and it slows as the tip comes clear. */
    { .at = 0.62f, .ease = ACTION_EASE_SNAP,
      .hand_frame = ACTION_IN_VIEW, .hand = { -0.265f, -0.877f, 0.604f + VIEWMODEL_HIP_BEHIND_EYE } },
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
      .hand_frame = ACTION_IN_VIEW, .hand = { -0.265f, -0.877f, 0.604f + VIEWMODEL_HIP_BEHIND_EYE } },
    /* Home. */
    { .at = 0.86f, .ease = ACTION_EASE_DRIVE,
      .hand_frame = ACTION_IN_VIEW, .hand = { -0.143f, -0.752f, -0.170f + VIEWMODEL_HIP_BEHIND_EYE } },
    /* And the hand stays ON it, which is how a worn sword is carried - see
       the draw's first key. */
    { .at = 1.00f, .ease = ACTION_EASE_SMOOTH,
      .hand_frame = ACTION_IN_VIEW,
      .hand = { -0.143f, -0.752f, -0.170f + VIEWMODEL_HIP_BEHIND_EYE } },
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

    .scabbard_path = "models/weapons/katana/katana_saya.glb",
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

    /* 15cm down the saya from its mouth. The right hand's grip sits at
       sheathed_offset - grip_right along the same axis, about 5cm ABOVE the
       mouth on the tsuka, so the two fists end up roughly 20cm apart on one
       line: the left low on the scabbard, the right up on the handle. */
    .saya_grip = { 0.0f, 0.0f, -0.15f },

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
    /* Camera space: +x right, +y up, -z forward, rotations applied about the
       sword's own origin as yaw, then pitch, then roll. The origin is the
       middle of the tsuka, so this places the HAND and the blade follows out
       of it - a positive pitch raises the tip, because the mesh runs -z toward
       it, so a steeply negative one hangs the blade at the floor.

       One hold, stated once and named three times. The three stances are what
       the blend interpolates between and they are deliberately identical: the
       sword is carried in the right hand, tip down, and there is no version of
       that which a wall run or an idle clock should change yet. The machinery
       stays because differentiating them later is editing these numbers rather
       than rebuilding the blend - and while they agree, nothing on screen
       moves when the stance does.

       Down at the right side, tip toward the floor, canted a little forward
       and inward so the blade is foreshortened rather than edge-on and reads
       as a blade. The tsuka and the tsuba sit low on the right of the frame
       and the rest of it runs off the bottom, which is where a sword carried
       at rest in one hand actually is.

       The hand sits at -0.30, two centimetres off ARMS_SHOULDER_DOWN: on the
       shoulder line, where a hand carrying something at rest is. That is the
       number this pose is anchored to and the one that was wrong before, at
       -0.05 - a hand authored 27cm ABOVE its own shoulder is the sword being
       held up under the chin, whatever the pitch below it says.

       Worth knowing what the shoulder line costs, because it is not free and
       nothing else in this file pays it: a hand exactly on it can only be on
       screen at all if the arm can span SH*(1/tan(fov/2) + 1/tan(fov/2+4)),
       which at this frustum is 0.85m against the 0.43m a comfortable arm has.
       The rifle answers that by sitting 20cm above the line and the pistol the
       same. This pose gets away with being ON it because the off hand is
       pushed out far enough to drag the whole shoulder line forward to -0.10 -
       which is also what brings the sword arm back to 0.75 of its reach from
       the 0.87 it sits at when the idle hand is tucked in.

       Thirty-two degrees below horizontal rather than straight down, and the
       angle answers a measured cliff rather than a taste. With the hand on the
       shoulder line the frame ends just under it, so whatever the blade does
       below that is off screen: at 30 degrees the whole blade is in, at 34 the
       tip is on the edge, by 40 there is 18% left and straight down there is
       nothing but a fist. What keeps it in shot is that the tip lands 1.2m out
       - perspective, not a shallower sword.

       On screen: the hand at 0.42 across and -0.93 down, the tsuba at 0.29,
       -0.93 and the tip at 0.03, -0.93. The sword lies low across the bottom
       of the frame from the right hand toward the centre, tip toward the
       ground ahead. */
    .carries = {
        [MELEE_CARRY_REST]     = { 0.24f, -0.30f, -0.46f, -32.0f, 15.0f, 0.0f,
                                   0.0f, 0.0f },
        [MELEE_CARRY_GUARD]    = { 0.24f, -0.30f, -0.46f, -32.0f, 15.0f, 0.0f,
                                   0.0f, 0.0f },
        [MELEE_CARRY_MOMENTUM] = { 0.24f, -0.30f, -0.46f, -32.0f, 15.0f, 0.0f,
                                   0.0f, 0.0f },
    },

    /* The left hand, which is holding nothing and is drawn anyway - the body
       has two arms and one of them being absent is louder than it being idle.
       Down and out on its own side, clear of the blade, fingers hanging
       forward and down the way an unoccupied hand does. A wrist, not a grip:
       there is nothing here for a fist to close on.

       On the shoulder line like the other one, and pushed further out than
       mirroring it would: that is what pulls the solved shoulder line forward
       and takes the sword arm off full stretch. An idle hand is doing nothing,
       so where it does nothing is free to be useful. In frame at -0.51 across
       and -0.85 down - the hand is idle, not missing, and a viewmodel showing
       one arm reads as a character with one arm. */
    .off_hand = { -0.34f, -0.32f, -0.54f },
    .off_hand_dir = { 0.05f, -0.86f, -0.51f },

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

    .mesh_path = "models/weapons/katana/katana.glb",
    /* Copies of the rifle's, so the sword is on screen and roughly where a
       held thing goes. Tuned in Task 8, once it is actually on the hand
       socket and there is something to tune against. */
    .view_right = 0.148f,
    .view_down = 0.125f,
    .view_forward = 0.38f,
    /* Wider than the rifle's 43, and this is the number that decides whether a
       sword can be carried at all.

       A viewmodel FOV is how much of the frame the weapon's own frustum shows,
       and the world is drawn at VIEW_BASE_FOV, 75. A gun is held near the view
       axis, so 43 costs it nothing and buys it a receiver that reads large.
       A sword is held DOWN, and down is where a narrow frustum has no room:
       this body's own hand in Sword_Idle sits 0.90m below the eye and 0.65m in
       front of it, which is 54 degrees below the axis - off the bottom of any
       frustum, 90 degrees included. So the first-person hold is a cheat and
       always was; what 43 did was make it a big enough cheat to look like one,
       with the hand pulled up to within 5cm of eye level to stay in shot.

       At 70 the hand sits 0.71 of the way down the frame at 21cm below the
       eye. Still a cheat - four times less of one, and low in frame, which is
       where a hand carrying something belongs. */

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
