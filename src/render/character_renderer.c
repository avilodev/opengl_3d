#include "render/character_renderer.h"
#include "render/character_library.h"
#include "render/character_transform.h"
#include "render/primitives.h"
#include "render/weapon_model.h"
#include "render/weapon_renderer.h"
#include "gameplay/movement_tuning.h"
#include "gameplay/weapon_spec.h"
#include "utils/math_utils.h"

#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The character rig: proportions, colours, and how movement state reads as a
   pose. The body itself is the animated mannequin when one loads and the seven
   boxes it grew from when one does not, but both hang off the same transform,
   so a lean or a slide looks the same either way.

   Which mesh that is comes from a CharacterLibrary the caller owns, named by
   the instance's rig index. Nothing here loads a file or holds one, so a second
   character type is an entry in that library rather than an edit to this file.

   Where the body stands and where the gun in its hand is are not here: they
   are arithmetic, they are wanted by things that have no business linking a
   renderer, and they live in character_transform.c. What is left here is the
   drawing - GL state, the box fallback, and the per-body update that advances
   a clip and skins a mesh. */

/* Matched to the animation's own cross-fade, so the body sinks exactly as the
   hang pose comes up. */
#define CHARACTER_HANG_BLEND_TIME 0.12f

/* Where the palm sits relative to the lip, in the rig's own units: back from
   the edge and a little under it, so the fingers take the corner rather than
   the wrist landing on it. */
#define CHARACTER_GRIP_PALM_INSET 0.06f
#define CHARACTER_GRIP_LIP_DROP 0.02f

/* The gun in the body's right hand, carried by the bone rather than posed:
   whatever the clip does with the arm, the rifle goes with it, and no clip has
   to know the rifle exists. Drawn inside the rig's scale, so a body standing
   two and a half metres tall carries a rifle in proportion to itself rather
   than a real one that would read as a toy in its hands. */
static void draw_held_weapon(const SkinnedModel* model, const Pose* pose,
                             const WeaponSpec* spec) {
    /* Empty hands draw nothing, which is what a body carrying no weapon should
       look like. */
    if (!spec) return;

    /* The same transform a tracer asks for when it needs to know where this
       gun's barrel is - see character_transform.h. Composed there rather than
       here so the drawn gun and the streak leaving it cannot be two answers. */
    Mat4 grip;
    if (!character_held_weapon_transform(model, pose, spec, &grip)) return;

    glPushMatrix();
    glMultMatrixf(grip.m);
    /* The same parts left off as in first person: the body carries the weapon
       it is holding, so it carries it built the same way. Taking the list from
       that weapon's own spec rather than repeating it here is what stops the
       third-person gun growing furniture the first-person one has had
       removed. */
    weapon_model_draw(spec, spec->hidden_parts);
    /* And its optic, on the same rail the viewmodel bolts one to. A weapon
       with no gun half has no rail and so nothing to bolt - which is the check
       here rather than a null test on the sight, because it is the class that
       decides whether the question makes sense at all. */
    if (weapon_spec_firearm(spec)) weapon_renderer_draw_sight(spec);
    glPopMatrix();
}

/* The ledge point the hand is reaching for, in the model's own space: undo the
   translate, the heading and the scale the body is drawn through, then step
   back from the lip so the fingers rather than the wrist meet the corner.
   Non-zero when there is a ledge to reach for.

   The lean and the wall-run tilt further down the stack are not undone. They
   are zero while hanging, and a degree of error in a grip nobody is looking
   at closely is not worth carrying the whole transform chain in here.

   The heading is last frame's, because this runs before the update that decides
   this frame's. A hang hands the body back to its travelling heading, so the
   two agree by the time anyone is hanging; what the stale frame costs is the
   tail of a turn during the twelfth of a second the pose is fading in. */
