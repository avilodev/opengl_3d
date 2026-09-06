/* Writes every weapon's CURRENT placement out as its own placement.json.

   `make placements` from the project root. Built with -DWEAPON_TUNING, because
   weapon_placement_save is inert without the overlay and a writer that
   compiled out would be a writer that quietly wrote nothing.

   This is the start of a tuning session rather than a step in the build. The
   numbers in src/gameplay/weapons/ are still the real ones; what this does is
   hand them to you in the form the F5 key can re-read, so the loop is edit a
   number and look at the screen rather than edit, rebuild, restart and walk
   back across the map. F6 prints them back as C when you are done.

   Seeded from the baked values on purpose. A file that says exactly what the
   spec already says changes nothing when it loads, which means running this is
   safe at any time and a weapon only starts behaving differently once somebody
   has actually edited its file.

   It does NOT overwrite a file that is already there. A session's worth of
   nudging is the thing most worth not destroying, and "reset this weapon" is
   deleting one file rather than a flag here that could be pressed by accident.

   A weapon with no mesh has nowhere to put one: the path is derived from
   mesh_path's folder, so a weapon still drawn as boxes is reported and
   skipped. */
#include "gameplay/weapon_placement.h"
#include "gameplay/weapon_registry.h"

#include <stdio.h>

int main(void) {
    if (!weapon_registry_tuning_enabled()) {
        printf("built without WEAPON_TUNING; there is nothing here to write\n");
        return 1;
    }

    int written = 0, kept = 0, nowhere = 0;
    for (int i = 0; i < weapon_registry_count(); ++i) {
        const WeaponSpec* spec = weapon_registry_get(i);
        char path[256];
        if (!weapon_placement_path(spec, path, sizeof path)) {
            printf("  %-14s no mesh, so no folder to put one in\n", spec->name);
            ++nowhere;
            continue;
        }
        FILE* existing = fopen(path, "r");
        if (existing) {
            fclose(existing);
            printf("  %-14s already has one, left alone: %s\n",
                   spec->name, path);
            ++kept;
            continue;
        }
        if (weapon_placement_save(spec)) {
            ++written;
        } else {
            printf("  %-14s could not be written to %s\n", spec->name, path);
        }
    }

    printf("\n%d written, %d already there, %d with nowhere to put one\n",
           written, kept, nowhere);
    printf("edit one and press F5 in bin/GameTuning.exe; F6 prints it back "
           "as C\n");
    return 0;
}
