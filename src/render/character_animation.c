#include "render/character_animation.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>
#include <string.h>

/* Movement state to clips. The table below is the whole mapping, so retiming a
   jump or swapping which clip a sprint uses is an edit to data rather than to
   control flow. */

#define FADE_SECONDS 0.12f

/* The clip the body is playing, sampled and cross-faded, with none of the
   layers that go over it. Defined further down, next to the layers it is the
   base for; declared here because switching a role snapshots one. */
static void compose_base(const CharacterAnimation* animation,
                         const SkinnedModel* model, Pose* out);

/* A one-shot owns the body for `hold` seconds and then hands back to whatever
   the state asks for. Holding for less than the clip's own length is the
   normal case, not a shortcut: the library's landing is a second and a quarter
   of settling, and a player who is already running again by then wants to be
   running, not finishing an animation about having landed. Zero means the role
   loops and never expires. */
static const struct {
    const char* name;
    float hold;
} ROLES[CHARACTER_CLIP_COUNT] = {
    [CHARACTER_CLIP_IDLE]         = { "Idle_Loop",        0.0f },
    [CHARACTER_CLIP_WALK]         = { "Walk_Loop",        0.0f },
    [CHARACTER_CLIP_JOG]          = { "Jog_Fwd_Loop",     0.0f },
    [CHARACTER_CLIP_SPRINT]       = { "Sprint_Loop",      0.0f },
    [CHARACTER_CLIP_CROUCH_IDLE]  = { "Crouch_Idle_Loop", 0.0f },
    [CHARACTER_CLIP_CROUCH_WALK]  = { "Crouch_Fwd_Loop",  0.0f },
    [CHARACTER_CLIP_JUMP_START]   = { "Jump_Start",       0.30f },
    [CHARACTER_CLIP_JUMP_LOOP]    = { "Jump_Loop",        0.0f },
    [CHARACTER_CLIP_JUMP_LAND]    = { "Jump_Land",        0.35f },
    [CHARACTER_CLIP_SLIDE_START]  = { "Slide_Start",      0.30f },
    [CHARACTER_CLIP_SLIDE_LOOP]   = { "Slide_Loop",       0.0f },
    [CHARACTER_CLIP_SLIDE_EXIT]   = { "Slide_Exit",       0.35f },
    [CHARACTER_CLIP_CLIMB]        = { "ClimbUp_1m",       0.0f },
    /* The hang's clip is only the pose it is built on top of - Pistol_Aim_Up,
       for its gripping fingers and its raised shoulders. What makes it a hang
       is HANG_POSE below. */
    [CHARACTER_CLIP_HANG]         = { "Pistol_Aim_Up",    0.0f },
};

/* The wall hang, authored rather than animated.

   Nothing in the library is a ledge hang. Of eighty-six clips exactly one puts
   both hands above the head - Pistol_Aim_Up, which is a weapon held overhead -
   and every other candidate, ClimbUp_1m included, keeps the hands at or below
   it. So this pose is built: each entry points one bone along a direction in
   the model's own space, +y up and +z toward the wall the player is facing,
   which is the form it was authored in and the only form of it anyone can read
   and adjust. Bones absent from the table keep whatever the base clip gave
   them, which is why the fingers are already curled.

   The shape is the one Titanfall hangs in: the left hand up on the lip, the
   right arm down with the rifle hanging muzzle-first, the body straight, and
   the legs dangling with the knees a little bent. */
static const struct {
    const char* bone;
    float x, y, z;
} HANG_POSE[CHARACTER_HANG_BONES] = {
    {"spine_01",    0.00f, 0.99f, 0.10f},
    {"spine_02",    0.00f, 0.99f, 0.10f},
    {"spine_03",    0.00f, 0.99f, 0.08f},
    {"neck_01",     0.00f, 0.97f, 0.24f},
    /* The reaching arm. These are only what it falls back to - the solve below
       overrides both of them whenever there is a ledge to reach for. */
    {"upperarm_l",  0.20f, 0.96f, 0.20f},
    {"lowerarm_l",  0.06f, 0.99f, 0.12f},
    /* Palm forward over the lip, so the fingers drape across it rather than
       pointing at the sky. */
    {"hand_l",      0.05f, 0.30f, 0.95f},
    {"upperarm_r", -0.26f,-0.95f, 0.06f},
    {"lowerarm_r", -0.16f,-0.94f, 0.30f},
    {"hand_r",     -0.10f,-0.88f, 0.46f},
    {"thigh_l",     0.10f,-0.91f, 0.40f},
    {"thigh_r",    -0.10f,-0.91f, 0.40f},
    {"calf_l",      0.08f,-0.93f,-0.36f},
    {"calf_r",     -0.08f,-0.93f,-0.36f},
    {"foot_l",      0.05f,-0.55f,-0.83f},
    {"foot_r",     -0.05f,-0.55f,-0.83f},
    /* The gripping hand. Four fingers over the near edge and down the far
       face; the thumb on the near one, opposing them. The base clip curls them
       round a pistol grip, so leaving them out is what made the first version
       look like it was holding a rifle in mid air. */
    {"index_01_l", -0.10f, 0.02f, 0.99f},
    {"index_02_l", -0.08f,-0.72f, 0.69f},
    {"index_03_l", -0.06f,-0.96f, 0.24f},
    {"middle_01_l",-0.02f, 0.02f, 0.99f},
    {"middle_02_l",-0.02f,-0.72f, 0.69f},
    {"middle_03_l",-0.01f,-0.96f, 0.24f},
    {"ring_01_l",   0.06f, 0.02f, 0.99f},
    {"ring_02_l",   0.05f,-0.72f, 0.69f},
    {"ring_03_l",   0.04f,-0.96f, 0.24f},
    {"pinky_01_l",  0.14f, 0.02f, 0.99f},
    {"pinky_02_l",  0.11f,-0.72f, 0.69f},
    {"pinky_03_l",  0.08f,-0.96f, 0.24f},
    {"thumb_01_l", -0.30f,-0.34f, 0.89f},
    {"thumb_02_l", -0.22f,-0.66f, 0.72f},
    {"thumb_03_l", -0.16f,-0.84f, 0.52f},
};
/* Which way the elbow breaks while reaching: out from the body and a little
   back, so the arm bends the way an arm does instead of folding forward
   through the chest. Positive x is the model's own left, which is the side
   the reaching arm is on. */
static const float HANG_ELBOW_POLE[3] = {1.0f, -0.25f, -0.45f};

/* ---- the facing split ----

   Which bones carry a turn, and how much of it each takes. Two channels, both
   about the model's own vertical, summed per bone.

   `legs` is the aim facing. The pelvis takes the whole turn, which is what puts
   the legs on the movement; the three spine joints give it all back, which is
   what leaves the chest on the aim. Those shares sum to exactly one, and they
   are spread over three joints rather than dumped on one because a waist that
   turns eighty degrees at a single vertebra folds.

   `look` is the head. Its shares also sum to one, so the head arrives exactly on
   the camera, and the chest is in the table for a quarter of it: a head that
   turns alone reads as a body ignoring what it is looking at, and a quarter is
   enough of a lead to say the person turned rather than just their eyes. It also
   swings the gun a quarter of the way over, which is what carrying a rifle at
   the ready toward what you are looking at does.

   `lean` is the peek, and it is the one channel about a different axis: the
   model's own forward rather than its vertical, which makes it a sideways bend
   instead of a turn. Its shares also sum to one, but they do not cancel - they
   accumulate down the chain, because the point of a lean is that everything
   above the hips ends up displaced. It is all spine and above: the pelvis is
   the root of both legs, so a share there rolls the legs out from under the
   body - three centimetres of foot slide per share of a tenth, measured - and
   a peek that moves the feet is a sidestep. The neck and the head take enough
   between them to carry the eyes out past the shoulder without cocking the
   head over like a bird.

   Nothing is spent on the arms, and nothing needs to be. The rifle hangs off
   hand_r, which hangs off the chest, so bending the spine takes the gun and
   both hands with it - which is exactly what peeking round a corner does and
   the reason the lean belongs here rather than on the body transform, where it
   used to swing the legs out from under a body that should have kept its feet
   planted. */
