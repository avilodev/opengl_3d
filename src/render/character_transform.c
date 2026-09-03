#include "render/character_transform.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>
#include <stddef.h>

/* Where a body and its rifle are, as arithmetic. See the header for why this is
   not in character_renderer.c any more.

   The transform is composed as a matrix rather than pushed onto the
   fixed-function stack a call at a time. It reaches GL through glMultMatrixf
   either way and the drawn result is identical, but a matrix is a value the
   rest of the engine can use: a shader backend wants this and not a replay of a
   dozen glRotatef calls, and so does a shadow pass, a bounds test, or a tracer
   asking where the barrel of the held gun ended up. */

/* Height the box body's lean pivots about - roughly the waist, so leaning
   swings its shoulders without dragging its feet sideways. Only the box rig
   uses it: the mannequin bends its spine instead, which needs no pivot because
   the joints are where the bending actually happens. */
#define CHARACTER_BOX_LEAN_PIVOT 1.15f

/* The bone the gun hangs off, and where on it. The rig's hand bone points +y
   along the fingers, +z toward the index side, and -x out through the palm, so
   the hold is a short step into the palm and forward toward the knuckles.
   Measured in the rig's own metres, before the scale that stands it up at
   gameplay height, which is why these are hand-sized numbers rather than
   player-sized ones. */
#define CHARACTER_WEAPON_BONE "hand_r"

/* The pelvis and not spine_01. A scabbard is thrust through a belt, so it
   rides the hips: it must not swing with the chest when the body twists to
   look somewhere, which is exactly what hanging it off the spine would do, and
   the twist this rig applies to aim is large enough that the sword would
   visibly lead the hips into every turn. */
#define CHARACTER_SCABBARD_BONE "pelvis"
#define CHARACTER_GRIP_INTO_PALM -0.020f
#define CHARACTER_GRIP_ALONG_HAND 0.060f

/* The middle of the rifle's pistol grip, in the model's own space: on the
   centreline, below the bore, a hair behind the origin. This is the point that
   ends up in the fist, so it is the mesh's number and not a preference. */
#define CHARACTER_GRIP_X 0.000f
#define CHARACTER_GRIP_Y -0.076f
#define CHARACTER_GRIP_Z -0.003f

/* How far above its own feet the hang pose's gripping hand reaches, in the
   rig's own units. Multiplied by the same scale the body is drawn at, this is
   what lets the hand be placed on the lip rather than near it. Measured off
   the authored pose, then shortened a little: at the authored figure the body
   hangs exactly an arm's length below the lip and the reach solves to a locked
   straight arm, which both reads as stiff and leaves the hand a few
   centimetres short whenever the ledge is a touch further out. Sitting the
   body higher keeps a bend in the elbow for the reach to spend. */
#define CHARACTER_HANG_REACH 1.955f

/* The body's feet stay where they were when it grabbed, and the ledge can be
   anything from a third of a metre to two and a third above them - while
   the body is two and a half metres tall. So the lip is always below head
   height, and a body drawn at its own feet cannot hang from it: it
   stands in the air beside it. The drawn body drops until its hands meet the
   lip, which is what a hang looks like and what the collision capsule, which
   does not move, would otherwise never show.

   Capped, because the drop grows as the ledge gets lower, and a body sunk far
   enough under its own capsule puts its legs through whatever is below. The
   cap covers every ledge from about a metre up, which is every ledge anyone
   hangs off in practice: a player rising toward one grabs it the frame it
   comes into range, which is at the top of the band. Below that the hands ride
   above the lip rather than the legs going through the floor. */
#define CHARACTER_HANG_MAX_DROP 1.6f

/* A body on the end of an arm is never quite still. The hand is solved onto
   the ledge every frame, so the weight can shift without the grip sliding -
   which is the whole reason the arm reaches rather than holding a pose. */
#define CHARACTER_HANG_SWAY 0.020f
#define CHARACTER_HANG_SWAY_RATE 1.35f

/* Scaled to the rig's height and stood on its own feet, so a model authored at
   any size or with its origin anywhere still meets the floor at y=0 with its
   head where the camera expects one. The extent is the bind pose's, not the
   current frame's, so the body does not grow and shrink as it moves. */
