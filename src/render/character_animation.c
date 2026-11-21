#include "render/character_animation.h"
#include "render/character_rig_internal.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>
#include <string.h>

/* Role state machine: which clip plays, cross-fades between clips, one-shot
   holds, and the order layers are composed in. */

#define FADE_SECONDS 0.12f

/* Role -> clip. `hold` > 0 makes a one-shot that expires after that many
   seconds (often shorter than the clip, so players aren't locked into a long
   settle). `clamp` plays once and holds the last frame. `pose` layers an
   authored pose over the clip. */
static const struct {
    const char* name;
    float hold;
    int clamp;
    const AuthoredPose* pose;
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
    /* Base for the authored hang: supplies curled fingers and raised shoulders. */
    [CHARACTER_CLIP_HANG]         = { "Pistol_Aim_Up",    0.0f, 0, &CHARACTER_HANG_POSE },
    /* Root pinned in place; clamped so the final frame is the corpse. */
    [CHARACTER_CLIP_DEATH]        = { "Death01",           0.0f, 1, NULL },
    /* Held for the clip's full length. */
    [CHARACTER_CLIP_GET_UP]       = { "LayToIdle",         1.53f, 0, NULL },
};

static float horizontal_speed(const CharacterVisualState* state) {
    return sqrtf(state->velocity_x * state->velocity_x +
                 state->velocity_z * state->velocity_z);
}

/* The role the current state calls for, ignoring one-shots. Most specific
   state first. */
static int state_role(const CharacterAnimation* animation,
                      const CharacterVisualState* state) {
    /* Dead overrides everything; other flags are stale from the moment of death. */
    if (state->dead) return CHARACTER_CLIP_DEATH;
    if (state->ledge_hanging) return CHARACTER_CLIP_HANG;
    if (state->mantling || state->wall_climbing) return CHARACTER_CLIP_CLIMB;
    if (state->sliding) return CHARACTER_CLIP_SLIDE_LOOP;
    /* Wall-run tilt is applied on the transform in character_renderer. */
    if (state->wall_running) return CHARACTER_CLIP_SPRINT;
    if (state->grappling || !state->grounded)
        return CHARACTER_CLIP_JUMP_LOOP;

    if (state->crouching)
        return state->moving ? CHARACTER_CLIP_CROUCH_WALK : CHARACTER_CLIP_CROUCH_IDLE;
    if (!state->moving) return CHARACTER_CLIP_IDLE;
    if (state->sprinting) return CHARACTER_CLIP_SPRINT;
    /* Hysteresis between walk and jog so speeds near the boundary don't
       restart the stride every frame. */
    float boundary = animation->role == CHARACTER_CLIP_JOG ? 0.48f : 0.62f;
    return horizontal_speed(state) < MOVE_WALK_SPEED * boundary
        ? CHARACTER_CLIP_WALK : CHARACTER_CLIP_JOG;
}

/* Scales locomotion playback to actual speed to prevent foot skating. Signed:
   a reversed leg_rate plays the cycle backwards for backpedalling. */
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
    return clampf(horizontal_speed(state) / reference, 0.6f, 1.7f) * animation->leg_rate;
}

/* Sampled clip plus cross-fade, without layers. This is what a role switch
   snapshots, so a fade only ever blends clip against clip (layers are applied
   live afterwards and must not be baked into the snapshot). */
static void compose_base(const CharacterAnimation* animation,
                         const SkinnedModel* model, Pose* out) {
    int clip = animation->oneshot_clip >= 0 ? animation->oneshot_clip
                                            : animation->clip[animation->role];

    /* Looping roles wrap; one-shots and clamped roles hold their last frame. */
    float time = animation->time;
    if (clip >= 0 && animation->oneshot_clip < 0 &&
        ROLES[animation->role].hold == 0.0f &&
        !ROLES[animation->role].clamp) {
        float duration = model->clips[clip].duration;
        /* fmodf keeps the sign; wrap negative time for reversed playback. */
        if (duration > 0.0f) {
            time = fmodf(time, duration);
            if (time < 0.0f) time += duration;
        }
    }
    skinned_model_sample(model, clip, time, out);
    /* Before the fade, so entering/leaving a hang eases like any clip change. */
    if (ROLES[animation->role].pose)
        character_authored_apply(animation, model, ROLES[animation->role].pose, out);

    if (animation->fading && FADE_SECONDS > 0.0f) {
        float t = 1.0f - animation->fade / FADE_SECONDS;
        skinned_model_blend(model, &animation->fade_from, out,
                            smoothstepf(clampf(t, 0, 1)), out);
    }
}