static int hang_grip_pose_space(const SkinnedModel* model,
                                const CharacterInstance* instance,
                                const CharacterVisualState* state,
                                float drop, float out[3]) {
    if (instance->hang_blend <= 0.0f) return 0;
    float scale = character_rig_scale(model);
    if (scale < 1e-5f) return 0;

    float vx = state->grip_x - state->x;
    float vy = state->grip_y - (state->y + drop);
    float vz = state->grip_z - state->z;

    float heading = character_animation_body_yaw(&instance->animation) *
                    (float)MATH_DEG_TO_RAD;
    float cosine = cosf(heading), sine = sinf(heading);
    out[0] = (vx * cosine - vz * sine) / scale;
    out[1] = vy / scale + model->min_y - CHARACTER_GRIP_LIP_DROP;
    out[2] = (vx * sine + vz * cosine) / scale - CHARACTER_GRIP_PALM_INSET;
    return 1;
}

/* The scabbard on the hip, and the sword in it when it is not in the hand.

   Drawn from the same transform character_scabbard_transform hands the probe,
   so what a screenshot shows and what the check asserts are one thing. The
   sheathed sword walks back along its own blade axis from the koiguchi - a
   katana is curved and can only enter and leave along that axis, which is
   what makes the depth one number rather than a pose. */
static void draw_scabbard(const SkinnedModel* model, const Pose* pose,
                          const WeaponSpec* spec, int sheathed) {
    const MeleeSpec* melee = weapon_spec_melee(spec);
    if (!melee || !melee->scabbard_path) return;

    Mat4 saya;
    if (!character_scabbard_transform(model, pose, melee, &saya)) return;

    glPushMatrix();
    glMultMatrixf(saya.m);
    weapon_model_draw_scabbard(melee);

    if (sheathed) {
        /* The same step the first-person hip takes, asked of the one function
           that states it. */
        float step[3];
        weapon_spec_sheathed_step(melee, step);
        glPushMatrix();
        glTranslatef(step[0], step[1], step[2]);
        weapon_model_draw(spec, spec->hidden_parts);
        glPopMatrix();
    }
    glPopMatrix();
}

/* Two draws in the rig's own space. The scale that stands it at gameplay
   height and the lift onto its own feet are in the matrix the caller has
   already applied, so there is no transform left to do here - which is what
   lets the gun be posed against the same space the body is. */
static void draw_model_body(const SkinnedModel* model,
                            const CharacterInstance* instance) {
    skinned_model_draw(model, &instance->skin);
    /* The scabbard first: it is worn rather than held, so it is behind the
       hand in every pose where the two overlap. */
    draw_scabbard(model, &instance->pose, instance->weapon, instance->sheathed);
    if (!instance->sheathed)
        draw_held_weapon(model, &instance->pose, instance->weapon);
}

/* The original rig, still the fallback. Its one animation is a stride, which
   is all seven boxes can say. */
static void draw_box_body(const CharacterVisualState* state) {
    float stride = state->has_movement_input && !state->sliding
        ? sinf(state->walk_time) * 28.0f : 0.0f;

    glColor3f(.12f, .32f, .72f);
    glPushMatrix(); glTranslatef(0, 1.45f, 0); primitives_draw_box(.85f, 1.05f, .42f); glPopMatrix();
    glColor3f(.86f, .65f, .48f);
    glPushMatrix(); glTranslatef(0, 2.22f, 0); primitives_draw_box(.62f, .62f, .58f); glPopMatrix();

    glColor3f(.16f, .20f, .27f);
    glPushMatrix(); glTranslatef(-.24f, .65f, 0); glRotatef(stride, 1, 0, 0);
    glTranslatef(0, -.42f, 0); primitives_draw_box(.28f, .85f, .32f); glPopMatrix();
    glPushMatrix(); glTranslatef(.24f, .65f, 0); glRotatef(-stride, 1, 0, 0);
    glTranslatef(0, -.42f, 0); primitives_draw_box(.28f, .85f, .32f); glPopMatrix();

    glColor3f(.86f, .65f, .48f);
    glPushMatrix(); glTranslatef(-.58f, 1.72f, 0); glRotatef(-stride, 1, 0, 0);
    glTranslatef(0, -.38f, 0); primitives_draw_box(.25f, .82f, .27f); glPopMatrix();
    glPushMatrix(); glTranslatef(.58f, 1.72f, 0); glRotatef(stride, 1, 0, 0);
    glTranslatef(0, -.38f, 0); primitives_draw_box(.25f, .82f, .27f); glPopMatrix();
}