static const struct {
    const char* bone;
    float legs;
    float look;
    float lean;
} TWIST_BONES[CHARACTER_TWIST_BONES] = {
    {"pelvis",    1.00f,  0.00f,  0.00f},
    {"spine_01", -0.34f,  0.00f,  0.28f},
    {"spine_02", -0.33f,  0.00f,  0.28f},
    {"spine_03", -0.33f,  0.25f,  0.24f},
    {"neck_01",   0.00f,  0.40f,  0.12f},
    {"Head",      0.00f,  0.35f,  0.08f},
};

/* How far round from the chest the legs are allowed to sit. A pure sideways
   strafe wants ninety, and this is deliberately a little short of it: the last
   few degrees are the difference between a body wound up and a body wrung out,
   and what they cost is a foot sliding by a hand's width over a stride. */
#define FACING_LEG_YAW_MAX 80.0f

/* And how far it may wind up while standing still. Shorter, because a planted
   body has nothing to explain the twist: the feet are what turn to follow the
   camera, and this is how long they wait before they do. */
#define FACING_STAND_TWIST_MAX 52.0f

/* Past this the legs turn round and the walk plays backwards instead. Halfway
   between a strafe and a backpedal, which is the one place in the range no
   input lands on: the eight directions a keyboard can ask for arrive at
   multiples of forty-five, so the flip never sits under a key the player is
   holding and cannot chatter about it. */
#define FACING_REVERSE_ANGLE 112.5f

/* Degrees a second the legs come round at while moving, and how long the walk
   takes to turn itself around. The turn is fast because it is a pivot, not a
   stride; the reverse is slower because it passes through standing still. */
#define FACING_LEG_TURN_RATE 540.0f
#define FACING_REVERSE_TIME 0.20f

/* How far the head may turn off the chest, and how fast it gets there. Eighty
   five is about a person's limit and it is a saturating clamp, not a refusal:
   a body running away from where it is looking turns its head as far as it
   goes and leaves it there, which is what someone does. */
#define FACING_LOOK_MAX 85.0f
#define FACING_LOOK_RATE 400.0f

/* Standing, the feet take a step once the waist runs out of twist, and the step
   is committed - it holds until they are within `SETTLE` of square - because
   releasing it the moment the twist came back inside the cap would shuffle them
   a few degrees every frame the camera stayed past the limit, instead of
   stepping once and being done. */
#define FACING_STEP_RATE 260.0f
#define FACING_STEP_SETTLE 10.0f

/* How long the body takes to hand its heading over to or from a clip that owns
   it - a slide, a mantle, a hang, a climb, a wall run. Short, because it is
   covering a transform snap rather than expressing anything, and matched to the
   clip cross-fade that runs alongside it. */
#define FACING_HANDOVER_TIME 0.14f

/* And degrees a second they unwind at while standing. Slow on purpose, and the
   one number here worth playing with: it is what decides whether a body turning
   on the spot looks planted or looks like it is on ice. Under a mouse flick the
   feet stay put and the waist takes it, up to the cap above; then they unwind
   over about a second, which is the shuffle a standing player does. */
#define FACING_LEG_SETTLE_RATE 60.0f

/* The bone the layer takes over from, and the two it lowers the gun with. */
#define AIM_UPPER_ROOT "spine_02"
#define AIM_ARM_ROOT_L "clavicle_l"
#define AIM_ARM_ROOT_R "clavicle_r"

/* Where the blend sits when the camera is level. The three poses run from a
   muzzle pitch of -78 degrees to +84, near enough linearly in the parameter
   between them, and level lands a little under halfway. */
#define AIM_LEVEL 0.442f

/* Parameter per degree of camera pitch. Tracking the camera exactly would want
   1/173, about half as much again as this. It is deliberately short: the poses
   at the ends of the trio are a lowered weapon and one held overhead, and
   driving the body all the way into them reads as a stoop and a salute rather
   than as aiming, however right the muzzle angle is. At this gain every pitch
   the camera can reach - it clamps to -35 and +70 - stays inside the part of
   the blend that still looks like a person aiming, and a third-person gun that
   is twenty degrees shy of the camera is not something anyone can see. */
#define AIM_PITCH_GAIN 0.0040f

/* On the sight it tracks exactly, because that is what the sight is for: a
   scoped rifle that points twenty degrees under the crosshair is the one case
   where the flattery above becomes a lie the player can see, and their own
   shots land where the camera says rather than where the body does. The trio
   spans a hundred and seventy-three degrees of muzzle pitch in the parameter
   between them, so its reciprocal is one-to-one. */
#define AIM_PITCH_GAIN_ADS (1.0f / 173.0f)

/* How far the arms drop toward the lowered pose when the sight is not up. The
   arms and not the spine: Aim_Down lowers the gun by folding the whole torso
   forward, so blending the spine toward it bows the head into a stoop, while
   the arms alone leave the body upright and just bring the muzzle down. */
#define AIM_CARRY_LOWER 0.60f

/* Every rotation above `node`, accumulated: the orientation the bone's parent
   frame has in the model's own space. Rotations only - a translation down the
   chain moves a bone without turning it, and what this is for is turning.

   Cheaper than the matrix walk it could have been, and exact for the same
   reason: the product of the local rotations IS the global rotation, provided
   nothing in the chain is scaled, and nothing in a skeleton is. */
static void chain_rotation(const Skeleton* skeleton, const Pose* pose, int node,
                           float out[4]) {
    int chain[SKIN_MAX_NODES];
    int depth = 0;
    for (int i = skeleton->parent[node]; i >= 0 && depth < SKIN_MAX_NODES;
         i = skeleton->parent[i])
        chain[depth++] = i;
    out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
    for (int i = depth - 1; i >= 0; --i)
        quat_multiply(out, pose->local[chain[i]].rotation, out);
}

/* The inverse of a unit rotation, which is its conjugate. */
static void quat_inverse(const float q[4], float out[4]) {
    out[0] = -q[0]; out[1] = -q[1]; out[2] = -q[2]; out[3] = q[3];
}

/* A fraction of a rotation: 0 leaves nothing of it, 1 leaves all of it.
   Normalized linear interpolation from the identity, which for the angles this
   is used on - a chest correction, never more than a right angle - differs from
   the great-circle version only in how evenly it is paced through the middle,
   and not at all at either end. The sign is taken off w so the interpolation
   goes the short way round: a quaternion and its negation are the same
   rotation, and one of the two is a turn the long way. */
