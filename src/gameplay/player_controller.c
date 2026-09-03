#include "gameplay/player_internal.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>

/* Owns player state and the order the movement subsystems run in. The order is
   load-bearing: sliding is resolved before the grapple so a slide-jump keeps
   its burst, and mantling is tested before wall running so cresting a wall
   climbs it instead of sticking to it. */

void player_controller_init(PlayerController* player) {
    player->x = 0.0f;
    player->y = 0.0f;
    player->z = 0.0f;
    player->velocity_x = 0.0f;
    player->velocity_z = 0.0f;
    player->vertical_velocity = 0.0f;
    player->heading_degrees = 0.0f;
    player->walk_time = 0.0f;
    player->lean = 0.0f;
    player->lean_target = 0;
    player->wall_normal_x = 0.0f;
    player->wall_normal_z = 0.0f;
    player->wall_run_time = 0.0f;
    player->wall_run_cooldown = 0.0f;
    player->wall_climb_time = 0.0f;
    player->wall_climb_speed = 0.0f;
    player->slide_reentry_time = 0.0f;
    player->slide_time = 0.0f;
    player->slide_forward_x = 0.0f;
    player->slide_forward_z = -1.0f;
    player->coyote_time = MOVE_COYOTE_TIME;
    player->jump_buffer_time = 0.0f;
    player->air_time = 0.0f;
    player->air_jumps_used = 0;
    player->wall_run_speed = 0.0f;
    player->grapple_x = player->grapple_y = player->grapple_z = 0.0f;
    player->grapple_length = 0.0f;
    player->mantle_time = 0.0f;
    player->mantle_start_x = player->mantle_start_y = player->mantle_start_z = 0.0f;
    player->mantle_target_x = player->mantle_target_y = player->mantle_target_z = 0.0f;
    player->mantle_exit_speed = 0.0f;
    player->is_moving = 0;
    player->has_movement_input = 0;
    player->is_sprinting = 0;
    player->is_grounded = 1;
    player->is_wall_running = 0;
    player->is_crouching = 0;
    player->is_sliding = 0;
    player->is_grappling = 0;
    player->is_mantling = 0;
    player->is_ledge_hanging = 0;
    player->is_wall_climbing = 0;
    player->wall_jumped = 0;
    player->wall_run_wall_index = -1;
    player->wall_climb_wall_index = -1;
    player->wall_run_side = 0;
}

void player_controller_place(PlayerController* player, float x, float y, float z,
                             float yaw_degrees) {
    if (!player) return;
    player_controller_init(player);
    player->x = x;
    player->y = y;
    player->z = z;
    player->heading_degrees = yaw_degrees;
}

/* Lean is held as a decision rather than a key. Each key toggles its own side
   on, toggles it off again if that is where the player already is, and takes
   the lean straight across if they are leaning the other way - so from centre Q
   leans left and E leans right, from a left lean Q returns to centre and E
   crosses to the right, and the same the other way round. That is the
   Rainbow Six peek: the player commits to an angle and both hands are free
   again, instead of holding a key for as long as they want to be looking round
   the corner.

   Only the target is toggled. The pose itself still eases at
   MOVE_LEAN_RESPONSE, so a toggle is a lean-over and not a snap, and every
   consumer of player->lean - camera roll and offset, viewmodel, character rig -
   is untouched by the change. */
/* Leaning is a stationary act - a peek round a corner - and a sprint is the
   opposite of stationary, so the two do not mix. Sprinting refuses the lean and
   takes back any lean already held.

   Being ON A WALL is exempt, and deliberately: a wall run is entered at speed
   with the sprint key still down, so gating on the sprint alone would have
   stripped the tilt off the one manoeuvre whose whole read is tilt. Wall
   climbing is exempt for the same reason - it is also being on a wall, and
   there is no version of this where tilt vanishing halfway up a wall is what
   the player meant.

   The camera's bank into a wall run does not come through here at all - it
   overrides the roll target outright in third_person_camera - but the viewmodel
   and the character rig do read this value, so without the exemption the gun
   and the body would have gone square while the view stayed banked. */
void player_controller_update_lean(PlayerController* player,
                                   const PlayerCommand* command, float delta_time) {
    int on_wall = player->wall_run.active || player->wall_climb.active;
    int lean_refused = player->is_sprinting && !on_wall;

    if (lean_refused) {
        /* The commitment is cleared, not just the pose. Zeroing only the pose
           would leave the decision standing, and the tilt would spring back the
           instant the player let go of sprint - which is the opposite of
           resetting it. */
        player->lean_target = 0;
    } else {
        if (command->lean_left_pressed)
            player->lean_target = player->lean_target > 0 ? 0 : 1;
        if (command->lean_right_pressed)
            player->lean_target = player->lean_target < 0 ? 0 : -1;
    }

    float target = (float)player->lean_target;
    /* Exponential response is framerate independent and naturally returns to
       centre when neither key (or both keys) is held - which is also what eases
       the tilt out when a sprint takes it away, so there is no second path for
       that and no snap. */
    float blend = 1.0f - expf(-MOVE_LEAN_RESPONSE * delta_time);
    player->lean += (target - player->lean) * blend;
}

