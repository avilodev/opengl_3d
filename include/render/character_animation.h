#ifndef RENDER_CHARACTER_ANIMATION_H
#define RENDER_CHARACTER_ANIMATION_H

#include "render/skinned_model.h"
#include "render/character_state.h"
#include "gameplay/weapon_spec.h"

/* Which clip the body is playing, and how it gets from the last one to this
   one. This is the half of the character rig that reads movement state; what
   that pose then looks like on screen is character_renderer's business.

   It is deliberately a reader, and it reads a CharacterVisualState rather than
   a player: the simulation - or a packet - decides that the body is sliding,
   and this decides that a slide looks like Slide_Start followed by Slide_Loop.
   Nothing here can write back to whatever produced the state, and nothing here
   knows whether that was a controller or a wire.

   One of these per body. It holds a playhead, a cross-fade snapshot and the
   facing the body has wound up to, none of which two bodies can share. */

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
    CHARACTER_CLIP_COUNT
};

/* Bones the wall hang poses by hand. */
#define CHARACTER_HANG_BONES 31

/* Bones the facing twists, in one table with three channels: the leg turn, the
   head look, and the lean. The first four are the leg chain - the pelvis, which
   carries the legs, and the three spine joints that carry the turn back out of
   the chest - and the rest are what the head looks with. The lean channel uses
   the same six bones about a different axis. */
#define CHARACTER_TWIST_BONES 6
#define CHARACTER_TWIST_LEG_BONES 4

/* The aim layer's three poses, in the order the blend runs through them. They
   are an aim offset, not animations: three static poses whose muzzle pitch
   runs from -78 to +84 degrees, and the body picks a point between them from
   where the body is looking. */
enum {
    CHARACTER_AIM_DOWN,
    CHARACTER_AIM_NEUTRAL,
    CHARACTER_AIM_UP,
    CHARACTER_AIM_COUNT
};

