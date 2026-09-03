#ifndef WEAPON_SPEC_H
#define WEAPON_SPEC_H

#include "gameplay/sight_spec.h"
#include "gameplay/viewmodel_pose.h"
#include "gameplay/reload_sequence.h"

/* Everything that makes one gun feel different from another, as data. The
   weapon code holds a pointer to one of these and reads it; it has no numbers
   of its own. Adding a gun is a new entry in weapon_specs.c, not a new branch
   anywhere.

   Only what genuinely varies between weapons lives here. How fast the view
   settles after a kick, how long a tracer is drawn for and how far the
   viewmodel bobs are properties of the camera and the renderer, and they stay
   in weapon_tuning.h. What belongs to the optic rather than to the gun - how
   far its reticle stands above its own clamp, how wide its glass is, what it
   magnifies - is on a SightSpec, and this file points at one.

   That split is what makes an optic reusable. A mount height is the gun's rail
   above its bore plus the sight's reticle above its rail seat, and the two are
   measured off two different meshes by two different importers. Added together
   by hand into one field, as this used to be, the sight's half is welded into
   the gun's and every new gun re-derives it with nothing to check against.
   Stated apart and summed by weapon_spec_sight_height, either half can change
   without the other knowing. */

/* Enough for the furniture one gun wants off. Raising it costs a pointer per
   spec and nothing else. */
#define WEAPON_MAX_HIDDEN_PARTS 4

/* What kind of weapon this is, which is the one branch the rest of the system
   is allowed to take on a weapon. Everything else a class needs is on the half
   this points at, so adding a third is a struct and a case in the handful of
   places that switch here - not a field on every weapon that most weapons
   leave zero.

   The field is `kind` rather than `class` for the reason every other enum in
   this file is: `class` is a C++ keyword, and a header that cannot be included
   from C++ is a header with a trap in it. */
typedef enum {
    WEAPON_CLASS_FIREARM,
    WEAPON_CLASS_MELEE
} WeaponClass;

/* Everything that is true of a gun and of nothing else.

   Lifted out of WeaponSpec unchanged. A katana has no magazine, no cone and no
   rail, and leaving those on the common struct would mean every melee weapon
   states a magazine size of zero and every reader has to wonder whether the
   zero means something. Behind a pointer, a weapon that is not a gun does not
   answer the question at all - which is the difference between a field that is
   empty and a field that does not apply. */
typedef struct {
    /* --- ammunition and rate --- */
    int magazine;
    float fire_interval;        /* seconds between rounds */
    float reload_time;

    /* --- the cone ---
       Sits at `bloom_min`, grows by `bloom_per_shot` per round to `bloom_max`,
       and recovers at `bloom_recovery` degrees a second once the trigger has
       been quiet for `bloom_recovery_delay`. That delay is what separates held
       fire from tapped fire, so it wants to be a little longer than one round. */
    float bloom_min;
    float bloom_max;
    float bloom_per_shot;
    float bloom_recovery;
    float bloom_recovery_delay;
    float ads_bloom_scale;

    /* --- the kick ---
       Composed into the look angles and decayed fully back out, so it is a
       bounce and never a pull the player has to hold against. */
    float recoil_pitch;
    float recoil_yaw;
    float ads_recoil_scale;

    /* --- the rail ---
       Which optic is bolted on, and where its rail is. Any SightSpec fits any
       rail; what differs between guns is these two numbers.

       `rail_height` is the gun's own half of the mount height, printed by
       tools/fbx_to_glb.py, and it knows nothing about which sight is clamped
       to it. `rail_forward` is the one free number here: where along the rail
       the clamp sits. Ask weapon_spec_sight_height for the sum. */
    const SightSpec* sight;
    float rail_height;
    float rail_forward;

    /* How far aiming pulls the gun back toward the eye. A property of the hold
       and not of the optic: a holographic sight has effectively unlimited eye
       relief, so what sets the distance from the eye to the glass is whether
       the gun is shouldered or held out at arm's length. */
    float view_ads_forward;
    float view_ads_fov;

    /* The barrel's tip within the model, where the flash is drawn and a tracer
       starts. Forward is positive, toward what the player is looking at. */
    float muzzle_up;
    float muzzle_forward;

    /* --- the detachable magazine ---
       Material names, and NULL on a gun whose magazine does not come out on
       screen. `magazine_part` is the box itself and `rounds_part` the
       cartridges visible in it, stated apart because a spent magazine leaves
       without them and a fresh one arrives carrying them - which is most of
       what says a magazine was CHANGED rather than refilled where it sat, and
       costs nothing, since they are already separate materials on the mesh.

       Naming these is what lets the reload draw the magazine somewhere other
       than on the gun. Without them the renderer would have to know that this
       particular rifle keeps its magazine in a material called M4Mag_Mat,
       which is exactly the knowledge that belongs on the spec. */
    const char* magazine_part;
    const char* rounds_part;
} FirearmSpec;

