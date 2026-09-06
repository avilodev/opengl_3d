#include "gameplay/weapon_spec.h"

#include <stddef.h>

/* The Beretta PMX. A 9mm carbine: thirty rounds, a stock, a full-length rail
   and iron sights standing on it.

   The gun between the two halves of this rack. It fires the M9's cartridge out
   of a barrel long enough to make it worth something and holds it with a stock
   and both hands, which is the whole of what a pistol-calibre carbine is: a
   pistol's round shot like a rifle. Against the MP7 it is slower, steadier and
   hits harder per round; against the assault rifle it is faster and does not
   reach.

   What it does NOT have is glass, and that is worth reading before anything
   below. The rail is measured, the rail is real, and the gun still carries
   irons - because on this mesh the flip-up sights are modelled into the same
   node as the chassis. There is no name to hide them by, and see the mount
   arithmetic further down for what putting an optic over them would look
   like. It is a constraint of the asset, stated where it can be seen, rather
   than a design nobody wrote down. */
static const FirearmSpec BERETTA_PMX_FIREARM = {
    .magazine = 30,
    .fire_interval = 0.0857f,           /* 700 rounds per minute */
    .reload_time = 2.20f,

    /* Between the MP7's and the carbine's, which is where the whole gun sits.
       Tighter at rest than the MP7 by a third and slower to open - 0.070 a
       round at 11.7 a second is 0.82 of cone a second against the MP7's 1.34 -
       so a held burst stays honest for twice as long. It gives back the
       recovery: 5.6 a second off a longer delay, against 6.6 off a shorter
       one, so this gun rewards a burst held rather than a burst tapped. */
    .bloom_min = 0.11f,
    .bloom_max = 1.20f,
    .bloom_per_shot = 0.070f,
    .bloom_recovery = 5.6f,
    .bloom_recovery_delay = 0.110f,
    .ads_bloom_scale = 0.40f,

    /* A stock and a 9mm: less kick per round than any rifle here and more than
       the MP7's, which is the weight of the round rather than the weight of
       the gun. Per second, 0.42 at 11.7 a second is 4.9 of climb - a hair over
       the MP7 and well under both rifles. */
    .recoil_pitch = 0.42f,
    .recoil_yaw = 0.14f,
    .ads_recoil_scale = 0.60f,

    /* No optic, on a gun that has a rail. That combination exists nowhere else
       here and it is the mesh's doing rather than a choice about the gun.

       The rail is measured and correct: 0.0348 above the bore, printed by
       tools/gun_to_glb.py, which is what a rail on a pistol-calibre carbine
       measures. What is on it is a pair of flip-up irons standing 33mm proud
       of it - and unlike the MP7's, which are four separate nodes and can be
       hidden by name, these are modelled into the same node as the whole
       chassis. There is nothing to name.

       So the arithmetic, since it is the argument. The EXPS2's reticle sits
       0.0374 above its seat, which would put it 4mm ABOVE sight tops that are
       33mm up - inside a window of 18mm radius, with the front sight in front
       of the glass and the rear one behind it. The tube dot is worse: 0.0314,
       which is below them. Either optic on this gun is an optic with iron
       sights drawn through it, which is exactly the screenshot
       tools/weapon_spec_probe.c exists to warn about.

       Irons it is, then, and irons suit it: see the M9's entry for what a NULL
       sight does, which is bring the gun to centre on its own bore. If this
       mesh is ever re-exported with the sights split out, this is one line and
       a measured rail is already waiting for it. */
    .sight = NULL,
    /* Kept, unlike the pistols', because unlike theirs it is a measurement of
       something: this gun HAS a rail, and the number is what the mesh says
       about it. It is read by nothing while the sight is NULL - see
       weapon_spec_sight_height, which answers zero without ever reaching
       here - and it is the half that is ready when the other half arrives. */
    .rail_height = 0.0348f,
    .rail_forward = 0.18f,

    /* A shouldered gun with no glass to frame, so this is the smallest pull-in
       of any long gun here: enough that aiming reads as the gun coming up,
       not enough to haul a sight picture that does not exist toward the eye.
       The field of view stays wide for the same reason - there is nothing to
       look THROUGH, so narrowing the viewmodel frustum would only make the
       gun bigger. */
    .view_ads_forward = -0.16f,
    .view_ads_fov = 52.0f,

    /* Measured off the mesh by tools/gun_to_glb.py. 0.527 is the whole gun:
       this mesh's origin is at the buttplate rather than at the back of a
       receiver, because its chassis and its stock are one node and there is no
       seam between them to name. See the manifest. */
    .muzzle_up = 0.0f,
    .muzzle_forward = 0.527f,

    /* Both, and this is the only gun here that has both. The magazine is its
       own material in the source and the rounds stacked inside it are another,
       so a reload can take the box away WITH its ammunition and bring a full
       one back - which is most of what says a magazine was changed rather than
       refilled where it sat. The M4 is the only other weapon that can show
       that; the carbine's mag is a closed box and the MP7's is too. */
    .magazine_part = "pmx_magazine",
    .rounds_part = "pmx_rounds",
};

const WeaponSpec WEAPON_SPEC_BERETTA_PMX = {
    .name = "Beretta PMX",
    .kind = WEAPON_CLASS_FIREARM,
    .firearm = &BERETTA_PMX_FIREARM,

    /* Four to a chest. The band is what fixes it: 25 exactly would also be
       four, and 26 through 33 is still four, so anything in that stretch is
       the same gun and the number only says how much a limb hit is worth. What
       matters is which side of the boundaries it sits on. At 34 it becomes a
       three-round gun and it is an assault rifle firing pistol ammunition; at
       24 it becomes five and it is a slower MP7 with nothing of its own.

       Four rounds at 700 a minute is 0.26 seconds, which is a hair behind the
       MP7's 0.25 and well behind both rifles' 0.20. It gets there with a cone
       a third tighter than the MP7's, which is the trade, whole. */
    .damage = 26.0f,
    .range = 300.0f,

    .mesh_path = "models/weapons/beretta_pmx/beretta_pmx.glb",

    /* Right and down are the long guns'. Forward is their arithmetic with the
       one wrinkle this mesh has: the shouldered weapons all put the BUTTPLATE
       0.155 in front of the eye, and on every other one that is a sum of the
       gun's view_forward and how far its origin sits from its butt. Here the
       origin IS the butt - the import had no receiver seam to name - so the
       sum has nothing in it and view_forward is the 0.155 outright. That is
       not this gun sitting closer than the others; it is the same buttplate in
       the same place, measured from a different zero. */
    .view_right = 0.148f,
    .view_down = 0.125f,
    .view_forward = 0.155f,

    .neck_follow = 0.30f,

    /* Two points, both measured off the mesh in the model's own frame. The
       pistol grip is behind the magwell on this gun - the magazine feeds
       forward of the trigger, MP5-fashion - and its fist sits 55mm below the
       bore at 0.245 forward. The support hand goes out on the handguard at
       0.462, and on the bore line rather than below it: this is a tube the
       hand wraps around the barrel's own axis, not a vertical grip hanging
       under one, so the palm's centre is the barrel's centre. That is the
       difference from the carbine's foregrip and from the MP7's handle, both
       of which hang. */
    .hold = WEAPON_HOLD_TWO_POINT,
    .grip_right = {0.0f, -0.055f, -0.245f},
    .grip_left = {0.0f, 0.0f, -0.462f},

    /* The library's rifle poses are the pistol ones - see the rifle's entry
       for what that costs. */
    .clips = {{ "Pistol_Aim_Down", "Pistol_Aim_Neutral", "Pistol_Aim_Up" }},
};
