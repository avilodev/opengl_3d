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
    if (!character_held_weapon_transform(model, pose, &grip)) return;

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

/* Two draws in the rig's own space. The scale that stands it at gameplay
   height and the lift onto its own feet are in the matrix the caller has
   already applied, so there is no transform left to do here - which is what
   lets the gun be posed against the same space the body is. */
static void draw_model_body(const SkinnedModel* model,
                            const CharacterInstance* instance) {
    skinned_model_draw(model, &instance->skin);
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
    skinned_model_bind_pose(model, &instance->pose);
    instance->ready = 1;
    return 1;
}

void character_instance_init(CharacterInstance* instance, int rig) {
    if (!instance) return;
    memset(instance, 0, sizeof *instance);
    instance->rig = rig;
    /* Empty hands until a caller says otherwise. A body that draws nothing is
       a better default than one that draws somebody else's rifle. */
    instance->weapon = NULL;
}

void character_renderer_update(CharacterLibrary* library, CharacterInstance* instance,
                               const CharacterVisualState* state, float delta_time,
                               int visible) {
    if (!library || !instance || !state) return;
    /* The one place a rig is allowed to come off disk. The draw takes a const
       library and so cannot, which is the whole point of the split. */
    SkinnedModel* model = character_library_load(library, instance->rig);
    if (!model || !instance_ready(instance, model)) return;

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
    if (!visible) return;
    character_animation_pose(&instance->animation, model, &instance->pose);
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