static void begin_fade(CharacterAnimation* animation, const SkinnedModel* model) {
    compose_base(animation, model, &animation->fade_from);
    animation->fading = 1;
    animation->fade = FADE_SECONDS;
}

static void switch_role(CharacterAnimation* animation, const SkinnedModel* model,
                        int role, float hold) {
    /* Returning from a one-shot to the same role is still a real change. */
    if (role == animation->role && animation->oneshot_clip < 0) return;
    begin_fade(animation, model);
    animation->role = role;
    animation->oneshot_clip = -1;
    animation->time = 0.0f;
    animation->hold = hold;
}

/* Shares the role playhead and fade, so a one-shot and a role never run on
   separate clocks. */
void character_animation_play_oneshot(CharacterAnimation* animation,
                                      const SkinnedModel* model,
                                      int clip_index, float hold_seconds) {
    if (!animation || !model || clip_index < 0) return;
    if (animation->oneshot_clip == clip_index) return;
    begin_fade(animation, model);
    animation->oneshot_clip = clip_index;
    animation->time = 0.0f;
    animation->hold = hold_seconds;
}

void character_animation_set_clips(CharacterAnimation* animation,
                                   const SkinnedModel* model,
                                   const WeaponClipSet* clips) {
    if (!animation || !model) return;
    /* Empty hands: no aim clips, so the layer stays weightless. */
    for (int i = 0; i < CHARACTER_AIM_COUNT; ++i)
        animation->aim_clip[i] = clips && clips->aim[i]
            ? skinned_model_find_clip(model, clips->aim[i]) : -1;
}

void character_animation_init(CharacterAnimation* animation,
                              const SkinnedModel* model,
                              const WeaponClipSet* clips) {
    memset(animation, 0, sizeof *animation);
    animation->oneshot_clip = -1;
    for (int i = 0; i < CHARACTER_CLIP_COUNT; ++i)
        animation->clip[i] = skinned_model_find_clip(model, ROLES[i].name);
    character_animation_set_clips(animation, model, clips);
    character_layers_init(animation, model);

    character_authored_reset(animation);
    for (int r = 0; r < CHARACTER_CLIP_COUNT; ++r)
        if (ROLES[r].pose)
            character_authored_resolve(animation, model, ROLES[r].pose);

    animation->role = CHARACTER_CLIP_IDLE;
    animation->leg_rate = 1.0f;
    /* Start fully aimed and facing the look direction; nobody spawns mid-slide. */
    animation->aim_weight = 1.0f;
    animation->facing_weight = 1.0f;
}

/* Eases aim_weight toward who owns the upper body, and derives the lean. */
static void update_aim_weight(CharacterAnimation* animation,
                              const CharacterVisualState* state, float delta_time) {
    animation->aim_pitch = state->look_pitch_degrees;
    animation->aim_ads = clampf(state->ads, 0.0f, 1.0f);
    /* The aim layer owns the upper body by default. Slides, mantles and wall
       climbs take it back only while the sight is down. Hanging and death
       always take it back. */
    int hands_full = state->ledge_hanging || state->dead;
    int clip_owns_upper = state->sliding || state->mantling || state->wall_climbing;
    float aim_target = hands_full ||
                       (clip_owns_upper && animation->aim_ads <= 0.0f) ? 0.0f : 1.0f;
    animation->aim_weight = delta_time > 0.0f
        ? approachf(animation->aim_weight, aim_target, delta_time / FADE_SECONDS)
        : animation->aim_weight;
    /* Suppressed by the same clips; already smoothed by the controller. */
    animation->lean = clampf(state->lean, -1.0f, 1.0f) * animation->aim_weight;
}

