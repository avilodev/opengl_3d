/* Every weapon in the game, named once.

   Deliberately without an include guard: this file is meant to be included
   more than once, each time with WEAPON defined differently. It is the whole
   of the weapon list, and the header that expands it decides what a row means
   - an extern in weapon_spec.h, an array entry in weapon_registry.c.

   The point is that adding a weapon cannot half-happen. A spec defined in
   src/gameplay/weapons/ and never listed here has no extern, so nothing can
   name it and the omission is a compile error at the first use rather than
   a weapon that quietly exists and is quietly never checked. That was the
   old failure: the probes named weapons by symbol, so a weapon nobody
   remembered to add to tools/weapon_spec_probe.c passed every check by not
   being looked at.

   Order is the order the cycle key walks and the order the probes report in.
   Nothing else reads it, so it is free to be the order that makes a probe log
   easy to read - here, the four that were here first and then the four that
   arrived together, each group in the order its own comparisons are made in.

   What a row is MADE of lives in src/gameplay/weapons/, one file per weapon -
   the tuning surface for the guns themselves, and where a number that changes
   how a weapon feels belongs.
   That used to be one weapon_specs.c, and it stopped being one at eight
   weapons and 1200 lines. The split is not only about length: a file per
   weapon is a file per thing being tuned, so a change to the MP7 is a diff
   that says MP7, and the Makefile takes the directory as a wildcard so that
   adding a weapon is adding a file and a row here and nothing else. */

/* Fast, light, and forgiving: the sidearm this project started with. Kept
   because it is a good gun, not yet handed to the player. */
WEAPON(AUTO_PISTOL)

/* The issued weapon. Slower and heavier, and unmagnified by choice. */
WEAPON(ASSAULT_RIFLE)

/* The other rifle: shorter, slower-firing and harder-hitting, under the EXPS2
   the M4 used to wear. Beside ASSAULT_RIFLE on purpose - the two are the
   comparison, and swapping their glass is what made it one. */
WEAPON(CARBINE)

/* The melee alternative: a katana, worn on the hip and drawn to use. */
WEAPON(KATANA)

/* The sidearm, and the first weapon here that is neither automatic nor
   scoped. A shot at a time, over irons, for two to a chest. */
WEAPON(M9)

/* The same argument one step further: slower again, eight rounds, and over
   the boundary where a chest takes one round instead of two. */
WEAPON(MAGNUM_357)

/* The close gun. The fastest here, on the smallest window, out of a cone that
   is exact for four rounds and a spray after twelve. */
WEAPON(MP7)

/* The one in between: a pistol's cartridge shot like a rifle. Steadier than
   the MP7, shorter-legged than either rifle, and on irons because its mesh
   models them into the chassis - see its spec. */
WEAPON(BERETTA_PMX)
