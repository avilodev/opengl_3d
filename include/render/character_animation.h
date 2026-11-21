#ifndef RENDER_CHARACTER_ANIMATION_H
#define RENDER_CHARACTER_ANIMATION_H

#include "render/skinned_model.h"
#include "render/character_state.h"
#include "gameplay/weapon_spec.h"

/* Character rig animation: picks clips from a CharacterVisualState and layers
   aim, facing, lean and hit reactions on top. Read-only with respect to the
   state, so a local controller and a network packet drive it the same way.

   One instance per body. Implementation is split across:
     character_animation.c      role state machine, cross-fade, public API
     character_facing.c         body heading, leg twist, head look
     character_layers.c         aim layer, twist/lean channels, hit reaction
     character_authored_pose.c  hand-authored poses and arm IK */

enum {
    CHARACTER_CLIP_IDLE,
    CHARACTER_CLIP_WALK,
    CHARACTER_CLIP_JOG,
    CHARACTER_CLIP_SPRINT,
    CHARACTER_CLIP_CROUCH_IDLE,
    CHARACTER_CLIP_CROUCH_WALK,
    CHARACTER_CLIP_JUMP_START,
    CHARACTER_CLIP_JUMP_LOOP,
    CHARACTER_CLIP_JUMP_LAND,
    CHARACTER_CLIP_SLIDE_START,
    CHARACTER_CLIP_SLIDE_LOOP,
    CHARACTER_CLIP_SLIDE_EXIT,
    CHARACTER_CLIP_CLIMB,
    CHARACTER_CLIP_HANG,
    CHARACTER_CLIP_DEATH,    /* state role; plays once and holds the last frame */
    CHARACTER_CLIP_GET_UP,   /* one-shot after a respawn */
    CHARACTER_CLIP_COUNT
};

/* Authored poses stand in for clips the library lacks (e.g. the ledge hang).
   Limits: bones per pose, and poses per rig. */
#define CHARACTER_AUTHORED_BONES 31
#define CHARACTER_AUTHORED_POSES 4

/* Bones driven by the facing/lean/hit channels. The first four (pelvis + three
   spine joints) form the leg chain; the rest carry the head look. */
#define CHARACTER_TWIST_BONES 6
#define CHARACTER_TWIST_LEG_BONES 4

/* Aim-offset poses, in blend order (muzzle pitch -78 to +84 degrees). */
enum {
    CHARACTER_AIM_DOWN,
    CHARACTER_AIM_NEUTRAL,
    CHARACTER_AIM_UP,
    CHARACTER_AIM_COUNT
};

