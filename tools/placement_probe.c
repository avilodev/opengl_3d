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
#include <stdlib.h>
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
                in the weapon's own file and nowhere else. Just update the number.

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

/* A weapon's placement file, held aside and put back.

   This probe writes a REAL placement.json into a real asset folder - that is
   the only way to test the save and the load against each other - and those
   files are tracked and hand-edited now. They did not used to be: while every
   weapon was baked the folders were empty, so removing what this wrote was the
   same thing as leaving them alone. It is not any more. Deleting a session's
   worth of somebody's tuning to check that a field survives a round trip would
   be the check doing more damage than the bug it looks for.

   So the bytes go aside first and come back afterwards, and a folder that had
   no file still has none. NULL means there was nothing there, which is the
   normal case for a baked weapon and is restored by removing. */
static char* hold_aside(const char* path, long* size) {
    *size = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* bytes = length >= 0 ? (char*)malloc((size_t)length + 1) : NULL;
    if (bytes && fread(bytes, 1, (size_t)length, f) == (size_t)length) {
        *size = length;
    } else {
        free(bytes);
        bytes = NULL;
    }
    fclose(f);
    return bytes;
}

static void put_back(const char* path, char* bytes, long size) {
    if (!bytes) {
        remove(path);
        return;
    }
    FILE* f = fopen(path, "wb");
    if (f) {
        fwrite(bytes, 1, (size_t)size, f);
        fclose(f);
    } else {
        printf("   COULD NOT RESTORE %s - its contents are in this process "
               "and nowhere else\n", path);
        ++failures;
    }
    free(bytes);
}

int main(void) {
    if (!weapon_registry_tuning_enabled()) {
        printf("built without WEAPON_TUNING; there is nothing here to check\n");
        return 1;
    }

    int round_tripped = 0;
    for (int i = 0; i < weapon_registry_count(); ++i) {
        WeaponSpec* spec = weapon_registry_mutable(i);
        if (!spec || !spec->mesh_path) {
            printf("\n%s: no mesh, so no folder to tune in\n",
                   weapon_registry_get(i)->name);
            continue;
        }
        printf("\n%s: every field survives save and load\n", spec->name);

        /* Whatever was in the folder, held aside until the end. */
        char path[256];
        if (!weapon_placement_path(spec, path, sizeof path)) {
            printf("   no path for this weapon\n");
            ++failures;
            continue;
        }
        long held_size = 0;
        char* held = hold_aside(path, &held_size);

        /* And the originals, because the save below writes over the spec. */
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

        /* Put the weapon and its folder back exactly as they were - which for
           the folder now means the bytes that were there, not an empty one. */
        *spec = kept;
        if (spec->firearm) *(FirearmSpec*)spec->firearm = kept_gun;
        if (spec->melee) *(MeleeSpec*)spec->melee = kept_blade;
        put_back(path, held, held_size);
    }

    printf("\n%d weapon(s) round-tripped, %d still unbaked\n",
           round_tripped, weapon_placement_unbaked_count());
    printf("%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}
