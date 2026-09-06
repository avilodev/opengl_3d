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
    .mesh_path = "models/attachments/sight_close/sight_close.glb",

    /* Printed by tools/scope_to_glb.py, measured from the centre of the
       window down to the flat of the clamp. */
    .reticle_above_seat = 0.0374f,
    /* The EXPS2's window is 36mm across. Half of it is what the reticle is
       seen through and what a scope pass would be masked to. */
    .window_radius = 0.018f,
    .magnification = 1.0f,
    .ads_time = 0.14f,                  /* light glass comes up fast */
};

/* A SORVO-19 open reflex. The EXPS2's argument with less of everything: an
   open pane rather than a tube, so the room around the target stays yours, but
   28.1mm of it against the EOTech's 36mm.

   It carries the ring, which is a decision and not a measurement. A reflex
   sight of this kind projects a bare dot, so the 68 MOA circle is the one
   thing on this spec that the hardware does not have; it is here because the
   rack is four optics deep and three of them would otherwise show the eye the
   same picture through different glass.

   Its numbers are printed by tools/reflex_to_glb.py. reticle_above_seat is the
   lowest of the four - this mount is flat-bottomed and short, where the other
   three ride risers - which is what makes it worth carrying beside the MRO. */
const SightSpec SIGHT_REFLEX = {
    .name = "SORVO-19 Reflex",
    .reticle = WEAPON_RETICLE_HOLOGRAPHIC,
    .mesh_path = "models/attachments/sight_reflex/sight_reflex.glb",

    .reticle_above_seat = 0.0293f,
    /* Half the 28.1mm pane. Taken across rather than up, the way the EXPS2's
       is, because the window is wider than it is tall on both of them and the
       mask is a circle either way. */
    .window_radius = 0.0141f,
    .magnification = 1.0f,
    /* Between the EXPS2's 0.14 and the tube dot's 0.16, and for the reason the
       tube dot's comment gives: an open pane is in front of the eye already
       and only has to be found to within its own width. This one is 8mm
       narrower than the EOTech's and has no tube to look down. */
    .ads_time = 0.15f,
};

/* A Trijicon MRO on a full co-witness mount. The tube the carbine's optic
   should have been and is not: a 21.5mm aperture against that one's 17.7, and
   a mount that puts the glass 39.5mm over the rail - a centimetre higher than
   anything else in the rack, which is what a co-witness height is for and what
   makes this the optic that changes how the gun sits rather than how it looks.

   The ring is a decision here too, but a smaller one: Trijicon ships the MRO
   HD with a 68 MOA circle around its dot, so this is a real variant of a real
   optic rather than a picture borrowed to tell four sights apart.

   Its numbers are printed by tools/mro_to_glb.py, off a mesh whose scale comes
   from the Picatinny rail the artist modelled under it - the only published
   dimension in any of these four files. */
const SightSpec SIGHT_MRO = {
    .name = "Trijicon MRO",
    .reticle = WEAPON_RETICLE_HOLOGRAPHIC,
    .mesh_path = "models/attachments/sight_mro/sight_mro.glb",

    .reticle_above_seat = 0.0395f,
    /* Half the objective, which is the narrower of this optic's two openings
       and so the one that bounds the sight picture. The ocular behind it is
       32.5mm and only has to be wide enough for the eye to find. */
    .window_radius = 0.0108f,
    .magnification = 1.0f,
    /* The slowest of the four, and the one place weight rather than aperture
       decides it: this glass rides a steel bracket twice the height of any
       other mount here, and the gun has further to travel to bring it up. */
    .ads_time = 0.17f,
};

/* The tube red dot off the carbine's mesh. One power like the EXPS2, and the
   argument for it is the same; what differs is the sight picture and the
   glass in front of it.

   A bare dot, so WEAPON_RETICLE_DOT and not HOLOGRAPHIC: this optic has no
   68 MOA ring, and drawing one would hand it a coarse mark it does not have.

   Its window is a 17.7mm tube against the EXPS2's 36mm pane - half the
   aperture, which is what a tube costs and why it comes up a shade slower.
   Both numbers are printed by tools/reddot_to_glb.py off the mesh, which is
   also where reticle_above_seat comes from: the optic is modelled sitting on
   the carbine's rail, so the seat is where the artist rested it rather than
   something inferred from its own silhouette. */
const SightSpec SIGHT_RED_DOT = {
    .name = "Tube Red Dot",
    .reticle = WEAPON_RETICLE_DOT,
    .mesh_path = "models/attachments/sight_dot/sight_dot.glb",

    .reticle_above_seat = 0.0314f,
    .window_radius = 0.0089f,
    .magnification = 1.0f,
    /* A shade slower than the EXPS2's 0.14. Not weight - this is the lighter
       of the two - but aperture: a 17.7mm tube has to be found before it can
       be looked through, where a 36mm open pane is in front of the eye
       already. */
    .ads_time = 0.16f,
};
