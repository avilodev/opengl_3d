#include "gameplay/sight_spec.h"

/* The optics rack. A sight is added here and nowhere else: no enum grows, no
   switch gains a case, and no renderer is edited, because a mesh is a path on
   the spec and a reticle style is a field. */

/* An EOTech EXPS2. Holographic, not reflex, and the reticle says so: a 68 MOA
   circle with a dot centred in it, rather than the bare dot a red dot has. The
   renderer draws that shape; what is stated here is the hardware. */
const SightSpec SIGHT_HOLOGRAPHIC = {
    .name = "EOTech EXPS2",
    .reticle = WEAPON_RETICLE_HOLOGRAPHIC,
    .mesh_path = "models/Scopes/Sight_close/sight_close.glb",

    /* Printed by tools/scope_to_glb.py, measured from the centre of the
       window down to the flat of the clamp. */
    .reticle_above_seat = 0.0374f,
    /* The EXPS2's window is 36mm across. Half of it is what the reticle is
       seen through and what a scope pass would be masked to. */
    .window_radius = 0.018f,
    .magnification = 1.0f,
    .ads_time = 0.14f,                  /* light glass comes up fast */
};

const SightSpec SIGHT_PRISM_2X = {
    .name = "2x Prism",
    .reticle = WEAPON_RETICLE_CHEVRON,
    /* No mesh yet, so the renderer draws its ring of boxes. */
    .mesh_path = 0,

    .reticle_above_seat = 0.0520f,
    .window_radius = 0.052f,
    .magnification = 2.0f,
    .ads_time = 0.22f,                  /* heavier glass, slower up */
};
