#include "gameplay/player_internal.h"
#include "gameplay/arena.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>

/* Wall climbing: running head-on at a wall runs *up* it. This is the case the
   wall run deliberately rejects - the tangent gate in player_wallrun.c throws
   out an approach that is too square to the surface, and everything it throws
   out lands here.

   The two mechanics split cleanly on approach angle: inside roughly 35 degrees
   of head-on is a climb, anything shallower is a run. A climb is an explicit
   trade rather than free height - it spends the horizontal speed the player
   arrived with to buy vertical speed, so a fast approach climbs higher and a
   walk barely leaves the ground. Height is capped by time, and a wall can only
   be climbed once per airtime, so a climb is a route and not an elevator. */

/* Finds the wall the player is pressed against and moving squarely into.
   Reports its outward normal and how head-on the approach is. */
static int find_climbable_wall(const PlayerController* player,
                               float wish_x, float wish_z,
                               float* normal_x, float* normal_z,
                               float* approach, int* wall_index) {
    float reach = PLAYER_RADIUS + WALL_CONTACT_DISTANCE;
    float best_distance_squared = reach * reach;
    int found = 0;

    for (int i = 0; i < ARENA_WALL_COUNT; ++i) {
        const ArenaWall* wall = &g_arena_walls[i];
        /* Nothing to climb below the wall-run floor, and nothing to climb on a
           box the player has already cleared - that is a mantle. */
        if (player->y < MOVE_WALL_MIN_HEIGHT) continue;
        if (player->y >= wall->height - 0.2f) continue;

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
        /* A corner the player is standing exactly inside has no usable normal. */
        if (distance_squared < 0.000001f) continue;

        float distance = sqrtf(distance_squared);
        float candidate_x = dx / distance;
        float candidate_z = dz / distance;
        /* How squarely the player is pushing into the surface: 1 is dead-on. */
        float into_wall = wish_x * -candidate_x + wish_z * -candidate_z;
        if (into_wall < MOVE_WALL_CLIMB_APPROACH) continue;

        *normal_x = candidate_x;
        *normal_z = candidate_z;
        *approach = into_wall;
        *wall_index = i;
        best_distance_squared = distance_squared;
        found = 1;
    }
    return found;
}

/* Speed aimed at the wall is what buys the climb. Speed running *along* the
   wall is momentum the player already had and never asked to spend, so it is
   kept - only the into-wall component is converted. A faster arrival climbs
   faster, between a floor that keeps a slow climb worth doing and a ceiling
   that stops a grapple-speed arrival from launching over everything.

   The ceiling bounds the climb *rate*, not the price. Charging the whole
   into-wall component for a rate that stopped rising is what made a fast
   approach come out slower than a slow one, so only the speed the climb
   actually converts is taken and the surplus is handed back. */
