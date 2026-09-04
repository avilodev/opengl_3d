/* Every weapon in the game, named once.

   Deliberately without an include guard: this file is meant to be included
   more than once, each time with WEAPON defined differently. It is the whole
   of the weapon list, and the header that expands it decides what a row means
   - an extern in weapon_spec.h, an array entry in weapon_registry.c.

   The point is that adding a weapon cannot half-happen. A spec defined in
   weapon_specs.c and never listed here has no extern, so nothing can name it
   and the omission is a compile error at the first use rather than a weapon
   that quietly exists and is quietly never checked. That was the old failure:
   the probes named weapons by symbol, so a weapon nobody remembered to add to
   tools/weapon_spec_probe.c passed every check by not being looked at.

   Order is the order the cycle key walks and the order the probes report in.
   Nothing else reads it, so it is free to be the order that makes a probe log
   easy to read. */

/* Fast, light, and forgiving: the sidearm this project started with. Kept
   because it is a good gun, not yet handed to the player. */
WEAPON(AUTO_PISTOL)

/* The issued weapon. Slower and heavier, and unmagnified by choice. */
WEAPON(ASSAULT_RIFLE)

/* The melee alternative: a katana, worn on the hip and drawn to use. */
WEAPON(KATANA)
