#ifndef RENDER_CHARACTER_RIG_INTERNAL_H
#define RENDER_CHARACTER_RIG_INTERNAL_H

#include "render/character_animation.h"
#include "utils/mat4.h"

/* Shared between the character_animation implementation files only. Not part
   of the public rig API. */

/* ---- authored poses (character_authored_pose.c) ---- */

/* One bone pointed along a model-space direction (+y up, +z forward). */
typedef struct {
    const char* bone;
    float x, y, z;
} AuthoredBone;

/* A hand-built pose. `slot` indexes CharacterAnimation.authored. If `reaches`,
   the named arm is IK-solved onto reach_target instead of its authored
   direction. Only one reaching pose is supported at a time. */
typedef struct {
    int slot;
    int reaches;
    const char* reach_upper;
    const char* reach_lower;
    const char* reach_hand;
    const float* elbow_pole;
    int count;
    const AuthoredBone* bones;
} AuthoredPose;

extern const AuthoredPose CHARACTER_HANG_POSE;

/* Clears resolved poses and reach bones. Call once before resolving. */
void character_authored_reset(CharacterAnimation* animation);
/* Resolves one pose's bones against the rig. */
void character_authored_resolve(CharacterAnimation* animation,
                                const SkinnedModel* model, const AuthoredPose* pose);
/* Overwrites the pose's bones in `out`, solving the reaching arm if set. */
void character_authored_apply(const CharacterAnimation* animation,
                              const SkinnedModel* model,
                              const AuthoredPose* pose, Pose* out);

/* ---- facing (character_facing.c) ---- */

/* Updates body_yaw, leg_yaw, leg_rate, look_yaw and stepping. Reads aim_weight,
   so call after it is updated for the frame. */
void character_facing_update(CharacterAnimation* animation,
                             const CharacterVisualState* state, float delta_time);

/* ---- layers (character_layers.c) ---- */

/* Builds the aim masks and resolves the twist bone chain. */
void character_layers_init(CharacterAnimation* animation, const SkinnedModel* model);
void character_aim_layer_apply(const CharacterAnimation* animation,
                               const SkinnedModel* model, Pose* out);
void character_twist_apply(const CharacterAnimation* animation,
                           const SkinnedModel* model, Pose* out);
void character_hit_reaction_apply(const CharacterAnimation* animation,
                                  const SkinnedModel* model, Pose* out);

/* A model-space axis expressed in a bone's parent frame (scale removed).
   NULL parent passes the axis through. */
void character_axis_in_parent(const Mat4* parent, const float axis[3], float out[3]);

#endif
