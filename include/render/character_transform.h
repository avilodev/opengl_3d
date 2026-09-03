#ifndef RENDER_CHARACTER_TRANSFORM_H
#define RENDER_CHARACTER_TRANSFORM_H

#include "render/character_renderer.h"
#include "render/character_state.h"
#include "render/skinned_model.h"
#include "utils/mat4.h"

/* Where a body and the rifle in its hands are, as matrices.

   This is arithmetic, not drawing: no GL, no state, nothing that needs a
   window. It was inside character_renderer.c, which is where it was written and
   the only place that needed it while the only question anyone asked of a body
   was how to draw one. That stopped being true the moment a tracer had to leave
   the barrel of the gun the body is holding - a question about the same
   transform, asked by gameplay, which must not link a renderer to ask it.

   Splitting it out also makes it checkable: `make bodyprobe` runs every
   function here against the rig the game actually loads, on the host compiler,
   with no window in sight. */

/* The scale the rig is drawn at, which is whatever stands it at gameplay
   height. Several things besides the draw need it. */
float character_rig_scale(const SkinnedModel* model);

/* How far to sink the drawn body so its hands land on the ledge it is holding.
   Zero when not hanging, and never positive - a hang lowers a body, it never
   lifts one. */
float character_hang_drop(const SkinnedModel* model, const CharacterInstance* instance,
                          const CharacterVisualState* state);

/* Where a body stands and how it is tilted, as one matrix: the position, the
   heading, the tilts a wall run or a mantle put on the whole of it, the lean's
   hip shift, and the scale that stands the rig at gameplay height.

   `model` may be NULL, which is the box fallback: it takes the travelling
   heading rather than the animation's, and it gets the crouch squash and the
   whole-body lean tilt that a rig with a spine does not need. */
Mat4 character_body_transform(const CharacterInstance* instance,
                              const CharacterVisualState* state,
                              const SkinnedModel* model);

/* The rifle in the body's right hand, in the rig's own space: the hand bone,
   the step into the palm, the quarter turn that stands the gun up in the fist,
   and the step back by the grip point. Zero if the rig has no hand to hold it
   with. Compose with character_body_transform for a world matrix. */
/* The scabbard on the body's hip, in the rig's own space: the pelvis bone,
   the step onto the belt, and the cant it hangs at - all three stated on the
   MeleeSpec, so both views read one set of numbers. Zero if the rig has no
   pelvis. Compose with character_body_transform for a world matrix.

   Its origin is the koiguchi, the mouth, which is where the blade enters and
   leaves. Everything about a draw is measured from that one point. */
/* The held weapon's whole transform in WORLD space: the body, then the hand,
   then the weapon in it. What character_weapon_muzzle takes a point off, handed
   back as a matrix instead - because a blade is a segment rather than a point,
   and a swing needs the transform itself to sweep with.

   The body's hand and not the viewmodel's, in both views. The viewmodel is
   drawn through a projection of its own and a camera-space offset put straight
   into the world lands about half as far from the middle of the screen as the
   thing it came from, which is a correction a tracer has to make and a hit
   test should not have to. The body's hand is already in world units, and it
   is in the same place whether or not anybody is drawing it. */
int character_weapon_world(const SkinnedModel* model,
                           const CharacterInstance* instance,
                           const CharacterVisualState* state, Mat4* out);

int character_scabbard_transform(const SkinnedModel* model, const Pose* pose,
                                 const MeleeSpec* melee, Mat4* out);

int character_held_weapon_transform(const SkinnedModel* model, const Pose* pose,
                                    Mat4* out);

/* The muzzle of that rifle, in world units - the two offsets are the weapon
   spec's own, in the weapon model's metres, and the rig's scale is applied for
   you. Zero unless this body has a rig that has been through an update, since
   until then there is no drawn gun to take a point off.

   The model is passed in rather than looked up from a library, so nothing here
   depends on the loader. It is what lets `make bodyprobe` call this exact
   function against the rig the game loads, on the host compiler. */
int character_weapon_muzzle(const SkinnedModel* model,
                            const CharacterInstance* instance,
                            const CharacterVisualState* state,
                            float muzzle_up, float muzzle_forward,
                            float out[3]);

#endif
