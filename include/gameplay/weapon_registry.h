#ifndef WEAPON_REGISTRY_H
#define WEAPON_REGISTRY_H

#include "gameplay/weapon_spec.h"

/* Every weapon there is, as a list a caller can walk.

   This exists because of one silent failure. The probes used to name weapons
   by symbol - tools/weapon_spec_probe.c listed three of them by hand, and
   the other four probes each hardcoded whichever weapon they were written
   for. A weapon added to src/gameplay/weapons/ and to the loadout list was
   therefore a weapon nothing checked: every probe still passed, because
   they were all still passing on the weapons they had been told about.
   Nothing announced
   that the fourth weapon was unmeasured. It simply was.

   A loop over this cannot make that mistake. The list comes out of
   weapon_list.h, which is the same file that declares the externs, so a weapon
   that can be named at all is a weapon that is in here.

   `find` is for the places that resolve a weapon from something outside the
   build - a save file, a config line, a command. Code that knows perfectly
   well which weapon it means still says WEAPON_SPEC_KATANA, because a name
   looked up at runtime is a name that can be misspelled and a symbol is not. */

int weapon_registry_count(void);

/* Always answers a weapon - an index off either end is clamped, so a caller
   holding a stale index gets a playable weapon rather than a crash. The same
   bargain player_loadout_get makes, for the same reason: the callers are a
   menu cursor and a cycle key, and neither has anything useful to do with a
   NULL. */
const WeaponSpec* weapon_registry_get(int index);

/* The weapon with this name, or NULL. Compared against WeaponSpec::name, which
   is the string the player is shown, because a second identifier that has to
   agree with the first is a second thing to keep in agreement. */
const WeaponSpec* weapon_registry_find(const char* name);

/* Where `spec` sits in the list, or -1 for a spec that is not in it. What the
   cycle key needs to answer "and then the next one". */
int weapon_registry_index_of(const WeaponSpec* spec);

/* --- writing to a weapon ------------------------------------------------

   The specs in src/gameplay/weapons/ are const globals and must stay that way:
   they are what ships, and they are the record of what a weapon IS. So the
   registry keeps a mutable COPY of each, hands the copy to everyone through
   weapon_registry_get, and this is how a writer reaches it. No caller anywhere
   learns that the copy exists.

   The copies carry their own FirearmSpec and MeleeSpec, re-pointed, because
   most of what is worth writing is on those halves rather than on the
   WeaponSpec itself.

   This used to exist only under WEAPON_TUNING, when the only writer was a
   placement file being nudged by hand. It is unconditional now because it has
   a second writer that ships: which optic a gun is wearing is something the
   player changes at runtime, and the sight lives on the FirearmSpec. A control
   the player has cannot be a control that needs a development build - see
   gameplay/sight_registry.h.

   Answers NULL for an index off the end, which is what a caller should check
   rather than assuming. */
WeaponSpec* weapon_registry_mutable(int index);

/* Whether this build has the PLACEMENT overlay - the hand-nudged tuning file
   and the keys that reload and bake it. Not about the copies above, which are
   always there. A constant, so the branches around it fold away in a release
   build rather than being #ifdef'd at every site. */
int weapon_registry_tuning_enabled(void);

#endif
