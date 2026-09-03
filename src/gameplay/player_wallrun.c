#include "gameplay/player_internal.h"
#include "gameplay/arena.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>

/* Wall running: finding a runnable surface, holding the player against it, and
   letting go. Wall jumping itself lives with the other jumps in
   player_locomotion.c because it shares the jump buffer. */

/* Returns the outward normal of the nearest wall touching the player. */
static int find_wall_contact(const PlayerController* player,
                             float* normal_x, float* normal_z, int* wall_index) {
    float best_distance_squared =
        (PLAYER_RADIUS + WALL_CONTACT_DISTANCE) *
        (PLAYER_RADIUS + WALL_CONTACT_DISTANCE);
    int found = 0;

    for (int i = 0; i < ARENA_WALL_COUNT; ++i) {
        const ArenaWall* wall = &g_arena_walls[i];
        if (player->y < MOVE_WALL_MIN_HEIGHT || player->y >= wall->height - 0.2f)
            continue;

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

        if (distance_squared > 0.000001f) {
            float inverse_distance = 1.0f / sqrtf(distance_squared);
            *normal_x = dx * inverse_distance;
            *normal_z = dz * inverse_distance;
        } else {
            float edge_x = half_width - fabsf(player->x - wall->x);
            float edge_z = half_depth - fabsf(player->z - wall->z);
            if (edge_x < edge_z) {
                *normal_x = player->x < wall->x ? -1.0f : 1.0f;
                *normal_z = 0.0f;
            } else {
                *normal_x = 0.0f;
                *normal_z = player->z < wall->z ? -1.0f : 1.0f;
            }
        }
        best_distance_squared = distance_squared;
        *wall_index = i;
        found = 1;
    }
    return found;
}