static void quat_fraction(const float q[4], float amount, float out[4]) {
    float sign = q[3] >= 0.0f ? 1.0f : -1.0f;
    float x = q[0] * sign * amount;
    float y = q[1] * sign * amount;
    float z = q[2] * sign * amount;
    float w = 1.0f - amount + q[3] * sign * amount;
    float length = sqrtf(x*x + y*y + z*z + w*w);
    if (length < 1e-8f) { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; return; }
    out[0] = x / length; out[1] = y / length; out[2] = z / length; out[3] = w / length;
}

/* A bone's own direction: where its first child sits, in the bone's own
   rotated frame. That is what a HANG_POSE direction is measured against, so a
   bone with no child - the head, the finger tips - cannot be pointed and is
   dropped rather than being pointed along a guess. */
static int rest_axis(const Skeleton* skeleton, int node, float out[3]) {
    for (int i = 0; i < skeleton->node_count; ++i) {
        if (skeleton->parent[i] != node) continue;
        const float* t = skeleton->bind_local[i].translation;
        float length = sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
        if (length < 1e-6f) continue;
        out[0] = t[0]/length; out[1] = t[1]/length; out[2] = t[2]/length;
        return 1;
    }
    return 0;
}

/* Entries stay aligned with HANG_POSE - an unresolved bone is -1 rather than
   being squeezed out, so an entry's node and its direction never drift apart. */
/* Length of the bone that ends at `child`, which is how far its own origin
   sits from its parent's. */
static float bone_length(const Skeleton* skeleton, const char* child) {
    for (int i = 0; i < skeleton->node_count; ++i) {
        if (strcmp(skeleton->node_names[i], child) != 0) continue;
        const float* t = skeleton->bind_local[i].translation;
        return sqrtf(t[0]*t[0] + t[1]*t[1] + t[2]*t[2]);
    }
    return 0.0f;
}

static short find_node(const Skeleton* skeleton, const char* name) {
    for (int i = 0; i < skeleton->node_count; ++i)
        if (strcmp(skeleton->node_names[i], name) == 0) return (short)i;
    return -1;
}

static void resolve_hang_bones(CharacterAnimation* animation, const SkinnedModel* model) {
    const Skeleton* skeleton = &model->skeleton;
    animation->hang_count = 0;
    animation->hang_upper_l = find_node(skeleton, "upperarm_l");
    animation->hang_lower_l = find_node(skeleton, "lowerarm_l");
    animation->hang_upper_length = bone_length(skeleton, "lowerarm_l");
    animation->hang_lower_length = bone_length(skeleton, "hand_l");
    for (int i = 0; i < CHARACTER_HANG_BONES; ++i) {
        animation->hang_node[i] = -1;
        for (int n = 0; n < skeleton->node_count; ++n) {
            if (strcmp(skeleton->node_names[n], HANG_POSE[i].bone) != 0) continue;
            float axis[3];
            if (!rest_axis(skeleton, n, axis)) break;
            animation->hang_node[i] = (short)n;
            animation->hang_axis[i][0] = axis[0];
            animation->hang_axis[i][1] = axis[1];
            animation->hang_axis[i][2] = axis[2];
            animation->hang_count++;
            break;
        }
    }
}

static void resolve_twist_bones(CharacterAnimation* animation, const SkinnedModel* model) {
    animation->twist_count = 0;
    animation->twist_legs = 1;
    for (int i = 0; i < CHARACTER_TWIST_BONES; ++i) {
        animation->twist_node[i] = find_node(&model->skeleton, TWIST_BONES[i].bone);
        if (animation->twist_node[i] >= 0) animation->twist_count++;
        else if (i < CHARACTER_TWIST_LEG_BONES) animation->twist_legs = 0;
    }
    /* The leg chain is all four or none - the shares only cancel above the waist
       if every one of them lands, and a pelvis turned with half a spine to give
       it back would carry the turn into the gun, which is the one thing the
       split exists to prevent. The look bones are each their own affair: a
       missing neck costs some head turn and nothing else. */
    if (!animation->twist_legs)
        for (int i = 0; i < CHARACTER_TWIST_LEG_BONES; ++i)
            animation->twist_node[i] = -1;
    animation->twist_count = 0;
    for (int i = 0; i < CHARACTER_TWIST_BONES; ++i)
        if (animation->twist_node[i] >= 0) animation->twist_count++;
}

void character_animation_init(CharacterAnimation* animation,
                              const SkinnedModel* model,
                              const WeaponClipSet* clips) {
    memset(animation, 0, sizeof *animation);
    animation->oneshot_clip = -1;
    for (int i = 0; i < CHARACTER_CLIP_COUNT; ++i)
        animation->clip[i] = skinned_model_find_clip(model, ROLES[i].name);
    /* A body holding nothing resolves no aim clips, which leaves the layer
       weightless and the locomotion clips alone with the arms - which is what
       empty hands should look like anyway. */
    for (int i = 0; i < CHARACTER_AIM_COUNT; ++i)
        animation->aim_clip[i] = clips && clips->aim[i]
            ? skinned_model_find_clip(model, clips->aim[i]) : -1;
    /* A rig without these bones gets a mask that moves nothing, so the layer
       drops out and the body falls back to the locomotion clips alone. */
    skinned_model_mask_subtree(model, AIM_UPPER_ROOT, &animation->upper);
    animation->upper_root = find_node(&model->skeleton, AIM_UPPER_ROOT);
    skinned_model_mask_subtree(model, AIM_ARM_ROOT_L, &animation->arms);
    PoseMask right;
    if (skinned_model_mask_subtree(model, AIM_ARM_ROOT_R, &right))
        for (int i = 0; i < SKIN_MAX_NODES; ++i)
            animation->arms.node[i] |= right.node[i];
    resolve_hang_bones(animation, model);
    resolve_twist_bones(animation, model);
    animation->role = CHARACTER_CLIP_IDLE;
    animation->leg_rate = 1.0f;
    /* Holding its rifle on the first frame rather than easing into holding it,
       for the same reason the facing starts handed over: nobody spawns mid-
       slide, and a body that spent its first tenth of a second with the layer
       off would be seen doing it. */
    animation->aim_weight = 1.0f;
    /* Already handed over, so the first frame draws the body where it is
       looking rather than swinging it round from wherever it happened to have
       been walking. Nobody spawns mid-slide. */
    animation->facing_weight = 1.0f;
}

/* Degrees wrapped to (-180, 180], which is the form every angular difference
   below wants: signed, and the short way round. */
static float wrap_degrees(float degrees) {
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees <= -180.0f) degrees += 360.0f;
    return degrees;
}

/* Which way the body stands, where its legs point inside that, which way its
   walk runs, and where its head is looking. One decision in four numbers, so
   one function: every one of them is measured against the one before it.

   The body goes where the player is pointed. Not where they are shooting, not
   where the sight is - where they are pointed, which is the thing anyone else
   watching them reads. What is left to decide is only how much of that a clip
   that owns the body takes back, and what the legs and the head do underneath
   whatever comes out. */