/* The body is drawn at gameplay height whatever size the rig was authored at,
   and several things besides the draw need to know by how much. */
float character_rig_scale(const SkinnedModel* model) {
    float height = model->max_y - model->min_y;
    return height > 0.01f ? CHARACTER_RIG_HEIGHT / height : 1.0f;
}

/* How far to sink the drawn body so its hands land on the ledge it is holding.
   Zero when not hanging, and never positive - a hang lowers a body, it never
   lifts one. */
float character_hang_drop(const SkinnedModel* model,
                          const CharacterInstance* instance,
                          const CharacterVisualState* state) {
    if (instance->hang_blend <= 0.0f) return 0.0f;
    float reach = CHARACTER_HANG_REACH * character_rig_scale(model);
    float lip_above_feet = state->grip_y - state->y;
    float drop = lip_above_feet - reach;
    if (drop > 0.0f) drop = 0.0f;
    if (drop < -CHARACTER_HANG_MAX_DROP) drop = -CHARACTER_HANG_MAX_DROP;
    drop += sinf(instance->hang_time * CHARACTER_HANG_SWAY_RATE) * CHARACTER_HANG_SWAY;
    return drop * instance->hang_blend;
}

/* The order is exactly what GL post-multiplication produced - each step applies
   inside the ones before it - so what reaches the rasteriser is unchanged. */
Mat4 character_body_transform(const CharacterInstance* instance,
                              const CharacterVisualState* state,
                              const SkinnedModel* model) {
    Mat4 m = mat4_translation(state->x,
                              state->y + (model ? character_hang_drop(model, instance, state) : 0.0f),
                              state->z);

    /* Heading is atan2(vx, vz), so a body at zero heading faces +Z. The
       mannequin's toes sit forward of its ankles on +Z, so it agrees, and
       neither rig needs a correction here.

       The mannequin stands where the animation says rather than where the body
       is travelling, because on the sight those are different things: the chest
       and the gun turn to the crosshair, and the legs are twisted back under
       them. The box rig has no waist to twist with, so it keeps the travel. */
    float yaw = model ? character_animation_body_yaw(&instance->animation)
                      : state->heading_degrees;
    m = mat4_multiply(m, mat4_rotation_degrees(yaw, 0, 1, 0));

    if (state->wall_running)
        m = mat4_multiply(m, mat4_rotation_degrees((float)state->wall_run_side * 14.0f,
                                                   0, 0, 1));
    if (state->mantling)
        m = mat4_multiply(m, mat4_rotation_degrees(-22.0f, 1, 0, 0));

    /* The crouch squash is the box rig's way of getting low. The mannequin has
       clips that actually crouch and slide, so squashing it too would flatten
       a pose that is already down there. */
    if (state->crouching && !model) {
        m = mat4_multiply(m, mat4_scaling(1.0f, state->sliding ? 0.52f : 0.68f, 1.0f));
        if (state->sliding)
            m = mat4_multiply(m, mat4_rotation_degrees(-10.0f, 1, 0, 0));
    }

    /* The lean.

       Q publishes positive lean, which is a peek to the body's own left, and
       the model's own left is +x: the rig faces +z with +y up, so up cross
       forward is +x, which is the side the rig's left arm is on. Both branches
       below take the head that way.

       The mannequin only gets the hip shift here. The bend itself is on the
       spine - see the lean channel in character_animation's TWIST_BONES - which
       is what leaves the feet planted while the head, the shoulders and the
       rifle in the hands all go out past the corner. Rolling the whole
       transform, which is what this used to do, swung the legs out with
       everything else and read as a body falling sideways rather than peeking.

       The box rig has no spine to bend, so it keeps the whole-body tilt: seven
       boxes leaning as one is as close to a peek as seven boxes get. */
    m = mat4_multiply(m, mat4_translation(state->lean * CHARACTER_LEAN_OFFSET, 0, 0));
    if (!model) {
        m = mat4_multiply(m, mat4_translation(0, CHARACTER_BOX_LEAN_PIVOT, 0));
        m = mat4_multiply(m, mat4_rotation_degrees(state->lean * CHARACTER_BOX_LEAN_TWIST,
                                                   0, 1, 0));
        m = mat4_multiply(m, mat4_rotation_degrees(-state->lean * CHARACTER_BOX_LEAN_ROLL,
                                                   0, 0, 1));
        m = mat4_multiply(m, mat4_translation(0, -CHARACTER_BOX_LEAN_PIVOT, 0));
    }

    /* Scaled to the rig's height and stood on its own feet, so a model
       authored at any size or with its origin anywhere still meets the floor
       at y=0 with its head where the camera expects one. The extent is the
       bind pose's, not the current frame's, so the body does not grow and
       shrink as it moves. */
    if (model) {
        float scale = character_rig_scale(model);
        m = mat4_multiply(m, mat4_scaling(scale, scale, scale));
        m = mat4_multiply(m, mat4_translation(0, -model->min_y, 0));
    }
    return m;
}

