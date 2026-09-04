#ifndef WEAPON_PLACEMENT_H
#define WEAPON_PLACEMENT_H

#include "gameplay/weapon_spec.h"

/* Tuning a weapon's placement without rebuilding, and baking it back to C.

   THE NUMBERS IN weapon_specs.c ARE STILL THE REAL ONES. This is scaffolding
   around them, and it is meant to come down: a weapon is tuned here and then
   baked into a C literal, which is what ships. A release build does not open a
   file, cannot fail to parse one, and behaves exactly as it did before any of
   this existed.

   What it is for is the loop. Finding where a hand closes on a new mesh is
   twenty rounds of nudge-a-number, rebuild, restart, walk back to whatever you
   were looking at - and the number is decided by looking at the screen, so
   there is no way to shorten it by thinking harder. With this, the loop is
   edit placement.json and press F5.

   Why the numbers do not simply LIVE in JSON: the comments in weapon_spec.h
   and weapon_specs.c are load-bearing - they are most of what says why a
   number is what it is - and JSON has nowhere to put them. A typo should stay
   a compile error. And what data buys is only the edit-and-see-it loop, which
   ends.

   Which numbers are in scope is decided by weapon_placement_fields.h: the
   measured ones, that change when the ASSET changes. A magazine size is not
   one of them.

   None of this is compiled unless WEAPON_TUNING is defined. The functions are
   declared either way so that a caller does not need an #ifdef of its own;
   without it they are inert and answer 0. */

/* Reads models/<bucket>/<name>/placement.json over a weapon and returns how
   many fields it changed, or -1 if the file is there and will not parse. A
   file that is absent is not an error and answers 0: most weapons are baked,
   and a baked weapon has no placement file.

   The spec must be one the registry owns a mutable copy of - see
   weapon_registry_tunable. Handing this a pointer to a const literal would be
   undefined behaviour, which is why the tunable copies exist at all. */
int weapon_placement_load(WeaponSpec* spec);

/* Loads every weapon in the registry. Called once at startup by a tuning
   build. Returns the number of weapons that had a file. */
int weapon_placement_load_all(void);

/* Prints the weapon's placement as C, in the shape weapon_specs.c states it,
   ready to paste. This is how the scaffolding comes down: the numbers stop
   being data the moment somebody is happy with them.

   To `out`, which is stdout in the game and a file in a tool. */
void weapon_placement_emit(const WeaponSpec* spec, void* out);

/* Writes the weapon's placement back to its own placement.json, so a session
   spent nudging is not lost on exit. Returns 0 if the file cannot be written.

   Emitting C is the end of tuning; this is the middle of it. */
int weapon_placement_save(const WeaponSpec* spec);

/* How many weapons in the registry are still reading a placement.json rather
   than their own C literal. Printed by the spec probe, so "we never baked the
   knife" is a line of output rather than a discovery. */
int weapon_placement_unbaked_count(void);

#endif