static void update_facing(CharacterAnimation* animation,
                          const CharacterVisualState* state, float delta_time) {
    /* Heading is atan2(vx, vz), so a body at zero heading faces +Z, while a
       look yaw of zero looks down -Z - the controller's movement decode is
       where that half turn comes from. This is the look direction expressed in
       the same degrees the body's own heading is in. */
    float aim_yaw = state->look_yaw_degrees + 180.0f;
    float move_yaw = state->heading_degrees;

    /* All of it, all the time, except where a clip owns the body - `aim_weight`
       is that exception, and a wall run is one more: the legs are on the wall,
       that read is the clip and the transform's fourteen degrees of tilt
       together, and a body turned off the wall to face a crosshair is a body
       running on air. The aim layer still pitches those and the head still
       looks; it is only the heading they keep.

       A rig with no leg chain to twist keeps its travel too, since the aim would
       otherwise arrive without anything to put the feet back. See `twist_legs`.

       Which clips those are is stated here rather than read off `aim_weight`,
       which is what it used to be. The two questions had the same answer while
       the sight could not take the upper body off a climb, and now that it can
       they do not: a body climbing a wall with its rifle up holds the wall with
       its legs and aims with everything above them. Turning the whole body to
       the crosshair would take its feet off the wall it is standing on, and the
       tilt this transform carries for a wall is measured against the heading it
       would have turned away from. */
    int clip_owns_heading = state->wall_running || state->wall_climbing ||
                            state->mantling || state->ledge_hanging;
    float handover = animation->twist_legs && !clip_owns_heading
        ? animation->aim_weight : 0.0f;
    if (delta_time > 0.0f)
        animation->facing_weight = approachf(animation->facing_weight, handover,
                                            delta_time / FACING_HANDOVER_TIME);

    float was = animation->body_yaw;
    animation->body_yaw = move_yaw + wrap_degrees(aim_yaw - move_yaw) *
                                     animation->facing_weight;
    float turned = wrap_degrees(animation->body_yaw - was);

    if (delta_time <= 0.0f) return;

    /* The head, measured off the chest the line above just placed - so wherever
       the body has already gone to the aim it is zero and costs nothing, and
       through a handover it covers exactly the part of the turn the body has not
       made. The two layers need no arbitration between them. */
    float look = wrap_degrees(aim_yaw - animation->body_yaw);
    /* Dead astern, which way round is the short way is a coin toss, and a
       heading that wobbles either side of it would swing the head through the
       front every time it wobbled. So it keeps the side it is already on. */
    if (fabsf(look) > 170.0f && animation->look_yaw < 0.0f) look = -fabsf(look);
    look = clampf(look, -FACING_LOOK_MAX, FACING_LOOK_MAX) * animation->aim_weight;
    animation->look_yaw = approachf(animation->look_yaw, look,
                                    FACING_LOOK_RATE * delta_time);

    float target, rate, rate_target = 1.0f;
    if (state->moving) {
        target = wrap_degrees(move_yaw - animation->body_yaw);
        /* Further round than a strafe, and the legs come back to the near side
           and walk backwards instead. A backpedal is the case this is for: the
           legs end up square under a chest facing the crosshair, and it is the
           cycle running in reverse that carries the body away from it. */
        if (target > FACING_REVERSE_ANGLE) { target -= 180.0f; rate_target = -1.0f; }
        else if (target < -FACING_REVERSE_ANGLE) { target += 180.0f; rate_target = -1.0f; }
        target = clampf(target, -FACING_LEG_YAW_MAX, FACING_LEG_YAW_MAX);
        rate = FACING_LEG_TURN_RATE;
        animation->stepping = 0;
    } else {
        /* Planted: the feet hold the direction they were left in, so every
           degree the chest turns to follow the camera is a degree of twist
           against them. The cap is what makes them give in once the camera has
           outrun the waist, and the settle below unwinds what is left when the
           camera stops.

           The cap for the frame is the standing one or whatever a strafe left
           behind, whichever is more: a player who stops mid-strafe is already
           wound further than a standing body may wind itself, and clamping to
           the cap outright would snap their legs a quarter turn on the frame
           they let go of the key. */
        float limit = fmaxf(FACING_STAND_TWIST_MAX, fabsf(animation->leg_yaw));
        animation->leg_yaw = clampf(animation->leg_yaw - turned, -limit, limit);
        /* And once they have given in they take a whole step rather than
           trailing the camera at arm's length: held until they are square, so a
           player who keeps turning gets steps rather than a permanent lag. */
        if (fabsf(animation->leg_yaw) >= FACING_STAND_TWIST_MAX) animation->stepping = 1;
        else if (fabsf(animation->leg_yaw) < FACING_STEP_SETTLE) animation->stepping = 0;
        target = 0.0f;
        /* A twist too small ever to have tripped a step unwinds lazily instead,
           which is the difference between a body relaxing and a body stepping. */
        rate = animation->stepping ? FACING_STEP_RATE : FACING_LEG_SETTLE_RATE;
    }

    animation->leg_yaw = approachf(animation->leg_yaw, target, rate * delta_time);
    animation->leg_rate = approachf(animation->leg_rate, rate_target,
                                    delta_time * 2.0f / FACING_REVERSE_TIME);
}

float character_animation_body_yaw(const CharacterAnimation* animation) {
    return animation ? animation->body_yaw : 0.0f;
}

static float horizontal_speed(const CharacterVisualState* state) {
    return sqrtf(state->velocity_x * state->velocity_x +
                 state->velocity_z * state->velocity_z);
}

/* The clip the body's current state asks for, ignoring any one-shot in
   flight. Ordered most specific first: a slide is a slide whether or not the
   body is also crouching and moving, which it is. */
static int state_role(const CharacterAnimation* animation,
                      const CharacterVisualState* state) {
    /* Before the climb, because a hang is the pose you hold instead of
       climbing: the two share a ledge but never run at once. */
    if (state->ledge_hanging) return CHARACTER_CLIP_HANG;
    if (state->mantling || state->wall_climbing) return CHARACTER_CLIP_CLIMB;
    if (state->sliding) return CHARACTER_CLIP_SLIDE_LOOP;
    /* A wall run is a sprint held sideways. The tilt that sells it is on the
       transform in character_renderer, not in a clip of its own. */
    if (state->wall_running) return CHARACTER_CLIP_SPRINT;
    if (state->grappling || !state->grounded)
        return CHARACTER_CLIP_JUMP_LOOP;

    if (state->crouching)
        return state->moving ? CHARACTER_CLIP_CROUCH_WALK : CHARACTER_CLIP_CROUCH_IDLE;
    if (!state->moving) return CHARACTER_CLIP_IDLE;
    if (state->sprinting) return CHARACTER_CLIP_SPRINT;
    /* The rig is scaled up to gameplay's height, so walking pace in metres is
       well under walking pace in world units: MOVE_WALK_SPEED is a jog on a
       body this size, and the slow clip is for the ends of an acceleration.

       Two thresholds rather than one, and which applies depends on which clip
       is already playing. A single boundary is a coin flipped every frame for
       any player whose speed sits on it - and a player pushing into a wall,
       easing off a key, or strafing at an angle sits on one for as long as they
       like. Each flip restarts the stride from its first frame and starts a
       cross-fade, so the feet stutter and the body never finishes settling. The
       band is wide enough that ordinary acceleration crosses it once. */
    float boundary = animation->role == CHARACTER_CLIP_JOG ? 0.48f : 0.62f;
    return horizontal_speed(state) < MOVE_WALK_SPEED * boundary
        ? CHARACTER_CLIP_WALK : CHARACTER_CLIP_JOG;
}

