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
       circular window with an opaque body around it. */
    WEAPON_RETICLE_CHEVRON
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

/* The EOTech EXPS2. Holographic rather than reflex, which is why the reticle
   kind is named for what it is: the dot is projected at infinity, so it stays
   on the target when the shooter's eye moves off the sight's axis. That fact
   is what the renderer draws, and it is why the reticle is a quad on the gun
   rather than a mark on the screen. */
extern const SightSpec SIGHT_HOLOGRAPHIC;

/* The magnified prism. Still the renderer's boxes - models/Scopes/Sight_mid
   is waiting for its mesh - which is exactly what a null mesh_path says. */
extern const SightSpec SIGHT_PRISM_2X;

#endif
