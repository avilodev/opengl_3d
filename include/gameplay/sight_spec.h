#ifndef SIGHT_SPEC_H
#define SIGHT_SPEC_H

/* An optic, as data, measured about its own reticle and knowing nothing about
   which gun carries it.

   That last part is the whole point of this file existing. A mount height is
   two measurements added together - how far the gun's rail stands above its
   bore, and how far the sight's reticle stands above the rail seat it clamps
   to - and they are taken off two different meshes by two different importers.
   Adding them by hand into one number, which is what this system used to do,
   welds the sight's half into the gun's and means every new gun re-derives it
   with nothing to check the arithmetic against. Kept apart, either half can
   change without the other knowing, and any sight fits any rail.

   What is NOT here is how far from the eye the glass ends up. A holographic
   sight's eye relief is effectively unlimited, so that distance is set by how
   the gun is held - a pistol at arm's length, a rifle shouldered - and it stays
   on the weapon as view_ads_forward. Putting it here would have meant fitting
   this sight to a pistol dragged the pistol to a rifle's hold.

   Everything else here is measured, not chosen. tools/scope_to_glb.py prints
   it. */

typedef enum {
    /* A projected holographic reticle on an open window. One power: aiming
       centres the gun and nothing else, so what the sight buys is a clean aim
       point and a steadier gun, never a closer target. The world around the
       reticle stays exactly as big as it was, which is the argument for this
       kind of sight - you keep the room while you shoot into it. */
    WEAPON_RETICLE_HOLOGRAPHIC,
    /* A chevron on a ranging stadia, seen through a magnified prism's
       circular window with an opaque body around it.

       NOTHING IN THE RACK USES THIS. The 2x prism that did was the last optic
       drawn out of the renderer's boxes, and it went when the fourth mesh
       arrived. What is left behind is the whole magnified path - this value,
       hud.c's draw_acog_reticle, weapon_renderer.c's draw_acog, and the second
       pass of the world in main.c that they are painted against - kept
       deliberately and not by neglect.

       Kept because it is the only thing here that answers a different
       question. The four optics that ship are all one power, so what they
       differ in is where the glass sits and what it costs to look through it;
       none of them can make the target bigger. The machinery for an optic that
       can is written and works, and a magnified sight is a plausible fifth. It
       costs a branch that is never taken.

       Anything that reads this enum still has to handle it. It is unreachable
       by data, not by type, and a switch that drops it would be a switch that
       breaks the day a scope is added. */
    WEAPON_RETICLE_CHEVRON,
    /* A bare dot in a tube, and the difference from HOLOGRAPHIC is one mark
       rather than two: no 68 MOA ring around it. That ring is a real feature
       of a holographic sight and not decoration - it is the coarse mark the
       eye finds first - so drawing it on a sight that does not have one would
       make a plain red dot claim a sight picture it does not offer. Collimated
       and projected the same way otherwise, which is why it shares the
       renderer's path with HOLOGRAPHIC rather than the HUD's with CHEVRON. */
    WEAPON_RETICLE_DOT
} WeaponReticleKind;

typedef struct {
    const char* name;
    WeaponReticleKind reticle;
    /* The mesh, authored about its reticle - which is the point an eye lines
       up with, and so the point the whole sight picture is built from. Null
       while an optic is still drawn out of the renderer's boxes. */
    const char* mesh_path;

    /* The sight's half of the mount height: its reticle above the flat it
       rests on when clamped to a rail. Add the gun's rail height for the
       distance the gun drops when it comes up to the eye. */
    float reticle_above_seat;
    /* The glass. This is the aperture the reticle is seen through, and the
       window a magnified pass is masked to, so it is one number with two
       readers and it belongs to the optic rather than to any gun. */
    float window_radius;
    /* 1.0 for an unmagnified sight, which is not a degenerate case but the
       common one: it means aiming does not make the target bigger. */
    float magnification;
    /* How fast this glass comes up. A property of its weight, so it travels
       with the optic and not with the gun. */
    float ads_time;
} SightSpec;

/* Every optic there is. Which ones exist is said in sight_list.h, beside a line
   each on what they are; what they are made of is said in sights.c. Neither is
   repeated here, because a third place to say it is a third place for it to be
   wrong.

   To iterate over these rather than name one, include sight_registry.h. */
#define SIGHT(id) extern const SightSpec SIGHT_##id;
#include "gameplay/sight_list.h"
#undef SIGHT

#endif