/* Everything that is true of a melee weapon and of nothing else.

   The blade segment is the whole of what a swing means geometrically: two
   points in the weapon's own frame, swept through the transform between one
   frame and the next. Stated here rather than derived from the mesh bounds
   because a bounding box includes the grip and the guard, and a sword that
   cut with its handle would be a surprise nobody debugged quickly. */
typedef struct MeleeSpec {
    float blade_base[3];
    float blade_tip[3];

    /* The scabbard: its own mesh, worn on the pelvis rather than held. A
       katana is worn thrust through the belt on the left hip, edge up, tsuka
       forward - so this is an offset and a cant off that bone, stated once and
       read by both views. */
    const char* scabbard_path;
    float scabbard_offset[3];
    float scabbard_pitch, scabbard_yaw, scabbard_roll;

    /* Where the sword sits inside the scabbard, along the blade's own axis
       from the koiguchi. A curved blade leaves along its axis and cannot come
       out sideways, which is what makes this one number rather than a pose. */
    float sheathed_depth;

    /* Pulls the clip-driven arms into frame. Those clips were staged for a
       camera behind the body; in first person the same motion needs bringing
       toward the eye. Camera space, same axes as view_right/view_down. */
    float view_clip_offset[3];
} MeleeSpec;

typedef enum {
    /* Two hands at two places: one closed on the grip, the other out on a
       handguard or foregrip. A rifle. */
    WEAPON_HOLD_TWO_POINT,
    /* Two hands at one place, the support hand wrapped around the firing one.
       A pistol, which has no forward furniture to hold. */
    WEAPON_HOLD_HANDS_TOGETHER
} WeaponHoldKind;

typedef struct {
    const char* name;

    /* What kind of weapon this is, and the half that says what that means.
       Exactly one of the two pointers is non-NULL, and it is the one `kind`
       names - checked by tools/weapon_spec_probe.c, because a spec that gets
       this wrong is a null dereference at the first shot rather than a
       compile error. Reach them through weapon_spec_firearm and
       weapon_spec_melee rather than by reading the pointer. */
    WeaponClass kind;
    const FirearmSpec* firearm;
    const MeleeSpec* melee;

    /* Flat at every distance. No weapon in this game has falloff. */
    float damage;
    float range;

    /* The weapon's own mesh, in the viewmodel's axes and in metres, or NULL
       for a weapon that has none yet and falls back to the boxes.

       Here rather than behind an enum in the renderer because an enum standing
       in for a file path is a code edit every time an artist hands one over -
       which is exactly what the first sword cost, and exactly the kind of
       change this file exists to make unnecessary.

       What the mesh's ORIGIN is depends on the class. A gun's sits on the bore
       at the back of the receiver, because that is what a muzzle flash and a
       tracer hang off; a sword's sits in the middle of its grip, because a
       sword has no bore and what it hangs off is the hand. Either way it is
       the frame every other number on this spec is measured in. */
    const char* mesh_path;

    /* --- the viewmodel ---
       Camera-space offsets: +x right, +y up, -z forward. */
    float view_right;
    float view_down;
    float view_forward;
    float view_fov;

    /* --- the hold ---
       Where the hands close on this gun, in the model's own space: +x right,
       +y up, -z forward, origin on the bore at the back of the receiver. The
       same frame the muzzle and the rail are measured in, and measured the same
       way - off the mesh, not chosen.

       These are the gun's half of the first-person arms, exactly as rail_height
       is its half of the sight mount. The arms themselves - how long they are,
       how an elbow breaks, where the shoulders end up - belong to the body and
       live in the renderer; what belongs here is only where this particular
       gun is held. A new gun states two points and a hold, and the shoulder
       placement that reaches them is solved rather than tuned.

       `hold` says what kind of grip it is, which is what the wrists are aimed
       from: a rifle's support hand runs along a handguard, and a pistol's wraps
       the firing hand instead. */
    WeaponHoldKind hold;
    float grip_right[3];
    float grip_left[3];

    /* --- parts of the mesh this gun does not draw ---
       Material names from its own model file, NULL-terminated. A glTF mesh
       arrives split one primitive per material, so these names are the parts
       list of the asset and the finest cut available without re-exporting it.

       Here rather than in the renderer because it is a fact about a particular
       gun and not about how guns are drawn: this rifle wears a holographic
       sight bolted over its front post, so its irons are furniture the player
       would only see clipping through the optic. A gun with no optic states
       nothing and keeps everything.

       Names and not indices, because an index is whatever order the exporter
       happened to write and a name survives a re-export. A name that matches
       nothing is reported at load rather than ignored, since the usual cause is
       an asset that came back with its materials renamed. */
    const char* hidden_parts[WEAPON_MAX_HIDDEN_PARTS];

} WeaponSpec;

