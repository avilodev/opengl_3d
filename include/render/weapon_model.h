#ifndef RENDER_WEAPON_MODEL_H
#define RENDER_WEAPON_MODEL_H

#include "gameplay/weapon_spec.h"

/* The gun and optic meshes, owned in one place because two renderers draw the
   same gun: the viewmodel in the player's hands, and the body's right hand in
   third person. Loading it twice would be two copies of a four megabyte
   texture and two chances for them to disagree.

   Every mesh here is authored in the viewmodel's own axes and in metres - +x
   right, +y up, -z forward - so a caller that has set the transform draws it
   with no correction of its own. What each one's origin is depends on what it
   is for: a gun's sits on the bore line at the back of the receiver, because
   that is what a muzzle flash hangs off, and an optic's sits on its reticle,
   because that is what an eye lines up with. tools/fbx_to_glb.py and
   tools/scope_to_glb.py are what put them in those frames. */

/* Draws the mesh for `kind` about the origin. Non-zero when it drew; zero when
   that weapon has no mesh, or the file would not load, and the caller should
   fall back to the boxes it drew before there was one.

   Loads on the first call, which has to be one with a current GL context. */
/* `hidden_parts` is the gun's own list of material names it does not draw, or
   NULL for all of it - see WeaponSpec.hidden_parts. Taken as an argument rather
   than read off the model, because the loaded meshes are cached and shared by
   path: what one gun leaves off is not a property of the file. */
int weapon_model_draw(WeaponModelKind kind, const char* const* hidden_parts);

/* Only the named parts of the same mesh, at the caller's current transform.
   What this exists for is the magazine during a reload: the gun is drawn
   without it, and it is drawn again here wherever the hand has taken it. */
int weapon_model_draw_only(WeaponModelKind kind, const char* const* parts);

/* The same, for the optic a weapon carries, drawn about its reticle. The
   caller has already put the transform where the reticle goes, which for the
   viewmodel is weapon_spec_sight_height and weapon_spec_sight_forward.

   The mesh is the sight's own business - it names its file - so a new optic
   arrives here without this function learning anything about it. */
int weapon_model_draw_sight(const SightSpec* sight);

/* Just the optic's glass, geometry only, at the caller's transform. This is the
   aperture a holographic reticle is seen through: masked to it, the reticle can
   only appear where the window actually is, so it emerges from behind the hood
   as the sight swings in rather than fading up in open air. Zero when this
   sight has no mesh, and so no glass to look through. */
int weapon_model_sight_glass(const SightSpec* sight);

/* Releases the loaded meshes and their textures. Needs a current context;
   call once, at shutdown. */
void weapon_model_shutdown(void);

#endif