/* Locomotion clips were authored at one pace, and the player arrives at
   whichever the tuning allows. Scaling playback to the speed the body is
   actually travelling is what keeps the feet from skating; the clamp keeps a
   near-stationary shuffle from crawling to a stop. */
static float playback_rate(const CharacterAnimation* animation,
                           const CharacterVisualState* state) {
    float reference;
    switch (animation->role) {
        case CHARACTER_CLIP_WALK:         reference = MOVE_WALK_SPEED * 0.55f; break;
        case CHARACTER_CLIP_JOG:          reference = MOVE_WALK_SPEED; break;
        case CHARACTER_CLIP_SPRINT:       reference = MOVE_SPRINT_SPEED; break;
        case CHARACTER_CLIP_CROUCH_WALK:  reference = MOVE_CROUCH_SPEED; break;
        default: return 1.0f;
    }
    if (reference <= 0.01f) return 1.0f;
    /* Signed, because a body walking away from where it is aiming plays its
       walk backwards rather than moonwalking forwards. Only the locomotion
       roles reach this line: a landing or a slide is a one-shot with a
       direction of its own, and nothing about aiming should run it in
       reverse. */
    return clampf(horizontal_speed(state) / reference, 0.6f, 1.7f) * animation->leg_rate;
}

static void switch_role(CharacterAnimation* animation, const SkinnedModel* model,
                        int role, float hold) {
    /* A one-shot is playing something no role names, so a body coming out of a
       swing back into the role it was already in is a real change and has to
       fade rather than early-return into a frozen last frame. */
    if (role == animation->role && animation->oneshot_clip < 0) return;
    /* Snapshot the clip the body is playing, cross-fade and all, so the new one
       fades in from where the old one had got to rather than from its own first
       frame - and a switch that lands mid-fade carries the fade it interrupted
       instead of jumping back to the clip underneath it.

       The clip and nothing else. What the layers on top of it are doing is not
       snapshotted, because they are not fading: the aim layer and the facing
       twist are applied live, once, to whatever this blend produces. Taking the
       finished pose here instead - which is what this used to do - fed a body
       that already carried the twist back into the blend and then twisted it
       again, and the second helping decayed over the fade rather than arriving
       and leaving with it. On a body wound up to the leg cap, which is any
       player moving across their own crosshair, that put the chest and the gun
       fifty degrees off the aim on the frame a clip changed and swung them back
       over the following tenth of a second. Every clip change. */
    compose_base(animation, model, &animation->fade_from);
    animation->fading = 1;
    animation->fade = FADE_SECONDS;
    animation->role = role;
    animation->oneshot_clip = -1;
    animation->time = 0.0f;
    animation->hold = hold;
}

/* Plays a clip that is not one of the locomotion roles, for `hold` seconds,
   through exactly the path a jump takes: the same snapshot, the same
   cross-fade, the same hold that expires back into whatever the state asks
   for. There is deliberately no second playhead - a swing is a clip the body
   is playing, and one clock is what keeps a mid-swing landing from running two
   animations at once and blending neither of them out. */
void character_animation_play_oneshot(CharacterAnimation* animation,
                                      const SkinnedModel* model,
                                      int clip_index, float hold_seconds) {
    if (!animation || !model || clip_index < 0) return;
    if (animation->oneshot_clip == clip_index) return;
    compose_base(animation, model, &animation->fade_from);
    animation->fading = 1;
    animation->fade = FADE_SECONDS;
    animation->oneshot_clip = clip_index;
    animation->time = 0.0f;
    animation->hold = hold_seconds;
}

void character_animation_update(CharacterAnimation* animation, const SkinnedModel* model,
                                const CharacterVisualState* state, float delta_time) {
    if (!animation || !model || !state) return;

    /* Everything the layers read, set before anything poses the body with them.
       They are not snapshotted anywhere - a cross-fade carries clip against
       clip and these are applied live on top of whatever it produces - so all
       that is asked of them is that they are this frame's by the time the frame
       is drawn. */
    animation->aim_pitch = state->look_pitch_degrees;
    animation->aim_ads = clampf(state->ads, 0.0f, 1.0f);
    /* Which of the two owns the body above the waist.

       The default is the aim layer, always: a body on its feet holds its rifle
       toward what it is looking at, whatever its legs are doing underneath -
       walking, jogging, sprinting, strafing across its own crosshair,
       crouching, jumping, landing, leaning. Nothing in that list is allowed to
       take the gun off the aim, and none of them ever needed to.

       Three clips take it back, and only while the sight is down. A slide leans
       the pelvis seventy degrees off vertical; a mantle and a wall climb have
       both hands out on the geometry. Those are poses worth having, and a
       player carrying their rifle at the ready gets them. Raise the sight and
       they hand the upper body straight back - which is the priority the player
       asked for, and it is affordable because stabilise_chest below holds the
       barrel on the crosshair however far off vertical the hips underneath it
       are. Without that, an aim laid over a slide pointed the rifle at the sky.

       Hanging off a ledge is the exception that is not negotiable: it is
       holding the body's whole weight on its arms. The weapon agrees and
       refuses the sight outright for it, so this can only ever disagree with a
       body whose state arrived over a wire - which is exactly when a rule
       stated once, here, is worth more than one inferred from a number.

       Eased, over the same tenth of a second a clip change takes. It used to be
       switched outright and left to the clip cross-fade to cover, which worked
       only because the fade snapshot carried the layer with it - and that same
       snapshot is what put the twist into the body twice. With the fade
       carrying clip against clip, this is the thing that has to be continuous,
       and now it is. */
    int hands_full = state->ledge_hanging;
    int clip_owns_upper = state->sliding || state->mantling || state->wall_climbing;
    float aim_target = hands_full ||
                       (clip_owns_upper && animation->aim_ads <= 0.0f) ? 0.0f : 1.0f;
    animation->aim_weight = delta_time > 0.0f
        ? approachf(animation->aim_weight, aim_target, delta_time / FADE_SECONDS)
        : animation->aim_weight;
    /* Refused by the same clips, and for the same reason: a slide is authored
       down to the spine, and bending an authored spine sideways is how a pose
       ends up in a shape nobody drew. The controller already smoothed the
       number, so it is taken as it arrives. */
    animation->lean = clampf(state->lean, -1.0f, 1.0f) * animation->aim_weight;

    /* After the two above, which is not arbitrary: the facing hands the body's
       heading over to the aim by exactly as much as the aim layer has of it,
       and reading a weight this frame's update has not yet written would leave
       the two a frame out of step through every handover. */
    update_facing(animation, state, delta_time);

    /* Events first: these are the moments a one-shot exists to cover, and they
       outrank a one-shot already running. Landing out of a jump has to be able
       to interrupt the jump.

       They arrive as bits rather than being inferred here from this frame's
       flags against last frame's, so a body whose state comes in over a wire
       still lands: an edge that fell between two snapshots would be a landing
       animation nobody ever sees. Which event wins, and whether the current
       state agrees it is worth playing, is still decided here - the producer
       reports transitions, this reads them as animation. */
    unsigned events = state->events;
    if (events & CHARACTER_EVENT_MANTLE_BEGAN)
        switch_role(animation, model, CHARACTER_CLIP_CLIMB, 0.0f);
    else if (events & CHARACTER_EVENT_SLIDE_BEGAN)
        switch_role(animation, model, CHARACTER_CLIP_SLIDE_START,
                    ROLES[CHARACTER_CLIP_SLIDE_START].hold);
    else if ((events & CHARACTER_EVENT_SLIDE_ENDED) && state->grounded)
        switch_role(animation, model, CHARACTER_CLIP_SLIDE_EXIT,
                    ROLES[CHARACTER_CLIP_SLIDE_EXIT].hold);
    else if ((events & CHARACTER_EVENT_LEFT_GROUND) && !state->sliding)
        switch_role(animation, model, CHARACTER_CLIP_JUMP_START,
                    ROLES[CHARACTER_CLIP_JUMP_START].hold);
    else if (events & CHARACTER_EVENT_LANDED)
        switch_role(animation, model, CHARACTER_CLIP_JUMP_LAND,
                    ROLES[CHARACTER_CLIP_JUMP_LAND].hold);
    else if (animation->hold <= 0.0f)
        switch_role(animation, model, state_role(animation, state), 0.0f);

    /* A landing the body has already run out of is over, whatever its hold
       says. Standing up from it while sprinting is the pose that looks broken,
       not the missing quarter second of settling. */
    if (animation->role == CHARACTER_CLIP_JUMP_LAND && state->has_movement_input)
        animation->hold = 0.0f;

    if (delta_time <= 0.0f) return;
    animation->time += delta_time * playback_rate(animation, state);
    if (animation->hold > 0.0f) animation->hold -= delta_time;
    if (animation->fading) {
        animation->fade -= delta_time;
        if (animation->fade <= 0.0f) animation->fading = 0;
    }
}

