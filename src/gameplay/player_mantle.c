#include "gameplay/player_internal.h"
#include "gameplay/arena.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>
#include <stddef.h>

/* Mantling: a scripted climb that owns the player's position for its duration
   and hands them back on top of the ledge with their momentum restored. */

/* Finds a ledge the player is airborne against and moving into, whose top is
   within reach. Reports where the mantle should deposit them. */
static int find_mantle_ledge(const PlayerController* player,
                             float wish_x, float wish_z,
                             float* target_x, float* target_y, float* target_z,
                             float* grip_x, float* grip_z) {
    float reach = PLAYER_RADIUS + MOVE_MANTLE_REACH;
    float best_distance_squared = reach * reach;
    int found = 0;

    for (int i = 0; i < ARENA_WALL_COUNT; ++i) {
        const ArenaWall* wall = &g_arena_walls[i];
        float rise = wall->height - player->y;
        if (rise < MOVE_MANTLE_MIN_RISE || rise > MOVE_MANTLE_MAX_RISE) continue;

        float half_width = wall->width * 0.5f;
        float half_depth = wall->depth * 0.5f;
        float nearest_x = clampf(player->x, wall->x - half_width,
                                wall->x + half_width);
        float nearest_z = clampf(player->z, wall->z - half_depth,
                                wall->z + half_depth);
        float dx = player->x - nearest_x;
        float dz = player->z - nearest_z;
        float distance_squared = dx * dx + dz * dz;
        if (distance_squared > best_distance_squared) continue;
        if (distance_squared < 0.000001f) continue;

        float distance = sqrtf(distance_squared);
        float normal_x = dx / distance;
        float normal_z = dz / distance;
        /* Only mantle a ledge the player is pushing into. */
        if (wish_x * -normal_x + wish_z * -normal_z < MOVE_MANTLE_APPROACH)
            continue;

        float inset = distance + PLAYER_RADIUS + MOVE_MANTLE_INSET;
        float landing_x = player->x - normal_x * inset;
        float landing_z = player->z - normal_z * inset;
        /* Reject a landing spot that a taller box already occupies. */
        float occupied = arena_support_height(landing_x, landing_z,
                                              PLAYER_RADIUS, 1.0e9f);
        if (occupied > wall->height + 0.05f) continue;

        *target_x = landing_x;
        *target_y = wall->height;
        *target_z = landing_z;
        /* The lip itself, as opposed to where a mantle would put the player
           down: the nearest point on the ledge's top edge, which is what a
           hanging character's hand has to be on. */
        if (grip_x) *grip_x = nearest_x;
        if (grip_z) *grip_z = nearest_z;
        best_distance_squared = distance_squared;
        found = 1;
    }
    return found;
}

/* A mantle is a scripted climb: it owns position for its duration, then hands
   the player back onto the ledge with their momentum restored. */
