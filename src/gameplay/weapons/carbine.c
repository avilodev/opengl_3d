#include "gameplay/weapon_spec.h"
#include "gameplay/sight_spec.h"

#include <stddef.h>

/* The carbine. A CZ 806 Bren 2, firing the rifle's cartridge slower and
   landing it harder - the marksman's half of the same pair.

   520rpm against the rifle's 600, and 49 damage against its 47. Those two
   changes do not pay for each other and are not meant to: the sustained
   figure drops from 470 a second to 425, and what buys it back is the cone.
   This gun opens slower, climbs less per second and halves tighter under the
   sight, so it holds a group at a distance where the rifle is spraying. The
   rifle wins the room; this wins the corridor into it.

   49 and not 55, and the two under the round number are the whole reason the
   damage moved so little. A chest hit is flat and the pool is 100, so 50
   would put two of them at exactly lethal and one head at exactly lethal -
   turning a slightly-harder rifle into a two-shot gun with a better cone,
   which is not a trade, it is a replacement. At 49 the second chest hit
   leaves 2 and the third kills, which is where the rifle already is: the
   damage figure is what the gun FEELS like per round, and the shot count is
   what it costs, and only one of those was meant to change. If it should
   become a genuine two-shot weapon, 50 is the cliff and the fire rate has to
   come down a long way with it - see the note in player_loadout.c.

   Its landmarks come from tools/cz_to_glb.py, which prints them every time it
   runs and writes them to measured.json beside the mesh. */
static const FirearmSpec CARBINE_FIREARM = {
    .magazine = 30,
    .fire_interval = 0.1154f,           /* 520 rounds per minute */
    .reload_time = 2.45f,

    /* Where this gun is actually paid for. Tighter at rest than the rifle's
       0.14, slower to open in absolute terms - 0.095 at 8.7 rounds a second is
       0.82 of cone a second against the rifle's 1.05 - and quicker to come
       back. A held burst that puts the rifle at its ceiling in twelve rounds
       takes fifteen here, and those fifteen took half a second longer, so the
       cone is honest for most of a magazine rather than the first third. */
    .bloom_min = 0.10f,
    .bloom_max = 1.15f,
    .bloom_per_shot = 0.095f,
    .bloom_recovery = 5.4f,
    .bloom_recovery_delay = 0.12f,
    /* And it halves tighter than the rifle's 0.40 under the sight, which is
       the number that makes this the long-range option rather than a slower
       version of the same gun. */
    .ads_bloom_scale = 0.36f,

    /* Less per round than the rifle's 0.85 and much less per second: 0.80 at
       8.7 rounds a second is 6.9 of climb against the rifle's 8.5. A slower
       gun is allowed a harder individual kick - there is more time to settle
       between rounds - and the Bren 2 being the softer-shooting of the two
       real guns is what keeps it under rather than over. */
    .recoil_pitch = 0.80f,
    .recoil_yaw = 0.19f,
    .ads_recoil_scale = 0.60f,

    /* The EOTech EXPS2, swapped over from the rifle. The tube dot that came in
       on this gun's own mesh is on the M4 now - see its entry for why an optic
       is not a property of the gun that shipped with it.

       The rail is still measured and still this gun's: 0.0450 above the bore,
       printed by tools/cz_to_glb.py, plus the EXPS2's own 0.0374 from
       sights.c for a mount of 0.0824.

       What the swap gives up is a check, and it is worth naming rather than
       quietly losing. With its own dot on it, all three numbers could be
       verified against the mesh - the artist had modelled the optic seated,
       and rail plus reticle came to exactly where the glass sat. Nothing
       confirms the EXPS2 that way, because the EXPS2 was never on this gun.
       The halves are still each measured off their own mesh; what is gone is
       the third measurement that agreed with their sum.

       0.126 forward was where the artist put the dot, and it is kept because
       it is still a good place: this mount's seat lands between 0.149 and
       0.059, and the rail runs 0.015 to 0.448. It is a chosen number again
       rather than a measured one, which is the honest way to hold it. */
    .sight = &SIGHT_HOLOGRAPHIC,
    .rail_height = 0.0450f,
    .rail_forward = 0.126f,

    /* Negative, as on the rifle, and both numbers moved with the glass. This
       gun used to carry the 17.7mm tube and was pulled in close to make it
       big enough; on the EXPS2's 36mm pane that same geometry would fill 0.45
       of the viewport height, which is not a sight picture, it is a wall with
       a dot on it. Letting the gun sit back out to 0.161m from the eye at 50
       degrees puts it at 0.24.

       That those are the M4's old numbers to within a millimetre is not a
       coincidence and is the point: the framing belongs to the glass, not to
       the gun, so the same window at the same distance through the same
       frustum reads the same on either rifle. */
    .view_ads_forward = -0.278f,
    .view_ads_fov = 50.0f,
    /* Measured off the mesh by tools/cz_to_glb.py. The bore is the model's own
       y = 0, so the flash sits on it. */
    .muzzle_up = 0.0f,
    .muzzle_forward = 0.587f,

    /* Off the mesh's parts list. There are no loose rounds modelled in this
       magazine, so rounds_part stays null and the reload drops a closed box -
       which is what this mesh has to show. */
    .magazine_part = "mag.001",
};