/* Pins the chest to the orientation the aim pose was authored with, however the
   clip underneath is moving the hips.

   The aim layer replaces the chest, the arms and the head, but it replaces them
   RELATIVE TO THE PELVIS, and a pelvis is never still. A stride swings and tips
   it, a crouch tucks it under, a landing folds it - and every degree of that
   goes straight up into a chest that was supposed to be holding a sight
   picture. Measured on this rig, at the sight and looking level, it puts the
   rifle fourteen degrees off the crosshair at a jog and twenty-two down at a
   crouch walk. That is not the gun swaying, which is a thing a gun should do:
   it is the gun pointing somewhere the player is not.

   So the difference between the chain of rotations above the chest in the pose
   on screen and the same chain in the authored aim pose is composed onto the
   chest, which puts its orientation exactly where the aim pose wanted it. The
   authored pose and not the bind pose, because the aim poses have hips of their
   own and reproducing what was authored is the whole point.

   Orientation only. The chest still rides the pelvis up and down and side to
   side, so the body still breathes and bobs and the weight still shifts - it is
   the barrel's DIRECTION that is nailed down, which is the thing the player is
   asking for when they raise a sight.

   Scaled by how far the sight is up, so a rifle carried at the ready keeps
   every bit of the stride's own motion and only an aimed one is held still. */
static void stabilise_chest(const CharacterAnimation* animation,
                            const SkinnedModel* model, const Pose* layer,
                            Pose* out) {
    float amount = animation->aim_ads * animation->aim_weight;
    if (animation->upper_root < 0 || amount <= 0.001f) return;

    float now[4], authored[4], inverse[4], correction[4];
    chain_rotation(&model->skeleton, out, animation->upper_root, now);
    chain_rotation(&model->skeleton, layer, animation->upper_root, authored);
    quat_inverse(now, inverse);
    quat_multiply(inverse, authored, correction);
    quat_fraction(correction, amount, correction);
    /* In the parent's frame, so before the bone's own rotation - the same side
       the facing twist composes on, and for the same reason. */
    quat_multiply(correction, out->local[animation->upper_root].rotation,
                  out->local[animation->upper_root].rotation);
}

/* The upper body, from where the camera is looking and whether the sight is
   up. Three steps, none of which the locomotion clip below it can see:

     1. pick a point in the aim offset from the camera's pitch,
     2. drop the arms toward the lowered pose by however far the sight is down,
     3. lay the result over the legs, on the masked bones only.

   Step 2 collapses the carry and the aim into one blend rather than two poses
   and a cross-fade between them: at full sight it weighs nothing and the pose
   is the aim offset exactly, and off the sight it is the same pose with the
   arms brought down. */
static void apply_aim_layer(const CharacterAnimation* animation,
                            const SkinnedModel* model, Pose* out) {
    if (animation->aim_weight <= 0.0f) return;
    const int* clip = animation->aim_clip;
    if (clip[CHARACTER_AIM_DOWN] < 0 || clip[CHARACTER_AIM_NEUTRAL] < 0 ||
        clip[CHARACTER_AIM_UP] < 0)
        return;

    float gain = lerpf(AIM_PITCH_GAIN, AIM_PITCH_GAIN_ADS, animation->aim_ads);
    float a = clampf(AIM_LEVEL + animation->aim_pitch * gain, 0.0f, 1.0f);
    /* Below halfway the blend runs Down to Neutral and above it Neutral to Up,
       so each half is a straight blend between two authored poses rather than
       a three-way average that would belong to none of them. */
    int from = a <= 0.5f ? CHARACTER_AIM_DOWN : CHARACTER_AIM_NEUTRAL;
    int to   = a <= 0.5f ? CHARACTER_AIM_NEUTRAL : CHARACTER_AIM_UP;
    float t  = a <= 0.5f ? a * 2.0f : (a - 0.5f) * 2.0f;

    /* Two poses' worth of scratch, twenty kilobytes, on the stack. They were
       file statics, which was one body's worth of workspace shared by every
       body that would ever exist - harmless while the posing is serial, and
       exactly the thing that stops it from not being. The stack is where a
       temporary belongs, and this file already puts two matrix arrays of the
       same size there in the hang and the twist. */
    Pose layer, scratch;

    /* The poses are static - six keyframes of the same thing - so the time
       these are sampled at does not matter. */
    skinned_model_sample(model, clip[from], 0.0f, &layer);
    skinned_model_sample(model, clip[to], 0.0f, &scratch);
    skinned_model_blend(model, &layer, &scratch, t, &layer);

    float lower = AIM_CARRY_LOWER * (1.0f - animation->aim_ads);
    if (lower > 0.0f) {
        skinned_model_sample(model, clip[CHARACTER_AIM_DOWN], 0.0f, &scratch);
        skinned_model_blend_masked(model, &layer, &scratch, lower,
                                   &animation->arms, &layer);
    }

    skinned_model_blend_masked(model, out, &layer, animation->aim_weight,
                               &animation->upper, out);
    stabilise_chest(animation, model, &layer, out);
}

void character_animation_set_grip(CharacterAnimation* animation,
                                  float x, float y, float z, int valid) {
    if (!animation) return;
    animation->hang_grip[0] = x;
    animation->hang_grip[1] = y;
    animation->hang_grip[2] = z;
    animation->hang_grip_valid = valid;
}

/* Two bones reaching a point: the law of cosines gives the angle the upper arm
   makes with the straight line to the target, and the pole decides which way
   round that cone the elbow sits. A target further away than the arm is long
   is pulled in to arm's length rather than refused, so an out-of-reach ledge
   straightens the arm instead of tearing it off.

   Both directions come out in the model's space, which is what the pointing
   below consumes. */
