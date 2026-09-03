#include "gameplay/player_internal.h"
#include "gameplay/movement_tuning.h"
#include "gameplay/traversal.h"
#include "utils/math_utils.h"

#include <math.h>

/* The grapple is a rope, not a winch: it goes taut at a fixed length, cancels
   any motion pulling away from the anchor, and leaves the tangential motion
   alone. That leftover tangential velocity is the swing. */

void player_grapple_update(PlayerController* player,
                           const PlayerFrameInput* in) {
    /* E attaches a physical pull to the first wall under the camera aim. */
    if (in->grapple_pressed) {
        float aim_x, aim_y, aim_z;
        traversal_aim_direction(in->camera_yaw_degrees, in->camera_pitch_degrees,
                                &aim_x, &aim_y, &aim_z);
        TraversalHit grapple_hit;
        if (traversal_raycast_walls(player->x,
                                    player->y + GRAPPLE_ANCHOR_HEIGHT, player->z,
                                    aim_x, aim_y, aim_z, MOVE_GRAPPLE_RANGE,
                                    &grapple_hit)) {
            player->grapple_x = grapple_hit.x;
            player->grapple_y = grapple_hit.y;
            player->grapple_z = grapple_hit.z;
            /* The rope starts at whatever length the shot reached. */
            float reach_x = grapple_hit.x - player->x;
            float reach_y = grapple_hit.y - (player->y + GRAPPLE_ANCHOR_HEIGHT);
            float reach_z = grapple_hit.z - player->z;
            player->grapple_length = fmaxf(MOVE_GRAPPLE_MIN_LENGTH,
                sqrtf(reach_x * reach_x + reach_y * reach_y + reach_z * reach_z));
            player->is_grappling = 1;
        }
    }
    if (!in->grapple_held) player->is_grappling = 0;
    if (player->is_grappling) {
        float gx = player->grapple_x - player->x;
        float gy = player->grapple_y - (player->y + GRAPPLE_ANCHOR_HEIGHT);
        float gz = player->grapple_z - player->z;
        float grapple_distance = sqrtf(gx * gx + gy * gy + gz * gz);
        if (grapple_distance < 1.0f) {
            player->is_grappling = 0;
        } else {
            gx /= grapple_distance; gy /= grapple_distance; gz /= grapple_distance;

            /* Holding the key reels the rope in, which is what climbs. */
            player->grapple_length = fmaxf(MOVE_GRAPPLE_MIN_LENGTH,
                player->grapple_length - MOVE_GRAPPLE_REEL * in->delta_time);

            player->velocity_x += gx * MOVE_GRAPPLE_PULL * in->delta_time;
            player->velocity_z += gz * MOVE_GRAPPLE_PULL * in->delta_time;
            player->vertical_velocity += gy * MOVE_GRAPPLE_PULL * in->delta_time;

            /* A taut rope cannot stretch: cancel the motion pulling away from
               the anchor and leave the tangential motion alone. That leftover
               tangential velocity is the swing. */
            if (grapple_distance > player->grapple_length) {
                float radial = player->velocity_x * gx +
                               player->vertical_velocity * gy +
                               player->velocity_z * gz;
                if (radial < 0.0f) {
                    player->velocity_x -= gx * radial;
                    player->vertical_velocity -= gy * radial;
                    player->velocity_z -= gz * radial;
                }
                /* Gravity is integrated after this block, so a spring always
                   trails a frame behind and the rope visibly stretches. Put the
                   player back on the rope's sphere directly instead. */
                float overshoot = fminf(grapple_distance - player->grapple_length,
                                        MOVE_GRAPPLE_MAX_CORRECTION);
                player->x += gx * overshoot;
                player->y += gy * overshoot;
                player->z += gz * overshoot;
                /* The rope must never drag the player through the floor. */
                if (player->y < 0.0f) player->y = 0.0f;
            }

            float grapple_speed = sqrtf(player->velocity_x * player->velocity_x +
                                        player->velocity_z * player->velocity_z);
            if (grapple_speed > MOVE_GRAPPLE_MAX_SPEED) {
                player->velocity_x *= MOVE_GRAPPLE_MAX_SPEED / grapple_speed;
                player->velocity_z *= MOVE_GRAPPLE_MAX_SPEED / grapple_speed;
            }
            if (player->is_grounded && gy > 0.05f) player->is_grounded = 0;
        }
    }
}