void player_mantle_advance(PlayerController* player,
                           const PlayerFrameInput* in) {
    player->mantle_time += in->delta_time;
    float t = clampf(player->mantle_time / MOVE_MANTLE_DURATION, 0.0f, 1.0f);
    float rise = t * t * (3.0f - 2.0f * t);
    float step = clampf((t - 0.3f) / 0.7f, 0.0f, 1.0f);
    step = step * step * (3.0f - 2.0f * step);

    player->y = player->mantle_start_y +
                (player->mantle_target_y - player->mantle_start_y) * rise;
    player->x = player->mantle_start_x +
                (player->mantle_target_x - player->mantle_start_x) * step;
    player->z = player->mantle_start_z +
                (player->mantle_target_z - player->mantle_start_z) * step;

    /* A jump pressed mid-climb fires the moment the ledge is cleared. */
    if (in->jump_pressed)
        player->jump_buffer_time = MOVE_JUMP_BUFFER_TIME;
    else if (player->jump_buffer_time > 0.0f)
        player->jump_buffer_time = fmaxf(0.0f,
                                         player->jump_buffer_time - in->delta_time);

    /* A mantle is surface contact for its whole duration. */
    player->air_time = 0.0f;
    player->air_jumps_used = 0;
    /* Velocity really is zero for the duration - the path owns position - so
       is_moving stays false. What the player is *asking* for is not zero, and
       reporting that they had let go snapped the stride and the viewmodel to
       idle and back on every ledge. */
    player->is_moving = 0;
    player->has_movement_input = in->has_move_input;
    player->is_sprinting = 0;
    player->is_wall_running = 0;
    player->wall_run_side = 0;
    player->is_sliding = 0;
    player->is_crouching = 0;
    player->is_grappling = 0;

    if (t < 1.0f) return;

    /* The exit runs off the ledge the player grabbed, bent toward wherever they
       are actually asking to go. Spitting them out square to the wall every
       time means a mantle is always followed by a correction, which is the
       opposite of the flow it is there to preserve. */
    float exit_x = player->mantle_target_x - player->mantle_start_x;
    float exit_z = player->mantle_target_z - player->mantle_start_z;
    float exit_length = sqrtf(exit_x * exit_x + exit_z * exit_z);
    if (exit_length > 0.0001f) {
        exit_x /= exit_length;
        exit_z /= exit_length;
        if (in->has_move_input) {
            exit_x += (in->wish_x - exit_x) * MOVE_MANTLE_EXIT_STEER;
            exit_z += (in->wish_z - exit_z) * MOVE_MANTLE_EXIT_STEER;
            float steered = sqrtf(exit_x * exit_x + exit_z * exit_z);
            if (steered > 0.0001f) { exit_x /= steered; exit_z /= steered; }
        }
        player->velocity_x = exit_x * player->mantle_exit_speed;
        player->velocity_z = exit_z * player->mantle_exit_speed;
    }
    player->is_mantling = 0;
    player->is_grounded = 1;
    player->vertical_velocity = 0.0f;
    player->coyote_time = MOVE_COYOTE_TIME;
    player->wall_run_time = 0.0f;
    player->wall_run_cooldown = 0.0f;
    player->wall_run_wall_index = -1;
}

static void begin_mantle(PlayerController* player, float target_x,
                         float target_y, float target_z) {
    /* A mantle spends everything the player arrived with. It is the one verb
       here that does, and it is deliberate: pulling over a ledge is the move
       you make when the route has already gone wrong, not a way through one.
       Carrying the entry speed made it the cheapest height in the game - 0.22s
       for up to 2.3m at no cost, against a climb that spends its whole
       into-wall component and a jump that pays the drag ramp - so the fast
       line through the arena ran over ledges instead of off walls.

       The exit below is whatever MOVE_MANTLE_EXIT_SPEED says and nothing the
       player brought with them; at its current 0 a mantle ends standing still
       on the ledge it grabbed. */
    player->mantle_exit_speed = MOVE_MANTLE_EXIT_SPEED;
    player->mantle_start_x = player->x;
    player->mantle_start_y = player->y;
    player->mantle_start_z = player->z;
    player->mantle_target_x = target_x;
    player->mantle_target_y = target_y;
    player->mantle_target_z = target_z;
    player->mantle_time = 0.0f;
    player->is_mantling = 1;
    player->air_time = 0.0f;
    player->air_jumps_used = 0;
    player->jump_buffer_time = 0.0f;
    player->velocity_x = 0.0f;
    player->velocity_z = 0.0f;
    player->vertical_velocity = 0.0f;
    player->is_grappling = 0;
    player->is_wall_running = 0;
    player->wall_run_side = 0;

    float heading_x = target_x - player->x;
    float heading_z = target_z - player->z;
    if (heading_x * heading_x + heading_z * heading_z > 0.0001f)
        player->heading_degrees = atan2f(heading_x, heading_z) / MATH_DEG_TO_RAD;
}

/* Whether a mantle would start right now if the press were there. The air jump
   needs to know, because the two compete for the same buffered press and the
   jump is resolved first: without this a player with the kit in hand could
   never mantle at all - the second jump would eat every press before the ledge
   test ever ran. A ledge outranks the kit, which is the Titanfall order and
   also the useful one, since the kit is for open air and the ledge is not. */