static void begin_climb(PlayerController* player, float into_wall_speed,
                        float normal_x, float normal_z, int wall_index) {
    float climb_speed = clampf(into_wall_speed * MOVE_WALL_CLIMB_CONVERSION,
                               MOVE_WALL_CLIMB_MIN_SPEED,
                               MOVE_WALL_CLIMB_MAX_SPEED);
    /* What that climb rate costs at the conversion rate, never more than the
       player brought: a slow arrival whose rate was clamped *up* to the floor
       still only pays what it had, and gets the floor for free. */
    float spent = fminf(into_wall_speed,
                        climb_speed / MOVE_WALL_CLIMB_CONVERSION);
    float surplus = into_wall_speed - spent;

    player->is_wall_climbing = 1;
    player->wall_climb_time = 0.0f;
    player->wall_climb_wall_index = wall_index;
    player->wall_normal_x = normal_x;
    player->wall_normal_z = normal_z;

    /* Keep whatever was running along the wall, and replace the whole
       into-wall component with a gentle hold so contact is not lost. All of it
       goes, not just the spent part: what the climb did not buy is redirected
       below, and leaving it here as well would pay it out twice - once as
       height and again as speed still driving into the surface. */
    player->velocity_x += normal_x * into_wall_speed;
    player->velocity_z += normal_z * into_wall_speed;
    player->velocity_x -= normal_x * MOVE_WALL_CLIMB_HOLD;
    player->velocity_z -= normal_z * MOVE_WALL_CLIMB_HOLD;

    /* The surplus goes back along the wall, the only direction left that is
       neither into it nor already spent. Square-on there is no such direction,
       so it lifts the climb instead, faded between the two by how much
       along-wall motion there actually is. */
    if (surplus > 0.0f) {
        float into = player->velocity_x * normal_x +
                     player->velocity_z * normal_z;
        float along_x = player->velocity_x - normal_x * into;
        float along_z = player->velocity_z - normal_z * into;
        float along_speed = sqrtf(along_x * along_x + along_z * along_z);
        float lateral = clampf(along_speed / MOVE_WALL_CLIMB_SURPLUS_ALONG,
                               0.0f, 1.0f);
        if (lateral > 0.0f) {
            player->velocity_x += along_x / along_speed * surplus * lateral;
            player->velocity_z += along_z / along_speed * surplus * lateral;
        }
        climb_speed += surplus * (1.0f - lateral);
    }

    player->wall_climb_speed = climb_speed;
    player->vertical_velocity = climb_speed;

    /* A climb ends a wall run and vice versa; they are the same contact. */
    player->is_wall_running = 0;
    player->wall_run_side = 0;
    player->wall_run_time = 0.0f;
    player->wall_run_wall_index = -1;
}

/* Hands the player back to normal air movement, nudged off the surface so they
   are not left scraping it with no speed. */
static void end_climb(PlayerController* player) {
    player->is_wall_climbing = 0;
    player->wall_climb_time = 0.0f;
    player->velocity_x += player->wall_normal_x * MOVE_WALL_CLIMB_EXIT_PUSH;
    player->velocity_z += player->wall_normal_z * MOVE_WALL_CLIMB_EXIT_PUSH;
}

void player_wallclimb_update(PlayerController* player,
                             const PlayerFrameInput* in) {
    if (player->is_grounded) {
        /* Touching the ground is what re-arms every wall in the arena. */
        player->wall_climb_wall_index = -1;
        if (player->is_wall_climbing) end_climb(player);
        return;
    }

    float normal_x = 0.0f;
    float normal_z = 0.0f;
    float approach = 0.0f;
    int wall_index = -1;
    int has_wall = in->has_move_input &&
                   find_climbable_wall(player, in->wish_x, in->wish_z,
                                       &normal_x, &normal_z, &approach,
                                       &wall_index);

    if (player->is_wall_climbing) {
        /* Letting go of the wall, turning away from it, or running out of time
           all end the climb the same way. */
        if (!has_wall || wall_index != player->wall_climb_wall_index ||
            player->wall_climb_time >= MOVE_WALL_CLIMB_TIME) {
            /* No gravity here: the wall run runs straight after this and
               either catches the player or applies it in its own else branch.
               Doing it here as well would double it for one frame. */
            end_climb(player);
            return;
        }

        player->wall_climb_time += in->delta_time;
        player->wall_normal_x = normal_x;
        player->wall_normal_z = normal_z;

        /* The climb eases out rather than stopping dead, so the top of it flows
           into the apex of a jump instead of dropping the player off a step. */
        float progress = clampf(player->wall_climb_time / MOVE_WALL_CLIMB_TIME,
                                0.0f, 1.0f);
        /* The rate was bought at entry and is held for the whole climb; it is
           not re-derived from the horizontal speed the entry already spent. */
        player->vertical_velocity =
            player->wall_climb_speed * (1.0f - smoothstepf(progress));
        return;
    }

    if (!has_wall) return;
    if (wall_index == player->wall_climb_wall_index) return;

    /* The gate is on how fast the player is actually going *at* the wall, not
       on total speed, so skimming past one at speed never starts a climb. */
    float into_wall_speed = -(player->velocity_x * normal_x +
                              player->velocity_z * normal_z);
    if (into_wall_speed < MOVE_WALL_CLIMB_ENTRY_SPEED) return;

    begin_climb(player, into_wall_speed, normal_x, normal_z, wall_index);
}
