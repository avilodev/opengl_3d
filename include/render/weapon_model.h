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
   that is what a muzzle flash hangs off; a sword's sits in the middle of its
   grip, because a sword has no bore and what it hangs off is the hand; and an
   optic's sits on its reticle, because that is what an eye lines up with. The
   importers in tools/ are what put them in those frames.

   Which file a weapon draws is the weapon's own business - it is on the spec -
   so a new one arrives here without this file learning anything about it. That
   was not always true: the path used to be a #define reached through an enum,
   and adding the first sword meant editing this file to say so. */

/* How many distinct meshes the cache behind this file holds at once - counted
   off the same two lists that say what exists, rather than written down.

   Written down it was 8, and the game grew past it. The cache is keyed by path
   and never evicts, so the first 8 distinct meshes a session asked for got a
   slot and every later one was refused and fell back to the boxes. Nothing
   said so. What it looked like was a gun that was a gun on one run and a crate
   on the next, because WHICH meshes lost depended entirely on the order they
   were first asked for - which class spawned, which weapon key was pressed,
   which optic was fitted. With four optics on a cycle key and seven guns, a
   session could ask for twelve.

   Counted, it cannot happen again: a weapon can name two meshes, its own and a
   scabbard, and an optic names one, so this is the most the game can ever ask
   for at once and adding either kind moves it. The slack costs a 2.2KB header
   per unused slot and no geometry at all - the vertices hang off pointers and
   are only allocated by a load that actually happened.

   tools/weapon_spec_probe.c checks the game against this, which is what turns
   a silent fallback into a failing check. */
enum {
    WEAPON_MESH_SLOTS = 0
#define WEAPON(id) + 2
#include "gameplay/weapon_list.h"
#undef WEAPON
#define SIGHT(id) + 1
#include "gameplay/sight_list.h"
#undef SIGHT
};

/* Draws `spec`'s own mesh about the origin. Non-zero when it drew; zero when
   the weapon names no mesh, or the file would not load, and the caller should
   fall back to the boxes it drew before there was one.

   Loads on the first call, which has to be one with a current GL context. */
/* `hidden_parts` is the gun's own list of material names it does not draw, or
   NULL for all of it - see WeaponSpec.hidden_parts. Taken as an argument rather
   than read off the model, because the loaded meshes are cached and shared by
   path: what one gun leaves off is not a property of the file. */
int weapon_model_draw(const WeaponSpec* spec, const char* const* hidden_parts);

/* Only the named parts of the same mesh, at the caller's current transform.
   What this exists for is the magazine during a reload: the gun is drawn
   without it, and it is drawn again here wherever the hand has taken it. */
int weapon_model_draw_only(const WeaponSpec* spec, const char* const* parts);

/* The same, for the optic a weapon carries, drawn about its reticle. The
   caller has already put the transform where the reticle goes, which for the
   viewmodel is weapon_spec_sight_height and weapon_spec_sight_forward.

   The mesh is the sight's own business - it names its file - so a new optic
   arrives here without this function learning anything about it. */
int weapon_model_draw_sight(const SightSpec* sight);

/* The scabbard, about its own origin - the koiguchi, the mouth of it. Its own
   entry point rather than a second WeaponSpec because a scabbard is not a
   weapon: it has no damage, no reach and nothing to hold it by, and giving it
   a spec so that one function could draw it would put a thing that cannot be
   used in the table of things that can. */
int weapon_model_draw_scabbard(const MeleeSpec* melee);

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
