/* Every number a weapon's placement is made of, named once.

   No include guard, deliberately: this file is included more than once, each
   time with the three macros defined differently, and each expansion decides
   what a row means. It is the same bargain weapon_list.h makes. Four things
   read it and so cannot disagree about what a placement is:

     weapon_placement_load     placement.json into the spec
     weapon_placement_emit     the spec back out as a C literal to paste
     weapon_placement_diff     what a probe compares
     the tuning overlay        what the arrow keys move

   Adding a field is one row here.

   Only MEASURED numbers belong in this list - the ones found by squinting at
   a screen or taken off a mesh by the importer, which change when the ASSET
   changes. A magazine size, a rate of fire and a bloom curve are chosen for
   feel, change when the DESIGN changes, and stay in weapon_specs.c where their
   reasons are written down.

   The first argument is which struct the field is on, because the three are
   reached differently: a weapon always has a WeaponSpec, and has at most one
   of a FirearmSpec and a MeleeSpec. A row for a struct a given weapon does not
   carry is skipped rather than written as a zero.

   PLACEMENT_POSE takes a name and a stance separately, because `carries` is
   the one array here and "carries[MELEE_CARRY_GUARD]" is not a key anybody
   wants to type into a JSON file. Its name is what appears there; the stance
   is what indexes the array. */

#ifndef PLACEMENT_F32
#define PLACEMENT_F32(owner, field, doc)
#endif
#ifndef PLACEMENT_VEC3
#define PLACEMENT_VEC3(owner, field, doc)
#endif
#ifndef PLACEMENT_POSE
#define PLACEMENT_POSE(name, stance, doc)
#endif

/* --- where the weapon sits in front of the eye ------------------------- */
PLACEMENT_F32 (WEAPON, view_right,   "camera space, +x right")
PLACEMENT_F32 (WEAPON, view_down,    "camera space, positive is down")
PLACEMENT_F32 (WEAPON, view_forward, "camera space, positive is away")

/* --- where the hands close on it, in the model's own space ------------- */
PLACEMENT_VEC3(WEAPON, grip_right,   "the firing hand, off the mesh")
PLACEMENT_VEC3(WEAPON, grip_left,    "the support hand, off the mesh")

/* --- the gun ----------------------------------------------------------- */
PLACEMENT_F32 (FIREARM, rail_height,      "the rail above the bore, from the importer")
PLACEMENT_F32 (FIREARM, rail_forward,     "where along the rail the clamp sits")
PLACEMENT_F32 (FIREARM, view_ads_forward, "how far aiming pulls it back to the eye")
PLACEMENT_F32 (FIREARM, muzzle_up,        "the barrel's tip, off the bore")
PLACEMENT_F32 (FIREARM, muzzle_forward,   "the barrel's tip, along the bore")

/* --- the blade --------------------------------------------------------- */
PLACEMENT_VEC3(MELEE, blade_base,       "where the edge starts, weapon space")
PLACEMENT_VEC3(MELEE, blade_tip,        "and where it ends")
PLACEMENT_VEC3(MELEE, off_hand,         "the wrist of the hand holding nothing")
PLACEMENT_VEC3(MELEE, off_hand_dir,     "and which way its fingers run")
PLACEMENT_VEC3(MELEE, view_clip_offset, "pulls the clip-driven arms into frame")

/* --- being worn -------------------------------------------------------- */
PLACEMENT_VEC3(MELEE, scabbard_offset, "off the pelvis bone")
PLACEMENT_F32 (MELEE, scabbard_pitch,  "and how it is canted")
PLACEMENT_F32 (MELEE, scabbard_yaw,    "")
PLACEMENT_F32 (MELEE, scabbard_roll,   "")
PLACEMENT_F32 (MELEE, sheathed_offset, "the origin back from the koiguchi")
PLACEMENT_VEC3(MELEE, saya_grip,       "where the off hand holds the scabbard")

/* --- the stances ------------------------------------------------------- */
PLACEMENT_POSE(carry_rest,     MELEE_CARRY_REST,     "down at the side, out of frame")
PLACEMENT_POSE(carry_guard,    MELEE_CARRY_GUARD,    "the one the player looks at")
PLACEMENT_POSE(carry_momentum, MELEE_CARRY_MOMENTUM, "canted out of the way")

#undef PLACEMENT_F32
#undef PLACEMENT_VEC3
#undef PLACEMENT_POSE