/* Fast, light, and forgiving: the sidearm this project started with. Kept
   because it is a good gun, not yet handed to the player. */
extern const WeaponSpec WEAPON_SPEC_AUTO_PISTOL;

/* The issued weapon. Slower and heavier, and unmagnified by choice. */
extern const WeaponSpec WEAPON_SPEC_ASSAULT_RIFLE;

/* The melee alternative: a katana, worn on the hip and drawn to use. */
extern const WeaponSpec WEAPON_SPEC_KATANA;

/* The gun half, or NULL on a weapon that is not a gun.

   Callers that already know perfectly well they are holding a firearm still
   come through here rather than reading spec->firearm, so that the one place
   which would crash on a melee weapon is the one place that gets the check.
   Every reader of a gun-only number starts with this and a NULL test. */
const FirearmSpec* weapon_spec_firearm(const WeaponSpec* spec);

/* The melee half, or NULL on a weapon that is not one. Same bargain as
   weapon_spec_firearm, read from the other side. */
const MeleeSpec* weapon_spec_melee(const WeaponSpec* spec);

/* The mount height: the gun's rail above its bore plus the sight's reticle
   above its rail seat. Aiming drops the gun by exactly this, which is what
   puts the reticle on the middle of the screen instead of near it, and the
   optic mesh is drawn about exactly this point, so the metal and the aim point
   cannot disagree. One function, so there is no second place for the sum to be
   written down and fall out of step. */
float weapon_spec_sight_height(const WeaponSpec* spec);

/* Where along the bore the reticle sits. The gun's clamp point; the sight has
   no say in it. */
float weapon_spec_sight_forward(const WeaponSpec* spec);

/* The field of view aiming settles at. An unmagnified sight leaves it alone,
   which is not a special case but the common one. */
float weapon_spec_ads_fov(const WeaponSpec* spec);

/* The VIEWMODEL's own projection when aimed - a different number from the one
   above, which is the world's. The weapon is drawn through its own frustum, so
   how much of the screen a 36mm window covers is decided here and not by what
   the world is doing.

   A weapon that does not aim answers with its resting field of view, so a
   caller can lerp toward it on an `ads` that never leaves zero and get the
   rest pose back rather than a hole. */
float weapon_spec_view_ads_fov(const WeaponSpec* spec);

/* How long the sight takes to come up. The glass's weight, so it travels with
   the optic between guns. */
float weapon_spec_ads_time(const WeaponSpec* spec);

/* The sprint carry: the gun in against the chest, canted over with the muzzle
   down and across the body.

   One pose for every weapon today, and the spec is taken anyway. That is the
   whole point of the accessor - how a gun is carried is a property of the gun,
   so when one wants its own this returns spec->sprint_pose and not one caller
   changes, because no caller ever knew where the pose came from. It is also
   why ViewmodelPose is a named type: a pose has to be storable on a spec to
   make that move cost a function body.

   Absolute camera space, like the hip and aimed ends it is blended against,
   rather than a delta from either. A carry is where the gun ends up, not how
   far it moved from wherever it happened to be. */
const ViewmodelPose* weapon_spec_sprint_pose(const WeaponSpec* spec);

/* How the gun is held through a magazine change, and the track the support hand
   and the magazine follow while it is. Both shared today and both reached
   through an accessor, for the reason weapon_spec_sprint_pose gives: a reload
   is as much a property of a particular weapon as its carry is, and this is
   where that becomes one function body instead of a refactor.

   The pose is anchored at the firing grip, so a shorter gun is held at the same
   place by the same hand rather than hanging off the same origin. */
const ViewmodelPose* weapon_spec_reload_pose(const WeaponSpec* spec);
const ReloadSequence* weapon_spec_reload_sequence(const WeaponSpec* spec);

/* Where the support hand closes on the magazine, in the gun's own space. The
   track states it too; this is for the code that needs to know the hand is ON
   the magazine rather than merely somewhere along the way. */
const float* weapon_spec_mag_grip(const WeaponSpec* spec);

/* Where this gun rests for a given blend of the carries, with nothing animated
   in it - no bob, no sway, no kick, no stow. `sprint`, `reload` and `ads` are
   the weapon's own eased numbers.

   THE definition of the viewmodel's resting pose, and it is here rather than in
   weapon.c because two things need it and they must not disagree: the live pose
   the gun is drawn at, and the arms' shoulder calibration, which solves against
   the gun at rest at each end of its travel. Calibrated against a second copy
   of this the arms would reach for a gun that is not the one drawn.

   Sprint first, then aim over the top of it - the precedence the sprint number
   is collapsed by, applied here again to the pose so the blend is the same
   shape wherever it is read. */
void weapon_spec_rest_pose(const WeaponSpec* spec, float sprint, float reload,
                           float ads, ViewmodelPose* out);

#endif