void player_wallrun_update(PlayerController* player,
                           const PlayerFrameInput* in) {
    /* A climb outranks a run and owns vertical velocity for its duration, so
       there is nothing to do here and gravity must not be applied twice. */
    if (player->is_wall_climbing) {
        player->is_wall_running = 0;
        player->wall_run_side = 0;
        return;
    }

    float horizontal_speed;
    float contact_x = 0.0f;
    float contact_z = 0.0f;
    int contact_wall_index = -1;
    int has_wall = find_wall_contact(player, &contact_x, &contact_z,
                                     &contact_wall_index);
    float run_x = in->has_move_input ? in->wish_x : player->velocity_x;
    float run_z = in->has_move_input ? in->wish_z : player->velocity_z;
    float tangent_x = run_x - contact_x *
                      (run_x * contact_x + run_z * contact_z);
    float tangent_z = run_z - contact_z *
                      (run_x * contact_x + run_z * contact_z);
    float tangent_length = sqrtf(tangent_x * tangent_x + tangent_z * tangent_z);
    /* The speed already running along the wall, as opposed to into it. Needed
       by the gate below as well as by the run itself, so it is measured here
       rather than inside the branch that uses it. */
    float along_wall_speed = tangent_length >= 0.0001f
        ? (player->velocity_x * tangent_x + player->velocity_z * tangent_z) /
          tangent_length
        : 0.0f;
    int attaching_now = !player->is_wall_running ||
                        contact_wall_index != player->wall_run_wall_index;
    /* How hard the player is asking to leave. Positive is away from the wall. */
    float pushing_away = in->has_move_input
        ? in->wish_x * contact_x + in->wish_z * contact_z : 0.0f;
    int let_go = pushing_away >= MOVE_WALL_DETACH;
    /* Input is required to *start* a run, so a player who merely brushes a wall
       in passing is not put on it. Once the run is going it is not required at
       all: making the player hold a key to stay on a wall means letting go to
       do anything else drops them, and the run already has a timer, a detach
       rule and a tangent gate deciding when it ends. The tangent falls back to
       the direction of travel, so a hands-off run keeps going the way it was
       already going.

       move_keys_held covers the frame whose keys cancel each other out and so
       carry no direction: the player has plainly not let go, and every strafe
       reversal passes through exactly that frame. Requiring a resolved
       direction there used to drop the run outright. */
    int asking_to_move = in->has_move_input || in->move_keys_held ||
                         player->is_wall_running;
    int can_wall_run = !player->is_grounded && asking_to_move &&
                       !player->is_sliding && !let_go &&
                       has_wall && tangent_length >= MOVE_WALL_TANGENT_MIN &&
                       (player->wall_run_cooldown <= 0.0f ||
                        contact_wall_index != player->wall_run_wall_index) &&
                       (!attaching_now ||
                        along_wall_speed >= MOVE_WALL_ATTACH_ALONG_MIN);

    if (can_wall_run && (player->wall_run_time < MOVE_WALL_MAX_TIME ||
                         contact_wall_index != player->wall_run_wall_index)) {
        tangent_x /= tangent_length;
        tangent_z /= tangent_length;
        int chained_wall = player->wall_run_wall_index >= 0 &&
                           contact_wall_index != player->wall_run_wall_index;
        int attaching = !player->is_wall_running || chained_wall;
        player->is_wall_running = 1;
        if (chained_wall) {
            player->wall_run_time *= 0.35f;
            player->velocity_x += tangent_x * MOVE_WALL_CHAIN_BONUS;
            player->velocity_z += tangent_z * MOVE_WALL_CHAIN_BONUS;
        }
        player->wall_run_wall_index = contact_wall_index;
        player->wall_normal_x = contact_x;
        player->wall_normal_z = contact_z;

        /* Attaching normalizes vertical speed so the arc reads the same whether
           the player arrived off a jump, a fall, or another wall. Because a
           chained wall re-attaches, chaining buys a fresh climb - that, plus
           the rewound timer, is what makes wall-to-wall gain height. */
        if (attaching)
            player->vertical_velocity = clampf(player->vertical_velocity,
                                               -MOVE_WALL_ATTACH_SINK,
                                               MOVE_WALL_ATTACH_RISE);

        player->wall_run_time += in->delta_time;
        /* Wall gravity ramps in over the run instead of being constant, so the
           player rises while it is weak, apexes about two thirds of the way
           through, and is already sinking when the timer lets go of them. A
           constant pull either never turns the climb around or kills it on
           contact; the ramp is what makes the run an arc. */
        float run_progress = clampf(player->wall_run_time / MOVE_WALL_MAX_TIME,
                                    0.0f, 1.0f);
        float wall_gravity = lerpf(MOVE_WALL_GRAVITY_START,
                                   MOVE_WALL_GRAVITY_END, run_progress);
        player->vertical_velocity -= wall_gravity * in->delta_time;
        if (player->vertical_velocity < -MOVE_WALL_SINK_MAX)
            player->vertical_velocity = -MOVE_WALL_SINK_MAX;
        /* A wall run is an accelerator, not a floor. It used to ramp only
           while below its minimum, which put the target barely above a ground
           sprint and meant anyone arriving with real speed got exactly nothing
           from the surface - the wall held you up and charged you the airtime.
           Now the run drives toward a speed above anything the ground can give,
           so riding a wall is a way to go faster and not merely a way not to
           fall. Never downward: arriving above the target keeps every bit of
           what was brought. */
        horizontal_speed = sqrtf(player->velocity_x * player->velocity_x +
                                 player->velocity_z * player->velocity_z);
        float target_run_speed = player->is_sprinting
            ? MOVE_WALL_SPRINT_SPEED : MOVE_WALL_WALK_SPEED;
        /* Only what was already travelling along the wall is carried into the
           run. Taking the whole magnitude instead rotates the into-wall
           component onto the tangent, which does not redirect speed but
           *creates* it: arriving at a corner at 12 m/s with only 5.6 of that
           running along the face came out at a full 12 along it, and the
           surplus read as being flung sideways off geometry the player had
           merely bumped. Every other contact in the codebase sheds that
           component - see clip_velocity, which the run is exempt from because
           it owns velocity outright - and this is the run paying the same
           price rather than being the one place momentum is invented.

           Collision clipping does not already cover this. It runs in
           player_locomotion_integrate, ahead of this, but only inside
           PLAYER_RADIUS - while a run may be taken out to PLAYER_RADIUS +
           WALL_CONTACT_DISTANCE. In that band the player is close enough to
           attach and too far to have been clipped, so the into-wall component
           is still on them when the run claims it, and that band is exactly
           where the corner launch lived.

           Attach only. Once the run is going, velocity is already pure tangent
           and the two are the same number; measuring the projection on a
           continuing frame would instead stop a player dead the moment they
           turned around on the wall, because their own tangent would have
           flipped underneath them. */
        float run_speed = attaching
            ? fmaxf(0.0f, player->velocity_x * tangent_x +
                          player->velocity_z * tangent_z)
            : horizontal_speed;
        if (run_speed < target_run_speed)
            run_speed = fminf(target_run_speed,
                              run_speed + MOVE_WALL_ACCELERATION * in->delta_time);
        player->wall_run_speed = run_speed;
        player->velocity_x = tangent_x * run_speed;
        player->velocity_z = tangent_z * run_speed;
        player->wall_run_side =
            (tangent_x * contact_z - tangent_z * contact_x) < 0.0f ? -1 : 1;
    } else {
        if (player->is_wall_running) {
            /* The re-attach delay is there to stop a player who deliberately
               left a wall, or who used one up, from immediately taking it
               again. A run that ended because contact or input momentarily
               vanished was not a decision, and charging it a quarter second of
               full gravity turned a one-frame flicker into a real fall. Those
               cost the frame they lost and nothing more. */
            int timed_out = has_wall &&
                            contact_wall_index == player->wall_run_wall_index &&
                            player->wall_run_time >= MOVE_WALL_MAX_TIME;
            if (let_go || timed_out)
                player->wall_run_cooldown = MOVE_WALL_REATTACH_DELAY;
            /* Letting go because the player asked to gets a nudge off the
               surface, so pushing away separates cleanly instead of scraping
               along it at arm's length. Timing out gets no such push. */
            if (let_go) {
                player->velocity_x += contact_x * MOVE_WALL_DETACH_PUSH;
                player->velocity_z += contact_z * MOVE_WALL_DETACH_PUSH;
            }
        }
        player->is_wall_running = 0;
        player->wall_run_side = 0;
        if (!player->is_grounded)
            player->vertical_velocity -=
                player_locomotion_air_gravity(player) * in->delta_time;
    }
}