/* Convert the focused command into camera-relative movement for the mechanics. */
static PlayerFrameInput decode_command(const PlayerCommand* command,
                                     float camera_yaw_degrees,
                                     float camera_pitch_degrees,
                                     float delta_time,
                                     const PlayerController* player) {
    PlayerFrameInput in;
    float right = command->move_right;
    float forward = command->move_forward;
    float length = sqrtf(right * right + forward * forward);

    in.camera_yaw_degrees = camera_yaw_degrees;
    in.camera_pitch_degrees = camera_pitch_degrees;
    in.delta_time = delta_time;
    in.has_move_input = length > 0.01f;
    in.slide_left = command->move_right < 0.0f;
    in.slide_right = command->move_right > 0.0f;
    in.slide_forward = command->move_forward > 0.0f;
    in.slide_backward = command->move_forward < 0.0f;
    in.move_keys_held = in.slide_left || in.slide_right ||
                        in.slide_forward || in.slide_backward;
    in.crouch_held = command->crouch_held;
    in.crouch_pressed = command->crouch_pressed;
    in.grounded_crouch = in.crouch_held && player->is_grounded;
    in.jump_pressed = command->jump_pressed;
    in.grapple_pressed = command->grapple_pressed;
    in.grapple_held = command->grapple_held;

    in.wish_x = 0.0f;
    in.wish_z = 0.0f;
    if (in.has_move_input) {
        right /= length;
        forward /= length;
        float yaw = camera_yaw_degrees * MATH_DEG_TO_RAD;
        in.wish_x = right * cosf(yaw) - forward * sinf(yaw);
        in.wish_z = right * -sinf(yaw) - forward * cosf(yaw);
    }
    return in;
}

static void advance_timers(PlayerController* player, const PlayerFrameInput* in) {
    if (in->jump_pressed)
        player->jump_buffer_time = MOVE_JUMP_BUFFER_TIME;
    else if (player->jump_buffer_time > 0.0f)
        player->jump_buffer_time = fmaxf(0.0f,
                                         player->jump_buffer_time - in->delta_time);
    if (player->is_grounded)
        player->coyote_time = MOVE_COYOTE_TIME;
    else
        player->coyote_time = fmaxf(0.0f, player->coyote_time - in->delta_time);

    player->wall_run_cooldown = fmaxf(0.0f,
                                      player->wall_run_cooldown - in->delta_time);
    player->slide_reentry_time = fmaxf(0.0f,
                                       player->slide_reentry_time - in->delta_time);
}

/* Heading and the walk cycle follow from wherever the frame left the player. */
static void update_pose(PlayerController* player, const PlayerFrameInput* in) {
    float horizontal_speed = sqrtf(player->velocity_x * player->velocity_x +
                                   player->velocity_z * player->velocity_z);
    player->is_moving = horizontal_speed > 0.1f;
    if (player->is_moving) {
        player->heading_degrees = atan2f(player->velocity_x,
                                         player->velocity_z) / MATH_DEG_TO_RAD;
        player->walk_time += in->delta_time * MOVE_WALK_ANIMATION_SPEED *
                             (horizontal_speed / MOVE_WALK_SPEED);
    }
}

void player_controller_update(PlayerController* player, const PlayerCommand* command,
                              float camera_yaw_degrees, float camera_pitch_degrees,
                              float delta_time) {
    PlayerFrameInput in = decode_command(command, camera_yaw_degrees,
                                         camera_pitch_degrees, delta_time, player);

    /* Consumed by the camera, which runs before this and so still sees the
       frame it was set on. Cleared here, ahead of every early return. */
    player->wall_jumped = 0;

    /* A mantle in progress owns the whole frame. */
    if (player->is_mantling) {
        player_mantle_advance(player, &in);
        return;
    }

    player->has_movement_input = in.has_move_input;
    player->is_sprinting = in.has_move_input && !in.grounded_crouch &&
        command->sprint_held;

    advance_timers(player, &in);
    player_locomotion_apply_jump(player, &in);

    player_slide_try_enter(player, &in);
    if (player->is_sliding && player->is_grounded)
        player_slide_update(player, &in);
    else
        player_locomotion_accelerate(player, &in);
    player_slide_resolve_state(player, &in);

    player_grapple_update(player, &in);
    player_locomotion_integrate(player, &in);

    if (player_mantle_try_begin(player, &in)) return;
    /* After the mantle, so a buffered jump climbs the ledge instead of hanging
       on it, and before the wall verbs, so hanging is not also a wall run. */
    if (player_ledge_hang_update(player, &in)) return;

    /* A head-on wall is climbed and an alongside wall is run, so the climb is
       resolved first and the run sees only what it left behind. Both sit after
       the mantle test, which means cresting a ledge still outranks either. */
    player_wallclimb_update(player, &in);
    player_wallrun_update(player, &in);
    player_locomotion_resolve_vertical(player, &in);

    /* Settled last, on the state the frame actually ended in: anything the
       player is touching resets the ramp, and only a player touching nothing
       pays the drag. */
    if (player->is_grounded || player->is_wall_running ||
        player->is_wall_climbing) {
        player->air_time = 0.0f;
        /* Anything at all refreshes the air jump - ground, a wall run, a wall
           climb. The condition above is already the game's definition of
           touching something, so the two live together and cannot drift. */
        player->air_jumps_used = 0;
    } else {
        player->air_time += in.delta_time;
        player_locomotion_apply_air_drag(player, &in);
    }

    update_pose(player, &in);
}
