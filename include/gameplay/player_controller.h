#ifndef PLAYER_CONTROLLER_H
#define PLAYER_CONTROLLER_H

#include "input/commands.h"

typedef struct {
    float coyote_time;
    float jump_buffer_time;
    /* Time since the player last touched any supporting surface. */
    float air_time;
    /* Air jumps spent since that surface. Zeroed everywhere air_time is, which
       is the definition of having touched something. */
    int air_jumps_used;
    int is_grounded;
} PlayerGroundState;

typedef struct {
    float reentry_time;
    float time;
    float forward_x;
    float forward_z;
    int active;
} PlayerSlideState;

typedef struct {
    float time;
    float cooldown;
    float speed;
    int active;
    int wall_index;
    int side;
    /* One-frame event consumed by the camera. */
    int jumped;
} PlayerWallRunState;

typedef struct {
    float time;
    float speed;
    int active;
    int wall_index;
} PlayerWallClimbState;

typedef struct {
    float x, y, z;
    float length;
    int active;
} PlayerGrappleState;

typedef struct {
    float time;
    float start_x, start_y, start_z;
    float target_x, target_y, target_z;
    float exit_speed;
    int active;
    /* Holding a direction into a reachable ledge instead of climbing it. Shares
       this struct because it shares the ledge search and the climb out of it. */
    int ledge_hanging;
    /* The point on the ledge's top edge the player is holding, in world units.
       The renderer needs it to put the character's hand on the ledge rather
       than in the air near it: the player's feet stay where they were when
       they grabbed, and the lip can be anywhere from a third of a metre to two
       and a third above them, at whatever distance the reach allowed. */
    float grip_x, grip_y, grip_z;
} PlayerMantleState;

typedef struct {
    float x;
    float y;
    float z;
    float velocity_x;
    float velocity_z;
    float vertical_velocity;
    float heading_degrees;
    float walk_time;
    /* Smoothed authored pose in [-1, 1]: left is negative, right positive.
       Camera, viewmodel, and character rig all consume this same value. */
    float lean;
    /* Which way the player has committed to leaning: -1, 0 or +1. The pose
       above eases toward it, so this is the decision and that is the picture. */
    int lean_target;
    float wall_normal_x;
    float wall_normal_z;
    PlayerGroundState ground;
    PlayerSlideState slide;
    PlayerWallRunState wall_run;
    PlayerWallClimbState wall_climb;
    PlayerGrappleState grapple;
    PlayerMantleState mantle;
    int is_moving;
    int has_movement_input;
    int is_sprinting;
    int is_crouching;
} PlayerController;

void player_controller_init(PlayerController* player);
/* Puts the player somewhere with nothing running: every mechanic state cleared,
   every velocity zero, at a position and a facing.

   It is init followed by a pose rather than a handful of assignments, and
   deliberately: a respawn that cleared only what somebody remembered to clear
   is a player who comes back still wall running, on a slide's forward vector,
   with a grapple anchored across the map. Routed through init, the seventh
   movement mechanic is cleared by this the day it is added and nobody has to
   notice. */
void player_controller_place(PlayerController* player, float x, float y, float z,
                             float yaw_degrees);
/* Update view-independent pose input before camera and rendering consumers.

   Sprinting refuses the lean and clears it, unless the player is on a wall -
   see the comment on the definition for why the wall is exempt. Called before
   player_controller_update, so the sprint and wall states it reads are one
   simulation step old; at this step length that is invisible, and reading them
   fresh would mean leaning after the camera had already been posed from them. */
void player_controller_update_lean(PlayerController* player,
                                   const PlayerCommand* command, float delta_time);
void player_controller_update(PlayerController* player, const PlayerCommand* command,
                              float camera_yaw_degrees, float camera_pitch_degrees,
                              float delta_time);

#endif