/* Converts the world-space flinch direction into the body's drawn frame.
   Heading zero faces +Z: forward (sin, 0, cos), left (cos, 0, -sin). */
static void update_flinch(CharacterAnimation* animation,
                          const CharacterVisualState* state) {
    animation->flinch = clampf(state->flinch, 0.0f, 1.0f);
    animation->flinch_region = state->flinch_region;
    if (animation->flinch > 0.0f) {
        float yaw = animation->body_yaw * (float)MATH_DEG_TO_RAD;
        float s_yaw = sinf(yaw), c_yaw = cosf(yaw);
        animation->flinch_forward = state->flinch_x * s_yaw + state->flinch_z * c_yaw;
        animation->flinch_left = state->flinch_x * c_yaw - state->flinch_z * s_yaw;
    } else {
        animation->flinch_forward = 0.0f;
        animation->flinch_left = 0.0f;
    }
}

/* Event edges outrank a running one-shot. Priority order matters:
   DIED > RESPAWNED > ground/traversal edges (a respawn is followed by a LANDED
   that must not override the get-up). */
static void handle_events(CharacterAnimation* animation, const SkinnedModel* model,
                          const CharacterVisualState* state) {
    unsigned events = state->events;
    if (events & CHARACTER_EVENT_DIED)
        switch_role(animation, model, CHARACTER_CLIP_DEATH, 0.0f);
    else if (events & CHARACTER_EVENT_RESPAWNED)
        switch_role(animation, model, CHARACTER_CLIP_GET_UP,
                    ROLES[CHARACTER_CLIP_GET_UP].hold);
    else if (events & CHARACTER_EVENT_MANTLE_BEGAN)
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
}

void character_animation_update(CharacterAnimation* animation, const SkinnedModel* model,
                                const CharacterVisualState* state, float delta_time) {
    if (!animation || !model || !state) return;

    update_aim_weight(animation, state, delta_time);
    /* Facing reads aim_weight, so it must follow. */
    character_facing_update(animation, state, delta_time);
    /* Flinch reads body_yaw, so it must follow facing. */
    update_flinch(animation, state);
    handle_events(animation, model, state);

    /* Movement input cancels a landing or get-up. */
    if ((animation->role == CHARACTER_CLIP_JUMP_LAND ||
         animation->role == CHARACTER_CLIP_GET_UP) && state->has_movement_input)
        animation->hold = 0.0f;

    if (delta_time <= 0.0f) return;
    animation->time += delta_time * playback_rate(animation, state);
    if (animation->hold > 0.0f) animation->hold -= delta_time;
    if (animation->fading) {
        animation->fade -= delta_time;
        if (animation->fade <= 0.0f) animation->fading = 0;
    }
}

void character_animation_set_reach(CharacterAnimation* animation,
                                   float x, float y, float z, int valid) {
    if (!animation) return;
    animation->reach_target[0] = x;
    animation->reach_target[1] = y;
    animation->reach_target[2] = z;
    animation->reach_valid = valid;
}

float character_animation_body_yaw(const CharacterAnimation* animation) {
    return animation ? animation->body_yaw : 0.0f;
}

float character_animation_look_yaw(const CharacterAnimation* animation) {
    return animation ? animation->look_yaw : 0.0f;
}

void character_animation_pose(const CharacterAnimation* animation,
                              const SkinnedModel* model, Pose* out) {
    if (!animation || !model || !out) return;
    /* Order matters: the aim layer overwrites the spine, so twist must follow
       it; the hit reaction is additive and goes last. */
    compose_base(animation, model, out);
    character_aim_layer_apply(animation, model, out);
    character_twist_apply(animation, model, out);
    character_hit_reaction_apply(animation, model, out);
}