/* Everything a body needs before its first frame. Deferred to the first update
   for the same reason the rig is: a rig loads lazily, and an instance cannot be
   sized until there is a model to size it against. */
static int instance_ready(CharacterInstance* instance, const SkinnedModel* model) {
    if (instance->ready) return 1;
    if (!skinned_instance_init(&instance->skin, model)) return 0;
    /* The hold comes from what this body is carrying. A body with no weapon
       passes no clip set and gets no aim layer, which is the right look for
       empty hands rather than a fallback standing in for one. */
    character_animation_init(&instance->animation, model,
                             instance->weapon ? &instance->weapon->clips : NULL);
    instance->aim_weapon = instance->weapon;
    skinned_model_bind_pose(model, &instance->pose);
    instance->ready = 1;
    return 1;
}

/* The aim layer follows what the body is holding NOW.

   It is resolved from the weapon's clip set, and that used to happen once, on
   the frame a body first drew - which was true for as long as a body picked up
   one weapon and kept it. It no longer is: a class change swaps the spec under
   a body that is already standing there, and the mesh in its hand changes
   because that is read live. The aim clips did not, so a body that came up
   holding a sword went on aiming with the sword's single pose while carrying a
   rifle - a gun that cannot pitch, held by a chest that cannot either, with a
   muzzle nowhere near where the rounds leave.

   Compared by pointer because specs are static and one per weapon, so the same
   weapon is the same address. */
static void aim_clips_follow_weapon(CharacterInstance* instance,
                                    const SkinnedModel* model) {
    if (instance->aim_weapon == instance->weapon) return;
    character_animation_set_clips(&instance->animation, model,
                                  instance->weapon ? &instance->weapon->clips
                                                   : NULL);
    instance->aim_weapon = instance->weapon;
}

void character_instance_init(CharacterInstance* instance, int rig) {
    if (!instance) return;
    memset(instance, 0, sizeof *instance);
    instance->rig = rig;
    /* Empty hands until a caller says otherwise. A body that draws nothing is
       a better default than one that draws somebody else's rifle. */
    instance->weapon = NULL;
    instance->sheathed = 0;
}

/* Both hands onto a sheathed sword, if that is what this body is wearing. */
static void worn_sword_reach(const SkinnedModel* model,
                             CharacterInstance* instance) {
    if (!instance->sheathed) return;
    const MeleeSpec* melee = weapon_spec_melee(instance->weapon);
    if (!melee || !melee->scabbard_path) return;

    Mat4 saya;
    if (!character_scabbard_transform(model, &instance->pose, melee, &saya))
        return;
    /* The sword sits back inside the mouth along its own axis - the step both
       renderers take, asked of the one function that states it. */
    float step[3];
    weapon_spec_sheathed_step(melee, step);
    Mat4 sword = mat4_multiply(saya,
                               mat4_translation(step[0], step[1], step[2]));

    float right[3], left[3];
    mat4_transform_point(&sword, instance->weapon->grip_right, right);
    mat4_transform_point(&saya, melee->saya_grip, left);
    character_animation_reach_hands(model, right, left, &instance->pose);
}

