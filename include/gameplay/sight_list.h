/* Every optic in the game, named once.

   Deliberately without an include guard: this file is meant to be included
   more than once, each time with SIGHT defined differently. It is the whole of
   the optics rack, and the header that expands it decides what a row means -
   an extern in sight_spec.h, an array entry in sight_registry.c.

   The same bargain weapon_list.h makes, and made for the same failure. A sight
   defined in sights.c and never listed here has no extern, so nothing can name
   it: the omission is a compile error at the first use rather than an optic
   that quietly exists, is quietly never checked, and is quietly unreachable
   from the key that exists to look at it.

   Order is the order the sight-swap key walks and the order the probe reports
   in. It used to be free, and was spent on putting the two optics that had
   meshes beside each other; all four have meshes now, so it is spent on the
   one axis that separates them. Widest window first, narrowest last - 36mm of
   open pane down to a 17.7mm tube - because that is what changes as the key is
   held down, and walking it in order makes the thing the rack exists to
   compare visible as a progression rather than a shuffle. */

/* An EOTech EXPS2. Open pane, 68 MOA ring around a dot. */
SIGHT(HOLOGRAPHIC)

/* A SORVO-19 open reflex. The same open pane and the same ring, 8mm narrower,
   and no hood at all around the glass. */
SIGHT(REFLEX)

/* A Trijicon MRO, on a full co-witness mount. A tube, and the tallest of the
   four by a centimetre. */
SIGHT(MRO)

/* A tube red dot, off the carbine's own mesh. Half the window, bare dot. */
SIGHT(RED_DOT)