static void reach_two_bone(const float shoulder[3], const float target[3],
                           float upper_length, float lower_length,
                           float upper_dir[3], float lower_dir[3]) {
    float to[3];
    for (int i = 0; i < 3; ++i) to[i] = target[i] - shoulder[i];
    float distance = sqrtf(to[0]*to[0] + to[1]*to[1] + to[2]*to[2]);
    if (distance < 1e-5f) { distance = 1e-5f; to[1] = distance; }

    float axis[3];
    for (int i = 0; i < 3; ++i) axis[i] = to[i] / distance;

    float shortest = fabsf(upper_length - lower_length) + 1e-4f;
    float longest = upper_length + lower_length - 1e-4f;
    if (distance < shortest) distance = shortest;
    if (distance > longest) distance = longest;

    float cosine = (upper_length * upper_length + distance * distance -
                    lower_length * lower_length) / (2.0f * upper_length * distance);
    cosine = clampf(cosine, -1.0f, 1.0f);
    float angle = acosf(cosine);

    /* The part of the pole square to the arm's line: the plane the bend lies
       in. A pole parallel to that line says nothing, so fall back to any
       perpendicular rather than dividing by nothing. */
    float dot = HANG_ELBOW_POLE[0]*axis[0] + HANG_ELBOW_POLE[1]*axis[1] +
                HANG_ELBOW_POLE[2]*axis[2];
    float bend[3];
    for (int i = 0; i < 3; ++i) bend[i] = HANG_ELBOW_POLE[i] - axis[i] * dot;
    float length = sqrtf(bend[0]*bend[0] + bend[1]*bend[1] + bend[2]*bend[2]);
    if (length < 1e-5f) {
        bend[0] = axis[1]; bend[1] = -axis[0]; bend[2] = 0.0f;
        length = sqrtf(bend[0]*bend[0] + bend[1]*bend[1]);
        if (length < 1e-5f) { bend[0] = 1.0f; bend[1] = bend[2] = 0.0f; length = 1.0f; }
    }
    for (int i = 0; i < 3; ++i) bend[i] /= length;

    float elbow[3];
    for (int i = 0; i < 3; ++i) {
        upper_dir[i] = axis[i] * cosf(angle) + bend[i] * sinf(angle);
        elbow[i] = shoulder[i] + upper_dir[i] * upper_length;
        lower_dir[i] = target[i] - elbow[i];
    }
    length = sqrtf(lower_dir[0]*lower_dir[0] + lower_dir[1]*lower_dir[1] +
                   lower_dir[2]*lower_dir[2]);
    if (length < 1e-5f) { lower_dir[0] = upper_dir[0]; lower_dir[1] = upper_dir[1];
                          lower_dir[2] = upper_dir[2]; }
    else for (int i = 0; i < 3; ++i) lower_dir[i] /= length;
}

/* Turns the direction table into rotations.

   One pass down the hierarchy, parents before children, because pointing a
   bone needs to know where its parent ended up: a direction here is in the
   model's space, and what a bone stores is relative to its parent. So each
   bone is pointed, and then its own global is computed for whichever of its
   children the table also poses. */
static void apply_hang_pose(const CharacterAnimation* animation,
                            const SkinnedModel* model, Pose* out) {
    const Skeleton* skeleton = &model->skeleton;
    if (animation->hang_count <= 0) return;

    int reaching = animation->hang_grip_valid && animation->hang_upper_l >= 0 &&
                   animation->hang_lower_l >= 0 && animation->hang_upper_length > 1e-5f &&
                   animation->hang_lower_length > 1e-5f;
    float upper_dir[3] = {0, 1, 0}, lower_dir[3] = {0, 1, 0};

    Mat4 global[SKIN_MAX_NODES];
    for (int i = 0; i < skeleton->node_count; ++i) {
        int node = skeleton->order[i];

        for (int e = 0; e < CHARACTER_HANG_BONES; ++e) {
            if (animation->hang_node[e] != node) continue;
            int parent = skeleton->parent[node];
            float target[3] = {HANG_POSE[e].x, HANG_POSE[e].y, HANG_POSE[e].z};
            /* The reaching arm overrides its authored direction. Solved once,
               when the upper arm comes round, because the forearm's answer
               depends on where the elbow landed. */
            if (reaching && node == animation->hang_upper_l) {
                float shoulder[3] = {global[parent].m[12], global[parent].m[13],
                                     global[parent].m[14]};
                const float* t = out->local[node].translation;
                float offset[3];
                mat4_transform_direction(&global[parent], t, offset);
                for (int c = 0; c < 3; ++c) shoulder[c] += offset[c];
                reach_two_bone(shoulder, animation->hang_grip,
                               animation->hang_upper_length,
                               animation->hang_lower_length, upper_dir, lower_dir);
                for (int c = 0; c < 3; ++c) target[c] = upper_dir[c];
            } else if (reaching && node == animation->hang_lower_l) {
                for (int c = 0; c < 3; ++c) target[c] = lower_dir[c];
            }
            float length = sqrtf(target[0]*target[0] + target[1]*target[1] +
                                 target[2]*target[2]);
            if (length < 1e-6f) break;
            for (int c = 0; c < 3; ++c) target[c] /= length;

            /* Into the parent's frame. The columns are normalized first: the
               rig is scaled to gameplay height by the caller, and a scaled
               basis would tilt the direction rather than just rotating it. */
            float local_target[3] = {target[0], target[1], target[2]};
            if (parent >= 0) {
                const Mat4* p = &global[parent];
                float basis[9];
                for (int c = 0; c < 3; ++c) {
                    float s = sqrtf(p->m[c*4+0]*p->m[c*4+0] + p->m[c*4+1]*p->m[c*4+1] +
                                    p->m[c*4+2]*p->m[c*4+2]);
                    if (s < 1e-8f) s = 1.0f;
                    for (int r = 0; r < 3; ++r) basis[c*3+r] = p->m[c*4+r] / s;
                }
                /* Transpose of a rotation is its inverse. */
                for (int c = 0; c < 3; ++c)
                    local_target[c] = basis[c*3+0]*target[0] + basis[c*3+1]*target[1] +
                                      basis[c*3+2]*target[2];
            }
            quat_between(animation->hang_axis[e], local_target, out->local[node].rotation);
            break;
        }

        const Transform* local = &out->local[node];
        Mat4 matrix = mat4_from_trs(local->translation, local->rotation, local->scale);
        int parent = skeleton->parent[node];
        global[node] = parent < 0 ? matrix : mat4_multiply(global[parent], matrix);
    }
}

/* One model-space axis expressed in a bone's parent frame, which is what a
   stored local rotation is relative to.

   The axis has to be carried in rather than used directly: a bone's own axes
   are wherever the rig author left them - this skeleton's pelvis sits a hundred
   degrees off about x, because the export stood a z-up rig up - so a rotation
   about the bone's x is not a rotation about the model's. The parent's basis is
   normalized first, since the rig is drawn through a scale and a scaled basis
   would tilt the axis rather than just reorienting it, and transposed, which
   for a rotation is its inverse. */