int player_mantle_available(const PlayerController* player,
                            const PlayerFrameInput* in) {
    float ledge_x, ledge_y, ledge_z;
    return !player->is_grounded && in->has_move_input &&
           find_mantle_ledge(player, in->wish_x, in->wish_z,
                             &ledge_x, &ledge_y, &ledge_z, NULL, NULL);
}

/* Hanging is held, never toggled and never entered on the player's behalf: the
   same push into the ledge that would mantle it holds them on it instead, and
   letting go of that push - or looking away, since the wish direction is camera
   relative - drops them the same frame. Nothing latches, so there is no state
   to get stuck in and no press to spend.

   It resolves after player_mantle_try_begin, which is what makes jump the climb
   out: with a buffered press the mantle takes the frame and this never runs.

   Position is frozen where the player reached the ledge rather than snapped to
   a pose. A pose would look tidier and would also be a teleport, which is the
   one thing a move that takes control away must not do.

   Contact for every purpose that cares: the air jump comes back, the air ramps
   reset, and the wall verbs are skipped because this owns the frame. */
int player_ledge_hang_update(PlayerController* player,
                             const PlayerFrameInput* in) {
    float ledge_x, ledge_y, ledge_z, grip_x = 0.0f, grip_z = 0.0f;
    if (player->is_grounded || player->is_mantling ||
        !in->has_move_input ||
        !find_mantle_ledge(player, in->wish_x, in->wish_z,
                           &ledge_x, &ledge_y, &ledge_z, &grip_x, &grip_z)) {
        player->is_ledge_hanging = 0;
        return 0;
    }

    player->is_ledge_hanging = 1;
    player->mantle.grip_x = grip_x;
    player->mantle.grip_y = ledge_y;
    player->mantle.grip_z = grip_z;
    player->velocity_x = 0.0f;
    player->velocity_z = 0.0f;
    player->vertical_velocity = 0.0f;
    player->air_time = 0.0f;
    player->air_jumps_used = 0;
    player->is_moving = 0;
    player->has_movement_input = in->has_move_input;
    player->is_sprinting = 0;
    player->is_wall_running = 0;
    player->wall_run_side = 0;
    player->is_wall_climbing = 0;
    player->is_sliding = 0;
    player->is_crouching = 0;
    player->is_grappling = 0;
    /* Face the ledge, so the character reads as holding it rather than as
       standing in the air pointing wherever they last travelled. */
    player->heading_degrees = atan2f(ledge_x - player->x,
                                     ledge_z - player->z) / MATH_DEG_TO_RAD;
    return 1;
}

int player_mantle_try_begin(PlayerController* player,
                            const PlayerFrameInput* in) {
    /* Mantling is asked for, not inferred. It used to fire on nothing but being
       airborne and holding a direction into a ledge, which meant it was not a
       move the player made - it was a move that happened to them whenever they
       drifted near geometry, repeatedly, without a button ever being touched.
       A verb that scripts the player's position for 0.22s has to be one they
       chose.

       The buffered press rather than the raw one, so a jump asked for slightly
       early still pulls the player over the ledge they were aiming at - the
       same forgiveness every other jump in this file gets. begin_mantle spends
       it, so one press is one mantle and holding the key does nothing.

       player_locomotion_apply_jump has already run this frame and clears the
       buffer when it fires, so a ground jump or a wall jump outranks a mantle
       and the same press is never spent twice. That also means a ledge taken
       from the ground costs two presses - one to leave the ground, one to pull
       up - which is the cost of the move being voluntary.

       It does not steal a wall run: the ledge test needs the player pushing
       into the surface by MOVE_MANTLE_APPROACH, and a wall run's wish direction
       runs along the wall rather than at it. It does catch the top of a wall
       climb, which is exactly where a mantle belongs. */
    if (!player->is_grounded && in->has_move_input &&
        player->jump_buffer_time > 0.0f) {
        float ledge_x, ledge_y, ledge_z;
        if (find_mantle_ledge(player, in->wish_x, in->wish_z,
                              &ledge_x, &ledge_y, &ledge_z, NULL, NULL)) {
            begin_mantle(player, ledge_x, ledge_y, ledge_z);
            return 1;
        }
    }
    return 0;
}