const WeaponSpec WEAPON_SPEC_CARBINE = {
    .name = "Carbine",
    .kind = WEAPON_CLASS_FIREARM,
    .firearm = &CARBINE_FIREARM,

    /* One under the two-shot cliff - see the FirearmSpec above. */
    .damage = 49.0f,
    .range = 300.0f,

    .mesh_path = "models/weapons/cz_806_carbine/cz_806_carbine.glb",

    /* Right and down are the rifle's, because the projection is near enough
       the same and the gun hangs in the same corner. Forward is NOT, and the
       difference is arithmetic rather than taste: this mesh is 0.745m long
       with its origin 0.158 from the buttplate, against the rifle's 0.838 and
       0.225. Carrying the rifle's 0.38 across would put this gun's butt 6.7cm
       further from the eye than the rifle's sits, which is a shorter weapon
       reading as a more distant one. 0.313 puts the buttplate at -0.155, where
       the rifle's already is, and lets the 9cm the carbine is shorter show up
       at the muzzle end where it actually is. */
    .view_right = 0.148f,
    .view_down = 0.125f,
    .view_forward = 0.313f,

    /* The rifle's, for the rifle's reasons - see its entry. A carbine is
       carried off the same shoulders. */
    .neck_follow = 0.30f,

    /* Measured off the mesh, not copied off the rifle. The pistol grip sits
       behind the magwell and its middle is 0.066 below the bore; the foregrip
       is a separate part on the handguard and its middle is 0.063 below the
       bore at 0.318 forward. That the second of those lands within 12mm of the
       rifle's own support hand is not a coincidence and is worth stating: two
       rifles of the same class are held in the same place, and a number here
       that disagreed with the M4's by much would more likely be a bad
       measurement than a different gun. */
    .hold = WEAPON_HOLD_TWO_POINT,
    .grip_right = {0.0f, -0.066f, -0.094f},
    .grip_left = {0.0f, -0.063f, -0.318f},

    /* Off, by name, from the gun's own mesh - the same way and for the same
       reason as the rifle's.

       Worth knowing that the swap nearly earned it back. The front post stands
       0.0846 above the bore; under the tube dot at 0.0764 it was 8mm proud of
       the reticle in a window of 8.9mm radius, which is not a co-witness, it is
       the post filling the top of the glass. Under the EXPS2 at 0.0824 it is
       2mm proud in a window of 18mm - which is exactly what a lower-third
       co-witness looks like, and is the sight picture the M4's own importer
       kept its front post for.

       Moot now: the post is not in the mesh. It is dropped at import along
       with the tube dot the artist modelled beside it, so there is no name
       here to hide and nothing to turn back on without a re-import. See
       models/weapons/cz_806_carbine/import.json. */
    /* The library's rifle poses are the pistol ones - see the rifle's entry
       for what that costs. */
    .clips = {{ "Pistol_Aim_Down", "Pistol_Aim_Neutral", "Pistol_Aim_Up" }},
};