static void axis_in_parent(const Mat4* parent, const float axis[3], float out[3]) {
    if (!parent) { out[0] = axis[0]; out[1] = axis[1]; out[2] = axis[2]; return; }
    for (int c = 0; c < 3; ++c) {
        float length = sqrtf(parent->m[c*4+0]*parent->m[c*4+0] +
                             parent->m[c*4+1]*parent->m[c*4+1] +
                             parent->m[c*4+2]*parent->m[c*4+2]);
        if (length < 1e-8f) length = 1.0f;
        out[c] = (parent->m[c*4+0]*axis[0] + parent->m[c*4+1]*axis[1] +
                  parent->m[c*4+2]*axis[2]) / length;
    }
}

/* One channel of the facing: every bone in the table rotated about one axis of
   the model's own space, through its own joint, by its own share.

   The axis is the model's, not the bone's, and it is carried into each bone's
   parent frame - see axis_in_parent. That needs the parent's global, so this is
   one pass down the hierarchy, parents before children, and it stops as soon as
   the last entry is placed rather than walking bones no entry names.

   Because every rotation in a pass is about a common axis in the model's space,
   the pass telescopes: what comes out at the top of the chain is one rotation
   about that axis by the sum of the shares below it. That is the property the
   whole facing split rests on. Shares that sum to zero above the pelvis cancel
   exactly, not approximately, so the chest and the rifle stay on the crosshair
   to the degree whatever the legs are doing. */
static void apply_axis_channel(const CharacterAnimation* animation,
                               const SkinnedModel* model, const float axis_model[3],
                               const float degrees[CHARACTER_TWIST_BONES], Pose* out) {
    const Skeleton* skeleton = &model->skeleton;
    Mat4 global[SKIN_MAX_NODES];
    int remaining = animation->twist_count;
    for (int i = 0; i < skeleton->node_count && remaining > 0; ++i) {
        int node = skeleton->order[i];
        int parent = skeleton->parent[node];

        for (int e = 0; e < CHARACTER_TWIST_BONES; ++e) {
            if (animation->twist_node[e] != node) continue;
            float axis[3];
            axis_in_parent(parent >= 0 ? &global[parent] : NULL, axis_model, axis);
            float turn[4];
            quat_from_axis_angle(axis, degrees[e] * (float)MATH_DEG_TO_RAD, turn);
            /* The turn happens after the bone's own rotation, in the parent's
               frame, which is this product and not the other one. */
            quat_multiply(turn, out->local[node].rotation, out->local[node].rotation);
            remaining--;
            break;
        }

        const Transform* local = &out->local[node];
        Mat4 matrix = mat4_from_trs(local->translation, local->rotation, local->scale);
        global[node] = parent < 0 ? matrix : mat4_multiply(global[parent], matrix);
    }
}

/* Turns the legs without turning anything above the waist, the head without
   turning anything below the neck, and leans the whole of it sideways without
   moving the feet.

   The first two are one channel about the model's vertical: the pelvis takes the
   legs round and the three spine joints hand the same turn back, which leaves
   the chest, the arms, the head and the rifle exactly where the aim layer put
   them; on the look share the chest, the neck and the head each take a piece
   that carries the head to the camera.

   The lean is a second channel, about the model's forward. Its shares do not
   cancel - they add all the way up, so the head ends up displaced by the sum of
   them, and the rifle in the body's hands goes with the chest that carries it.

   Two passes, and that is the whole reason this is not one loop. Rotations about
   two different axes do not commute, so interleaving them bone by bone gives a
   chain that telescopes to neither sum: the leg shares stop cancelling, and what
   should have been a peek that left the barrel on the crosshair took it eleven
   degrees off - but only while the legs were also wound round, which is to say
   only while moving, which is exactly when it is hardest to see and easiest to
   blame on something else. Run as two passes, each is a clean rotation about its
   own axis: the turn cancels above the waist exactly as it always did, and the
   lean arrives on top of it as a single roll of everything above the hips. */
static void apply_body_twist(const CharacterAnimation* animation,
                             const SkinnedModel* model, Pose* out) {
    if (animation->twist_count <= 0) return;

    float turn[CHARACTER_TWIST_BONES], lean[CHARACTER_TWIST_BONES];
    int turning = 0, leaning = 0;
    /* The rig faces its own +z, so that is the axis a lean rolls about. A
       positive roll about it takes the head toward -x, and +x is the model's own
       left - which is the side a positive lean peeks to, hence the sign here. */
    float lean_degrees = -animation->lean * CHARACTER_LEAN_ROLL;
    for (int e = 0; e < CHARACTER_TWIST_BONES; ++e) {
        turn[e] = animation->leg_yaw * TWIST_BONES[e].legs +
                  animation->look_yaw * TWIST_BONES[e].look;
        lean[e] = lean_degrees * TWIST_BONES[e].lean;
        if (fabsf(turn[e]) >= 0.02f) turning = 1;
        if (fabsf(lean[e]) >= 0.02f) leaning = 1;
    }

    static const float MODEL_UP[3] = {0.0f, 1.0f, 0.0f};
    static const float MODEL_FORWARD[3] = {0.0f, 0.0f, 1.0f};
    if (turning) apply_axis_channel(animation, model, MODEL_UP, turn, out);
    if (leaning) apply_axis_channel(animation, model, MODEL_FORWARD, lean, out);
}

/* The clip and the cross-fade out of the last one, and nothing else. This is
   what the aim layer and the facing twist are then applied to, and it is what a
   role switch snapshots - so a fade only ever carries clip against clip. */
static void compose_base(const CharacterAnimation* animation,
                         const SkinnedModel* model, Pose* out) {
    int clip = animation->oneshot_clip >= 0 ? animation->oneshot_clip
                                            : animation->clip[animation->role];

    /* Looping is the sampler's caller's job, which is here: a clip that runs
       out holds its last frame, and a role that loops wraps its time instead.
       A one-shot never loops - it holds its last frame and then expires, which
       is the whole difference between a swing and a walk cycle. */
    float time = animation->time;
    if (clip >= 0 && animation->oneshot_clip < 0 &&
        ROLES[animation->role].hold == 0.0f) {
        float duration = model->clips[clip].duration;
        /* Reversed playback walks the clock backwards, and fmodf keeps the
           sign of its left operand, so a negative time has to be brought back
           into the clip's own range rather than left to be clamped to the
           first keyframe - which would freeze a backpedal solid. */
        if (duration > 0.0f) {
            time = fmodf(time, duration);
            if (time < 0.0f) time += duration;
        }
    }
    skinned_model_sample(model, clip, time, out);
    /* Before the cross-fade, so entering and leaving a hang eases the way
       every other clip change does. */
    if (animation->role == CHARACTER_CLIP_HANG) apply_hang_pose(animation, model, out);

    if (animation->fading && FADE_SECONDS > 0.0f) {
        float t = 1.0f - animation->fade / FADE_SECONDS;
        skinned_model_blend(model, &animation->fade_from, out,
                            smoothstepf(clampf(t, 0, 1)), out);
    }
}

void character_animation_pose(const CharacterAnimation* animation,
                              const SkinnedModel* model, Pose* out) {
    if (!animation || !model || !out) return;
    /* Clip, then layers, each applied exactly once and in this order. The clip
       is the only thing that cross-fades; everything above it is a live
       function of where the body is looking and how far round its legs are, and
       neither of those changes because a clip did. */
    compose_base(animation, model, out);
    apply_aim_layer(animation, model, out);
    /* Last, and after the aim layer on purpose: the layer overwrites the spine
       outright, so a twist applied before it would be thrown away. */
    apply_body_twist(animation, model, out);
}
