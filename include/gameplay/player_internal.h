#ifndef PLAYER_INTERNAL_H
#define PLAYER_INTERNAL_H

#include "gameplay/player_controller.h"

/* Player mechanics are being migrated one file at a time to explicit state
   ownership. Keep legacy member spellings private to those implementation
   files; public consumers can only use the grouped state. */
#define coyote_time ground.coyote_time
#define jump_buffer_time ground.jump_buffer_time
#define air_time ground.air_time
#define air_jumps_used ground.air_jumps_used
#define is_grounded ground.is_grounded
#define slide_reentry_time slide.reentry_time
#define slide_time slide.time
#define slide_forward_x slide.forward_x
#define slide_forward_z slide.forward_z
#define is_sliding slide.active
#define wall_run_time wall_run.time
#define wall_run_cooldown wall_run.cooldown
#define wall_run_speed wall_run.speed
#define is_wall_running wall_run.active
#define wall_run_wall_index wall_run.wall_index
#define wall_run_side wall_run.side
#define wall_jumped wall_run.jumped
#define wall_climb_time wall_climb.time
#define wall_climb_speed wall_climb.speed
#define is_wall_climbing wall_climb.active
#define wall_climb_wall_index wall_climb.wall_index
#define grapple_x grapple.x
#define grapple_y grapple.y
#define grapple_z grapple.z
#define grapple_length grapple.length
#define is_grappling grapple.active
#define mantle_time mantle.time
#define mantle_start_x mantle.start_x
#define mantle_start_y mantle.start_y
#define mantle_start_z mantle.start_z
#define mantle_target_x mantle.target_x
#define mantle_target_y mantle.target_y
#define mantle_target_z mantle.target_z
#define mantle_exit_speed mantle.exit_speed
#define is_mantling mantle.active
#define is_ledge_hanging mantle.ledge_hanging

/* Shared surface between the player controller and the movement subsystems it
   drives. Not part of the public gameplay API - only player_*.c include this. */

/* Input decoded once per frame, so no subsystem re-reads raw keys. */
typedef struct {
    float wish_x;                 /* camera-relative move direction, unit */
    float wish_z;
    float camera_yaw_degrees;
    float camera_pitch_degrees;
    float delta_time;
    int has_move_input;
    /* Any movement key physically down, even when the pair of them cancels out
       and leaves no direction. Holding A and D together, which is what every
       strafe reversal passes through, gives has_move_input == 0 with the player
       plainly still asking to move; anything that would otherwise read that as
       letting go wants this instead. */
    int move_keys_held;
    int crouch_held;
    int crouch_pressed;
    int grounded_crouch;
    int slide_left;
    int slide_right;
    int slide_forward;
    int slide_backward;
    int jump_pressed;
    int grapple_pressed;
    int grapple_held;
} PlayerFrameInput;

/* --- shared body metrics --- */
#define PLAYER_RADIUS 0.35f
#define WALL_CONTACT_DISTANCE 0.12f
#define GRAPPLE_ANCHOR_HEIGHT 1.4f

/* --- locomotion (player_locomotion.c) --- */
/* Consumes the buffered jump; reads only state the timers already resolved.
   Takes the frame input because a wall jump steers toward the look direction. */
void player_locomotion_apply_jump(PlayerController* player,
                                  const PlayerFrameInput* in);
void player_locomotion_accelerate(PlayerController* player,
                                  const PlayerFrameInput* in);
void player_locomotion_integrate(PlayerController* player,
                                 const PlayerFrameInput* in);
void player_locomotion_resolve_vertical(PlayerController* player,
                                        const PlayerFrameInput* in);
/* Downward pull for a player off every surface, ramped by air_time. The wall
   run owns its own gravity while attached and calls this only when it lets go. */
float player_locomotion_air_gravity(const PlayerController* player);
/* Bleeds horizontal speed while airborne, harder the longer air_time has run.
   Caller decides whether the player counts as airborne. */
void player_locomotion_apply_air_drag(PlayerController* player,
                                      const PlayerFrameInput* in);

/* --- wall running (player_wallrun.c) --- */
void player_wallrun_update(PlayerController* player, const PlayerFrameInput* in);

/* --- wall climbing (player_wallclimb.c) --- */
/* Runs before wall running and outranks it: a head-on wall is climbed, an
   alongside wall is run. Owns vertical velocity while a climb is active. */
void player_wallclimb_update(PlayerController* player,
                             const PlayerFrameInput* in);

/* --- sliding (player_slide.c) --- */
/* Starts a slide and captures the line it will hold. Every entry point into a
   slide must go through this - see player_slide.c. */
void player_slide_begin(PlayerController* player);
void player_slide_try_enter(PlayerController* player, const PlayerFrameInput* in);
void player_slide_update(PlayerController* player, const PlayerFrameInput* in);
void player_slide_resolve_state(PlayerController* player,
                                const PlayerFrameInput* in);
void player_slide_apply_burst(PlayerController* player);

/* --- grapple (player_grapple.c) --- */
void player_grapple_update(PlayerController* player, const PlayerFrameInput* in);

/* --- mantle (player_mantle.c) --- */
/* Holds the player on a ledge for as long as they push into it. Returns
   non-zero while hanging, in which case it has owned the frame. */
int player_ledge_hang_update(PlayerController* player, const PlayerFrameInput* in);
/* Read-only: would a mantle start this frame if the press were there. Used by
   the air jump, which must not spend a press a ledge has a claim on. */
int player_mantle_available(const PlayerController* player,
                            const PlayerFrameInput* in);
/* Returns non-zero when a climb started and owns the rest of the frame. */
int player_mantle_try_begin(PlayerController* player, const PlayerFrameInput* in);
void player_mantle_advance(PlayerController* player, const PlayerFrameInput* in);

#endif