void character_renderer_update(CharacterLibrary* library, CharacterInstance* instance,
                               const CharacterVisualState* state, float delta_time,
                               int visible) {
    if (!library || !instance || !state) return;
    /* The one place a rig is allowed to come off disk. The draw takes a const
       library and so cannot, which is the whole point of the split. */
    SkinnedModel* model = character_library_load(library, instance->rig);
    if (!model || !instance_ready(instance, model)) return;
    aim_clips_follow_weapon(instance, model);

    float target = state->ledge_hanging ? 1.0f : 0.0f;
    if (delta_time > 0.0f) {
        float step = delta_time / CHARACTER_HANG_BLEND_TIME;
        if (instance->hang_blend < target)
            instance->hang_blend = fminf(target, instance->hang_blend + step);
        else if (instance->hang_blend > target)
            instance->hang_blend = fmaxf(target, instance->hang_blend - step);
        instance->hang_time = state->ledge_hanging
            ? instance->hang_time + delta_time : 0.0f;
    }

    /* Before the update, because switching into the hang samples the pose it
       is fading from, and that pose has to already be reaching. */
    float grip[3] = {0.0f, 0.0f, 0.0f};
    int reaching = hang_grip_pose_space(model, instance, state,
                                        character_hang_drop(model, instance, state), grip);
    character_animation_set_grip(&instance->animation, grip[0], grip[1], grip[2],
                                 reaching);

    character_animation_update(&instance->animation, model, state, delta_time);

    /* The POSE is composed whatever `visible` says, and only the skinning below
       is skipped. The two used to be on the same side of that test, and the
       difference between them is the difference between a drawing and a fact:
       skinned vertices are for looking at, and the pose is where this body's
       hands ARE.

       Something outside the renderer reads it. A melee weapon's hit volume is
       swept off the body's own hand - see character_weapon_world and main.c -
       and in first person `visible` is 0, so the hand this returned was the
       BIND pose and stayed there: arms at the sides, the blade hanging into the
       floor beside the player's foot, identical on every frame. A sweep between
       two identical transforms has no length, so a sword that swung perfectly
       well on screen cut a stationary segment by the player's boot and nothing
       else. The bug was invisible from the chair in the exact way a hit test
       always is - the swing plays, and the damage simply never arrives.

       Composing it costs one clip sample and a few blends over seventy bones.
       The skinning it still guards is per-vertex, which is the cost that
       actually wanted saving. */
    character_animation_pose(&instance->animation, model, &instance->pose);
    /* A worn sword is carried with both hands on it: the weapon hand on the
       tsuka standing out of the scabbard's mouth, the off hand steadying the
       scabbard below it. Layered onto the finished pose rather than authored
       as a clip, because the library has no such clip and because the points
       it reaches for are the spec's own - the same grip_right, saya_grip and
       sheathed_offset the first-person hands close on. One set of facts, two
       solvers: the viewmodel solves human-sized arms in camera space and this
       solves the body's own, and what they agree about is where the hands are
       ON the sword.

       Last, and after every layer, because the aim and the twist move the
       shoulders and the scabbard hangs off a pelvis those layers also turn.
       Composed off the pose as it stands, so the arms reach the sheath that is
       actually there this frame rather than the one the bind pose had. */
    worn_sword_reach(model, instance);
    if (!visible) return;

    skinned_model_skin(model, &instance->pose, &instance->skin);
    /* Straight onto the card, here rather than in the draw: this runs once a
       frame per body and the draw may not run at all, or - if the world ever
       gets a second pass that shows the body - more than once. The vertices
       should cross the bus exactly as often as they change. */
    skinned_instance_upload(model, &instance->skin);
}

void character_instance_free(CharacterInstance* instance) {
    if (!instance) return;
    /* Buffers before arrays: the first needs a current context and the second
       deliberately does not, so they cannot be one call. */
    skinned_instance_free_buffers(&instance->skin);
    skinned_instance_free(&instance->skin);
    instance->ready = 0;
}

void character_renderer_draw(const CharacterLibrary* library,
                             const CharacterInstance* instance,
                             const CharacterVisualState* state) {
    if (!library || !instance || !state) return;
    /* The mannequin only if this instance has been through an update: an
       instance with no skinned vertices yet has nothing to draw, and the boxes
       are the honest fallback for it as much as for a missing file. Asking a
       const library also means a rig that has not loaded yet reads as absent
       rather than being loaded from inside a draw. */
    const SkinnedModel* model = instance->ready
        ? character_library_model(library, instance->rig) : NULL;

    Mat4 body = character_body_transform(instance, state, model);
    glPushMatrix();
    glMultMatrixf(body.m);
    if (model) draw_model_body(model, instance);
    else draw_box_body(state);
    glPopMatrix();
}