typedef struct {
    int clip[CHARACTER_CLIP_COUNT];  /* role -> the model's clip index, -1 if absent */
    int role;                        /* what is playing now */
    float time;                      /* seconds into it */
    float hold;                      /* seconds a one-shot still owns the body */
    /* A clip outside the locomotion roles that owns the body right now - a
       swing, a draw - or -1 for none. Read in place of the role's clip, so
       there is one playhead and not two. */
    int oneshot_clip;

    /* A cross-fade blends out of the pose the body was in at the moment the
       clip changed, rather than out of the old clip still running. One
       snapshot costs a copy and spares the bookkeeping of a second playhead,
       and over a fade this short the two are indistinguishable. */
    Pose fade_from;
    float fade;                      /* seconds left in the cross-fade */
    int fading;

    /* ---- the aim layer ----
       An upper-body pose laid over whatever the legs are doing, so the body
       holds the rifle through every locomotion clip without any of them
       knowing the rifle exists. It is a mask, not a clip: `upper` owns
       spine_02 and everything above it, which is the whole torso, both arms
       and the head, and leaves the pelvis and the legs alone. Crouching lives
       almost entirely in the pelvis and the legs, so the two compose without
       either fighting the other. */
    int aim_clip[CHARACTER_AIM_COUNT];
    PoseMask upper;                  /* spine_02 up: what the layer replaces */
    /* The bone that mask is rooted at, as an index. The layer needs it by name
       to build the mask and by index to stabilise the chest on it. */
    short upper_root;
    PoseMask arms;                   /* the two arms, for the carry lower */
    float aim_pitch;                 /* where the body is looking, degrees */
    float aim_ads;                   /* 0 carried at the ready, 1 on the sight */
    /* How much of the upper body the aim layer has, eased over FADE_SECONDS.
       One almost always: a body on its feet holds its rifle toward what it is
       looking at whatever its legs are doing. A slide, a mantle and a wall
       climb take it back while the sight is down, and hand it straight over
       when the sight goes up; a ledge hang keeps it regardless, being the one
       pose that is holding the body's weight on its arms. */
    float aim_weight;

    /* ---- the lean ----
       How far the body is peeked, -1 to 1, positive to the body's own left -
       the same number the player controller publishes, refused on the frames a
       clip owns the whole body. It is a bend of the spine rather than a tilt of
       the transform: see the lean channel in TWIST_BONES. Nothing eases it
       here, because the controller already did. */
    float lean;

    /* ---- the wall hang ----
       Resolved once, because the pose is authored rather than animated:
       nothing in the library is a ledge hang, so it is built by pointing
       eighteen bones along directions chosen by eye. `node` is which bone
       each entry moves and `axis` is that bone's own rest direction - where
       its first child sits - which is what the direction is measured against. */
    short hang_node[CHARACTER_HANG_BONES];
    float hang_axis[CHARACTER_HANG_BONES][3];
    int hang_count;

    /* The gripping arm reaches for the ledge rather than holding an authored
       direction, because a ledge is wherever it is: the pose gets the hand
       close and this puts it on the lip. In the model's own space, which is
       what the renderer converts the world point into. */
    float hang_grip[3];
    int hang_grip_valid;
    short hang_upper_l, hang_lower_l;   /* the two bones the reach solves for */
    float hang_upper_length, hang_lower_length;

    /* ---- facing ----
       Which way the body stands, and where its legs point inside that.

       The body stands where it is looking, sight up or not, shooting or not.
       Where someone is facing is not a detail of aiming, it is the single thing
       everyone else reads them by, and a body that only turned to its look
       direction once a trigger was involved would spend most of a fight
       pointing somewhere nobody was looking. So the chest, the arms, the head
       and the rifle they hold go to the look, and whatever the feet are doing
       happens underneath that: `body_yaw` is where the chest and the gun point,
       and `leg_yaw` is how far round from it the legs are.

       `facing_weight` is how much of that the aim actually gets, and it is only
       ever 1 or on its way to or from 0. A slide, a mantle, a hang, a climb and
       a wall run take the body back - see `aim_weight` - and it is eased rather
       than switched because a body's heading is a transform and not a pose: the
       clip cross-fade that covers a change of clip cannot cover this, so a
       handover thrown in one frame is a body snapping round.

       The legs get there by twisting the pelvis and untwisting the spine by the
       same amount, which leaves the chest exactly where the aim put it and
       costs nothing above the waist. Rotations about a common axis commute, so
       this is exact rather than approximate: the gun points where the crosshair
       is whatever the legs are doing.

       Two things bound the twist. The legs never sit more than
       FACING_LEG_YAW_MAX round, because past that a body is not twisting, it is
       broken - a strafe holds the clamp and the feet slide a few degrees, which
       nobody sees. And a body walking further round than a strafe plays its
       walk backwards instead: `leg_rate` goes negative, the legs come back to
       near the aim, and the cycle running in reverse is what a backpedal is.

       ---- the head look ----
       The same mechanism, one layer up: the neck and the head turn to the look
       direction over whatever the body is doing, with the chest leading a
       quarter of the way into it. `look_yaw` is measured off the chest rather than off the
       world, so wherever the body has already gone to the aim this is zero and
       costs nothing - the two layers hand over without either knowing about the
       other, and no arbitration is needed between them.

       What it is for is the wall run, which keeps its own heading and takes the
       aim layer anyway: the body holds the wall and the head is then the only
       thing left to say where the body is actually looking. It also carries
       the eased handovers into and out of that, and any residue the standing
       plant below leaves behind. The clips that own the whole body refuse the
       look along with the layer, because they are authored down to the neck.

       ---- standing ----
       Standing, the chest goes to the look and the feet do not: they hold what
       they were left with, so the turn winds up the waist, and the wind-up is
       what reads as a body twisting to look rather than a body pivoting on ice.
       Once the waist runs out the feet give up and take a step, which `stepping`
       holds until they are square again - a committed turn rather than a shove
       every frame the look stays past the limit. */
    float body_yaw;                  /* degrees, what the body is drawn at */
    float leg_yaw;                   /* degrees, the legs relative to that */
    float leg_rate;                  /* +1 forward, -1 reversed, eased between */
    float look_yaw;                  /* degrees, the head relative to the chest */
    float facing_weight;             /* how much of the yaw the aim has */
    int stepping;                    /* feet mid-step, standing */

    short twist_node[CHARACTER_TWIST_BONES];
    int twist_count;
    /* The leg chain is all or nothing, and this is whether it landed. A body
       that twisted its pelvis with half a spine to give the turn back would
       carry it up into the gun, so a rig missing one of those four keeps its
       travelling heading and only the head looks around. */
    int twist_legs;
} CharacterAnimation;