int character_held_weapon_transform(const SkinnedModel* model, const Pose* pose,
                                    Mat4* out) {
    Mat4 hand;
    if (!skinned_model_bone_matrix(model, pose, CHARACTER_WEAPON_BONE, &hand))
        return 0;

    /* Bone, then into the palm, then a quarter turn about the bone's own x
       that stands the gun up in the fist - its forward runs out along the
       fingers and its top points the way the trigger finger does - then back
       by the grip, which is the point of the mesh that ends up held. */
    Mat4 grip = mat4_multiply(hand,
        mat4_translation(CHARACTER_GRIP_INTO_PALM, CHARACTER_GRIP_ALONG_HAND, 0.0f));
    grip = mat4_multiply(grip, mat4_rotation_degrees(90.0f, 1, 0, 0));
    grip = mat4_multiply(grip,
        mat4_translation(-CHARACTER_GRIP_X, -CHARACTER_GRIP_Y, -CHARACTER_GRIP_Z));
    *out = grip;
    return 1;
}

int character_scabbard_transform(const SkinnedModel* model, const Pose* pose,
                                 const MeleeSpec* melee, Mat4* out) {
    if (!model || !pose || !melee || !out) return 0;
    Mat4 hips;
    if (!skinned_model_bone_matrix(model, pose, CHARACTER_SCABBARD_BONE, &hips))
        return 0;

    /* Off the hip, then the cant, in that order: the offset says where on the
       belt it is threaded and the angles say how it hangs there, and swapping
       them would swing the scabbard around the pelvis as it was tilted rather
       than tilting it where it sits.

       Yaw, then pitch, then roll - the same order the viewmodel names its
       three, so a number that means "canted back" means the same thing in both
       halves of the game. */
    Mat4 worn = mat4_multiply(hips,
        mat4_translation(melee->scabbard_offset[0], melee->scabbard_offset[1],
                         melee->scabbard_offset[2]));
    worn = mat4_multiply(worn,
        mat4_rotation_degrees(melee->scabbard_yaw, 0, 1, 0));
    worn = mat4_multiply(worn,
        mat4_rotation_degrees(melee->scabbard_pitch, 1, 0, 0));
    worn = mat4_multiply(worn,
        mat4_rotation_degrees(melee->scabbard_roll, 0, 0, 1));
    *out = worn;
    return 1;
}

int character_weapon_muzzle(const SkinnedModel* model,
                            const CharacterInstance* instance,
                            const CharacterVisualState* state,
                            float muzzle_up, float muzzle_forward,
                            float out[3]) {
    /* The same test the draw makes: a body that has not been through an update
       has no pose to take a point off, and the box fallback has no hand. */
    if (!model || !instance || !state || !out || !instance->ready) return 0;

    Mat4 grip;
    if (!character_held_weapon_transform(model, &instance->pose, &grip)) return 0;

    /* Body, then the hand's gun, then the muzzle on that gun - the whole chain
       the drawn rifle goes through, so the point comes out where the barrel is
       and not near it. The muzzle offsets are the weapon model's own metres and
       the gun is drawn inside the rig's scale, which the body transform already
       carries, so nothing has to be scaled here by hand. */
    Mat4 world = mat4_multiply(character_body_transform(instance, state, model), grip);
    const float local[3] = {0.0f, muzzle_up, -muzzle_forward};
    mat4_transform_point(&world, local, out);
    return 1;
}
