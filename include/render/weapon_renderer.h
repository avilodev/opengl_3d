#ifndef WEAPON_RENDERER_H
#define WEAPON_RENDERER_H

#include "gameplay/weapon.h"
#include "gameplay/third_person_camera.h"
#include "render/character_library.h"

/* World-space shot effects. Drawn with the world's camera still on the matrix
   stack, after the arena so they blend against it. */
void weapon_renderer_draw_effects(const Weapon* weapon);

/* The optic `spec` carries, on the rail of a gun the caller has already
   placed: `sight_height` up and `sight_forward` forward of that gun's origin.

   Public because the gun is drawn twice - in the player's hands here, and in
   the body's right hand by the character renderer - and a sight that only one
   of them knew about would be a rifle that grew an optic when the camera
   moved. Falls back to boxes for a sight with no mesh. */
void weapon_renderer_draw_sight(const WeaponSpec* spec);

/* The gun in the player's hands. Owns its own projection and depth clear, so it
   sits in front of the world instead of clipping into whatever the player is
   standing next to. First person only.

   `arms`/`arms_rig` name the body rig the first-person arms are cut out of, and
   may be NULL and CHARACTER_RIG_NONE for no arms. They are drawn here rather
   than by the caller because they belong inside this pass: they share the gun's
   near plane and its cleared depth, and a hand drawn through the world's
   projection would be a hand that parallaxes off the grip it is holding. */
/* Where the animated hand puts a melee weapon this frame, in camera space.

   Asked here rather than solved inside the draw because the pose has to be on
   the weapon BEFORE anything composes with it: the tracer origin, the sword
   mesh and the arms all read weapon_viewmodel_pose, and a base published after
   that call would be a frame behind the hand holding it. Returns 0 for a
   firearm, or when the rig is not loaded yet. */
int weapon_renderer_melee_base(const CharacterLibrary* arms, int arms_rig,
                               const Weapon* weapon, ViewmodelPose* out);

void weapon_renderer_draw_viewmodel(const Weapon* weapon,
                                    const PlayerController* player,
                                    const ThirdPersonCamera* camera,
                                    float aspect_ratio,
                                    const CharacterLibrary* arms,
                                    int arms_rig);

#endif