/* Resolves clip names against `model`. Safe on a model that loaded no clips:
   every role resolves to -1 and the body holds its bind pose. */
void character_animation_init(CharacterAnimation* animation,
                              const SkinnedModel* model,
                              const WeaponClipSet* clips);

/* Advances the playhead and picks the clip `state` now calls for.
   `delta_time` is frame time, not simulation time - animation is presentation,
   and a body that only moved on simulation steps would judder on a display
   faster than the step rate. Pass zero to freeze it, which is what a paused
   game wants.

   The one-shots come from `state->events` rather than from comparing this
   frame's flags against last frame's. A body whose state arrives over a wire
   does not get every frame, and an edge inferred from the two frames either
   side of a dropped packet is a jump or a landing that never plays. */
/* Plays a clip by index for `hold` seconds, through the same snapshot and
   cross-fade a jump takes. Hands the body back to the state's own role when
   the hold expires. */
void character_animation_play_oneshot(CharacterAnimation* animation,
                                      const SkinnedModel* model,
                                      int clip_index, float hold_seconds);

void character_animation_update(CharacterAnimation* animation, const SkinnedModel* model,
                                const CharacterVisualState* state, float delta_time);

/* Where on the ledge the hanging hand should land, in the model's own space.
   Set before the update each frame; `valid` zero leaves the arm on its
   authored direction, which is what a body that is not hanging wants. */
void character_animation_set_grip(CharacterAnimation* animation,
                                  float x, float y, float z, int valid);

/* The yaw the body should be drawn at, in the same degrees the state's own
   heading is in. Travelling heading where a clip owns the body, look heading
   otherwise. Read after the update; before the first one it is zero. */
float character_animation_body_yaw(const CharacterAnimation* animation);

/* The head's yaw relative to the chest, in degrees, positive to the left.

   Public because FIRST person wants it. With the camera inside the head, this
   is how far the body has been left behind by a turn - which is exactly what
   the viewmodel's neck is, and it is computed here whichever camera is up: the
   pose is composed whatever `visible` says, for the reasons character_renderer.c
   sets out. See render/viewmodel_neck.h. */
float character_animation_look_yaw(const CharacterAnimation* animation);

/* Writes the pose the body should hold this frame. */
void character_animation_pose(const CharacterAnimation* animation,
                              const SkinnedModel* model, Pose* out);

/* Which three poses the aim layer blends between, named by the weapon the body
   is holding. Resolved at init and re-resolved whenever what the body is
   holding changes - a body can be handed a different weapon mid-round, and the
   aim clips are the one part of the animation that is a fact about the weapon
   rather than about the body.

   Everything else on the animation is left alone: a playhead, a facing and an
   eased aim weight belong to the body and survive it swapping what it carries.

   A NULL clip set, or a name the rig has not got, leaves the layer resolving to
   nothing, which is the empty-hands look rather than a fallback. Note that a
   weapon with no aim pitch to speak of names ONE pose three times - a sword
   does - so a stale set of these is silent: the body goes on aiming with the
   sword's single pose while holding a rifle, and neither the mesh in its hand
   nor the rounds it fires say anything is wrong. */
void character_animation_set_clips(CharacterAnimation* animation,
                                   const SkinnedModel* model,
                                   const WeaponClipSet* clips);

/* Puts one or both hands on a point in the MODEL's own space, by solving the
   two bones of each arm.

   The pose is an input as well as an output: the shoulders are wherever the
   clip and the layers above it left them, and only the two bones below each
   shoulder are rewritten. So this is a layer like the aim and the twist, and
   it goes last for the same reason they go in the order they do - it has to
   see where the shoulders actually ended up.

   Either target may be NULL, which leaves that arm alone. A target further
   away than the arm is long is reached AT rather than reached: the arm
   straightens toward it, which is what an arm does.

   It knows nothing about what is being held. The caller works out the points -
   a hilt, a scabbard, a ledge - and this puts the hands on them. */
void character_animation_reach_hands(const SkinnedModel* model,
                                     const float* right_target,
                                     const float* left_target, Pose* pose);

#endif