typedef struct {
    /* ---- clip playback ---- */
    int clip[CHARACTER_CLIP_COUNT];  /* role -> model clip index, -1 if absent */
    int role;                        /* current role */
    float time;                      /* seconds into the clip */
    float hold;                      /* seconds a one-shot still owns the body */
    int oneshot_clip;                /* non-role clip playing (swing, draw), or -1 */

    /* Cross-fade from a snapshot of the base pose at the switch. */
    Pose fade_from;
    float fade;                      /* seconds left in the fade */
    int fading;

    /* ---- aim layer ----
       Upper-body pose masked over locomotion (spine_02 and above). */
    int aim_clip[CHARACTER_AIM_COUNT];
    PoseMask upper;                  /* bones the layer replaces */
    short upper_root;                /* spine_02 node index, for chest stabilising */
    PoseMask arms;                   /* both arms, for lowering the carry */
    float aim_pitch;                 /* look pitch, degrees */
    float aim_ads;                   /* 0 hip, 1 on the sight */
    float aim_weight;                /* eased layer weight; 0 while a clip owns the upper body */

    /* ---- lean ----
       Peek amount, -1..1, positive to the body's left. Pre-smoothed by the
       controller; applied as a spine bend. */
    float lean;

    /* ---- authored poses ----
       Resolved at init. node[i] is -1 for a missing bone so entries stay
       aligned with the pose table; axis[i] is that bone's rest direction. */
    struct {
        short node[CHARACTER_AUTHORED_BONES];
        float axis[CHARACTER_AUTHORED_BONES][3];
        int count;
    } authored[CHARACTER_AUTHORED_POSES];

    /* ---- reaching arm ----
       Model-space IK target for a pose's reaching hand (e.g. the hang's ledge). */
    float reach_target[3];
    int reach_valid;
    short reach_upper, reach_lower;
    float reach_upper_length, reach_lower_length;

    /* ---- hit reaction ----
       Additive spine bend, resolved into the body's own frame. Pelvis excluded
       so locomotion is never interrupted. */
    float flinch;
    float flinch_forward;            /* positive: hit from the front */
    float flinch_left;               /* positive: hit on the body's left */
    int flinch_region;

    /* ---- facing ----
       The chest faces the look direction; legs twist underneath toward the
       movement direction (pelvis turns, spine turns back, so the chest is
       unaffected). Moving further round than a strafe reverses the walk cycle.
       Standing, the waist winds up until a step is triggered. */
    float body_yaw;                  /* degrees, drawn heading */
    float leg_yaw;                   /* degrees, legs relative to body */
    float leg_rate;                  /* +1 forward, -1 reversed, eased */
    float look_yaw;                  /* degrees, head relative to chest */
    float facing_weight;             /* how much of the heading follows the aim */
    int stepping;                    /* standing step in progress */

    short twist_node[CHARACTER_TWIST_BONES];
    int twist_count;
    int twist_legs;                  /* 0 if any leg-chain bone is missing */
} CharacterAnimation;

/* Resolves clip and bone names against `model`. Safe on a model with no clips:
   every role resolves to -1 and the body holds its bind pose. */
void character_animation_init(CharacterAnimation* animation,
                              const SkinnedModel* model,
                              const WeaponClipSet* clips);

/* Plays a clip by index for `hold_seconds`, cross-fading in, then returns to
   the state's role. */
void character_animation_play_oneshot(CharacterAnimation* animation,
                                      const SkinnedModel* model,
                                      int clip_index, float hold_seconds);

/* Advances playback and selects the role `state` calls for. `delta_time` is
   frame time; zero freezes. One-shots are driven by `state->events`, not by
   diffing flags, so dropped network frames still trigger them. */
void character_animation_update(CharacterAnimation* animation, const SkinnedModel* model,
                                const CharacterVisualState* state, float delta_time);

/* Sets the reaching hand's model-space target. Call before update each frame;
   `valid` 0 leaves the arm on its authored direction. */
void character_animation_set_reach(CharacterAnimation* animation,
                                   float x, float y, float z, int valid);

/* Drawn heading in degrees (same convention as state heading). Zero before
   the first update. */
float character_animation_body_yaw(const CharacterAnimation* animation);

/* Head yaw relative to the chest, degrees, positive left. Also drives the
   first-person viewmodel neck (see render/viewmodel_neck.h). */
float character_animation_look_yaw(const CharacterAnimation* animation);

/* Writes this frame's final pose: base clip, aim, twist/lean, hit reaction. */
void character_animation_pose(const CharacterAnimation* animation,
                              const SkinnedModel* model, Pose* out);

/* Re-resolves the aim clips for the held weapon. Call on weapon change; other
   animation state is preserved. NULL or unknown names disable the aim layer.
   Note: a stale set is silent (a sword's single pose also "works" for a rifle). */
void character_animation_set_clips(CharacterAnimation* animation,
                                   const SkinnedModel* model,
                                   const WeaponClipSet* clips);

/* Two-bone IK: places either hand on a model-space point, rewriting only the
   upper and lower arm. Run after the other layers so it sees final shoulder
   positions. NULL target leaves that arm alone; unreachable targets are
   skipped rather than over-straightened. */
void character_animation_reach_hands(const SkinnedModel* model,
                                     const float* right_target,
                                     const float* left_target, Pose* pose);

#endif
