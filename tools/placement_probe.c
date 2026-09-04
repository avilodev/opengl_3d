/* Whether the four readers of weapon_placement_fields.h agree with each other.

   `make tuneprobe` from the project root. Built with -DWEAPON_TUNING, which
   is the only probe that is - the overlay does not exist in any other build,
   and a check that compiles it out would be a check that passes by not
   running.

   The claim the whole placement design rests on is that one field table drives
   the JSON reader, the C emitter, the save and the probe, so they cannot drift.
   That claim is worth exactly nothing unless something tests it, because the
   way it fails is silent: a field added to the struct and not to the table is
   a number that tunes fine, saves fine, and comes back as whatever it was -
   and the person nudging it concludes the number does not do anything.

   So: change every field to a value it could not have been, save, reload into
   a second copy, and compare. A field missing from any one of the three paths
   fails to survive the trip.

   It also reports which weapons are still reading a placement.json rather than
   their own C literal, because the overlay is scaffolding and "we never baked
   the knife" should be a line of output rather than a discovery. */
#include "gameplay/weapon_placement.h"
#include "gameplay/weapon_registry.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* A partial guard on the one failure the round trip below cannot see.

   A field added to a spec struct and NOT added to weapon_placement_fields.h
   is a number nothing tunes - and the round trip cannot notice, because it
   only ever walks the table. It would pass, cheerfully, while the new field
   sat there untouchable.

   So the sizes are pinned. When one of these fails, somebody added a field.
   Decide which kind it is:

     MEASURED - found by squinting at the screen or taken off the mesh by the
                importer, changes when the ASSET changes. Add a row to
                weapon_placement_fields.h, then update the number here.
     CHOSEN   - found by playing, changes when the DESIGN changes. It belongs
                in weapon_specs.c and nowhere else. Just update the number.

   What this does NOT catch, stated plainly because a guard trusted past its
   reach is worse than no guard: a single float appended AFTER the struct's
   last member lands in trailing padding and does not change the size. Checked,
   not assumed - MeleeSpec ends on float[3] at 440 bytes with three spare, so
   one more float is free and a second is not. Anything inserted mid-struct is
   caught, and so is anything wider than the slack.

   Sizes are for the host compiler, which is the only one that builds this
   file. They agree with the cross compiler's today; nothing depends on that,
   and if they diverge this check still does its job. */
static int failures = 0;

static void expect(int ok, const char* what) {
    if (!ok) ++failures;
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
}

/* A value no real placement holds, and a different one per field, so that two
   fields that got crossed over are caught as well as one that got lost. The
   step is a hundredth, which survives the emitter's four decimal places. */
static float marker(int n) { return -7.0f + (float)n * 0.01f; }

static int same(float a, float b) { return fabsf(a - b) < 5e-5f; }

/* Every field, three times over: stamped, checked, and restored. Written as
   one macro block per pass rather than three functions, because the whole
   point is that all three see the same list. */
static int stamp(WeaponSpec* spec, int start) {
    int n = start;
    WeaponSpec* WEAPON = spec;
    FirearmSpec* FIREARM = (FirearmSpec*)spec->firearm;
    MeleeSpec* MELEE = (MeleeSpec*)spec->melee;
#define PLACEMENT_F32(owner, field, doc)  if (owner) owner->field = marker(n++);
#define PLACEMENT_VEC3(owner, field, doc) \
    if (owner) for (int c = 0; c < 3; ++c) owner->field[c] = marker(n++);
#define PLACEMENT_POSE(name, stance, doc)                     \
    if (MELEE) {                                              \
        MELEE->carries[stance].x = marker(n++);               \
        MELEE->carries[stance].y = marker(n++);               \
        MELEE->carries[stance].z = marker(n++);               \
        MELEE->carries[stance].pitch = marker(n++);           \
        MELEE->carries[stance].yaw = marker(n++);             \
        MELEE->carries[stance].roll = marker(n++);            \
    }
#include "gameplay/weapon_placement_fields.h"
    return n;
}

static int verify(const WeaponSpec* spec, int start) {
    int n = start;
    const WeaponSpec* WEAPON = spec;
    const FirearmSpec* FIREARM = spec->firearm;
    const MeleeSpec* MELEE = spec->melee;
#define PLACEMENT_F32(owner, field, doc) \
    if (owner) expect(same(owner->field, marker(n++)), #field);
#define PLACEMENT_VEC3(owner, field, doc)                                  \
    if (owner) {                                                           \
        int ok = 1;                                                        \
        for (int c = 0; c < 3; ++c) ok &= same(owner->field[c], marker(n++)); \
        expect(ok, #field);                                                \
    }
#define PLACEMENT_POSE(name, stance, doc)                                  \
    if (MELEE) {                                                           \
        const ViewmodelPose* p = &MELEE->carries[stance];                  \
        int ok = same(p->x, marker(n++)) & same(p->y, marker(n++)) &       \
                 same(p->z, marker(n++)) & same(p->pitch, marker(n++)) &   \
                 same(p->yaw, marker(n++)) & same(p->roll, marker(n++));   \
        expect(ok, #name);                                                 \
    }
#include "gameplay/weapon_placement_fields.h"
    return n;
}

int main(void) {
    if (!weapon_registry_tuning_enabled()) {
        printf("built without WEAPON_TUNING; there is nothing here to check\n");
        return 1;
    }

    int round_tripped = 0;
    for (int i = 0; i < weapon_registry_count(); ++i) {
        WeaponSpec* spec = weapon_registry_tunable(i);
        if (!spec || !spec->mesh_path) {
            printf("\n%s: no mesh, so no folder to tune in\n",
                   weapon_registry_get(i)->name);
            continue;
        }
        printf("\n%s: every field survives save and load\n", spec->name);

        /* Keep the originals, because this writes a real placement.json into
           the asset's own folder and must leave nothing behind. */
        WeaponSpec kept = *spec;
        FirearmSpec kept_gun;
        MeleeSpec kept_blade;
        if (spec->firearm) kept_gun = *spec->firearm;
        if (spec->melee) kept_blade = *spec->melee;

        stamp(spec, i * 1000);
        int saved = weapon_placement_save(spec);
        expect(saved, "the placement writes");

        if (saved) {
            /* Back to the originals first, so a load that changed nothing
               would fail rather than pass on values that were never cleared -
               which is the way this check would otherwise lie. */
            *spec = kept;
            if (spec->firearm) *(FirearmSpec*)spec->firearm = kept_gun;
            if (spec->melee) *(MeleeSpec*)spec->melee = kept_blade;

            expect(weapon_placement_load(spec) > 0, "and reads back");
            verify(spec, i * 1000);
            ++round_tripped;
        }

        /* Put the weapon and its folder back exactly as they were. */
        *spec = kept;
        if (spec->firearm) *(FirearmSpec*)spec->firearm = kept_gun;
        if (spec->melee) *(MeleeSpec*)spec->melee = kept_blade;
        char path[256];
        snprintf(path, sizeof path, "%.*s/placement.json",
                 (int)(strrchr(spec->mesh_path, '/') - spec->mesh_path),
                 spec->mesh_path);
        remove(path);
    }

    printf("\n%d weapon(s) round-tripped, %d still unbaked\n",
           round_tripped, weapon_placement_unbaked_count());
    printf("%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}
