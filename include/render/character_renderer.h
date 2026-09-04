#ifndef CHARACTER_RENDERER_H
#define CHARACTER_RENDERER_H

#include "render/character_animation.h"
#include "render/character_library.h"
#include "render/character_state.h"
#include "render/skinned_model.h"
#include "gameplay/weapon_spec.h"

/* Draws a character's body. This is the character rig, not renderer plumbing:
   it owns proportions, colours, and how movement state reads as a pose.

   Two halves, and the split is the point of the file. The rig itself - the
   mesh, the skeleton, forty-three clips, the textures - loads once and never
   changes, so every body in the world shares one copy of it. Everything a body
   does with that rig is per body, and that is a CharacterInstance: a playhead,
   a pose, the vertices skinned from it, and the ledge blend. Two players need
   two of these and one model, which is what makes a second body cost a few
   hundred kilobytes rather than a second import. */

/* One body's worth of rig state. Initialise it with character_instance_init,
   naming the rig it wears, then hand it to character_renderer_update; the
   first call fills the rest in. Free it with character_instance_free, which
   needs a current GL context - the instance owns the vertex buffer its skinned
   vertices are streamed through. */
typedef struct {
    /* Which rig in the caller's library this body wears. An index rather than
       a pointer, so a body is plain data that could have arrived over a wire,
       and so a library that reloads its rigs does not leave bodies pointing at
       freed models. CHARACTER_RIG_NONE, or an index the library never handed
       out, draws the box fallback. */
    int rig;

    /* What this body is holding, or NULL for empty hands.

       The renderer used to name WEAPON_SPEC_ASSAULT_RIFLE where it drew the
       gun in the hand, which meant every body in the world carried the issued
       rifle whatever it was actually holding - and meant a second weapon could
       not be seen in third person at all, however carefully the first-person
       half had been taught about it. A pointer per body is what makes the two
       views agree by construction rather than by both being edited.

       Not owned: specs are static and outlive every body that points at one. */
    const WeaponSpec* weapon;

    /* Whether a melee weapon is in its scabbard rather than in the hand. One
       bit, shared by both views off the Weapon that owns it, because a sword
       that is sheathed in third person and drawn in first is two swords. */
    int sheathed;

    CharacterAnimation animation;
    /* Which weapon the animation's aim clips were resolved against, so a body
       handed a different one mid-round re-resolves them instead of going on
       aiming with the last one's. Set by the update; nothing outside it should
       write this. See character_animation_set_clips. */
    const WeaponSpec* aim_weapon;
    Pose pose;
    SkinnedInstance skin;
    /* Eased rather than switched, so grabbing and letting go of a ledge lowers
       and lifts the body over the same span the pose cross-fades in. */
    float hang_blend;
    float hang_time;
    int ready;
} CharacterInstance;

/* Advances the animation and skins the mesh. Once per frame per body, before
   drawing: the world is drawn twice whenever a magnified sight is up, and a
   body that skinned itself inside the draw would pay for it twice and, worse,
   advance its clock twice on exactly the frames a scope was up.

   `state` is everything the rig reads about this body - see character_state.h.
   It carries where the body is, what it is doing, and where it is looking, and
   nothing about who is watching it: the local player's comes off their
   controller and a remote player's comes off the wire, and this cannot tell
   which.

   The look direction outranks the direction the body is travelling, sight up or
   not: the chest, the arms, the head and the rifle turn to it, and the legs are
   twisted back underneath to keep the feet on the movement. A player strafing
   sideways is drawn facing where they are pointed, because that is what anyone
   watching them needs to see, whether or not there is a trigger involved.

   `delta_time` is frame time. Pass zero to hold the current pose, which is
   what a paused game wants.

   `visible` is whether the body will be drawn at all - it is not, in first
   person, and it is not for a body behind you. The clip clock runs either way,
   so the pose is already right the frame the view switches back, but the eight
   thousand vertex transforms behind it are skipped for a body nobody can
   see. */
void character_renderer_update(CharacterLibrary* library, CharacterInstance* instance,
                               const CharacterVisualState* state, float delta_time,
                               int visible);

/* Draws the pose the last update left. Safe to call more than once a frame.
   `state` must be the same one that update was given.

   The library is const because a draw must never load an asset: a body whose
   rig has not been through an update draws the box fallback rather than
   stalling the frame on a file. */
void character_renderer_draw(const CharacterLibrary* library,
                             const CharacterInstance* instance,
                             const CharacterVisualState* state);

/* Sets a body up to wear `rig`, an index from character_library_add. Cheap
   and allocation-free: everything else is filled in by the first update, once
   there is a loaded model to size the body against. */
void character_instance_init(CharacterInstance* instance, int rig);

/* Releases one body's arrays and its vertex buffer. Needs a current GL
   context. The rig it was wearing stays loaded - it belongs to the library. */
void character_instance_free(CharacterInstance* instance);

#endif
