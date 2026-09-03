#include "gameplay/player_internal.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>

/* Sliding: entry burst, steering off the captured slide line, and the friction
   that ends it. */

/* The cap is a ceiling on what the burst may *add*, never a speed limit on the
   player. Arriving already faster than it simply means the burst pays nothing -
   entering a slide must never be the reason speed goes down, or the fast
   landing this exists to reward would be slower than a slow one. */
void player_slide_apply_burst(PlayerController* player) {
    float speed = sqrtf(player->velocity_x * player->velocity_x +
                        player->velocity_z * player->velocity_z);
    if (speed < 0.001f) return;
    float boosted_speed = fmaxf(speed, fminf(speed + MOVE_SLIDE_BURST,
                                             MOVE_SLIDE_MAX_SPEED));
    player->velocity_x *= boosted_speed / speed;
    player->velocity_z *= boosted_speed / speed;
}

/* Every way into a slide goes through here. There are two - a crouch pressed
   while running, and a landing with crouch already held - and when they were
   written out separately the landing one set the state but never captured the
   line. It inherited whatever slide_forward_x/z happened to hold: on the first
   slide of a session the (0,-1) left by player_controller_init, and after that
   the heading of the player's previous slide. The steering below then spent a
   second or so hauling the player round onto a direction they had chosen
   several slides ago, which read as the slide curving away on its own.

   The capture is the whole reason this exists as a function rather than three
   assignments at each call site. Anything that starts a slide calls this. */
void player_slide_begin(PlayerController* player) {
    float horizontal_speed = sqrtf(player->velocity_x * player->velocity_x +
                                   player->velocity_z * player->velocity_z);
    /* No speed means no line to hold. The callers both gate on a speed well
       above this, so it is a guard against dividing by zero and nothing else. */
    if (horizontal_speed > 0.001f) {
        player->slide_forward_x = player->velocity_x / horizontal_speed;
        player->slide_forward_z = player->velocity_z / horizontal_speed;
    }
    player->is_sliding = 1;
    player->slide_time = 0.0f;
    player_slide_apply_burst(player);
}

/* Crouch is a ground verb. Airborne the key does nothing at all - no slide, no
   crouch pose, no view change - so a player cannot crouch-spam mid-flight.
   Landing into a slide still works, but it is owned by the touchdown branch in
   player_locomotion_resolve_vertical, which runs at the instant the player is
   grounded rather than while they are still in the air. */
void player_slide_try_enter(PlayerController* player,
                            const PlayerFrameInput* in) {
    if (!player->is_grounded || player->is_sliding) return;
    if (!in->crouch_pressed || player->slide_reentry_time > 0.0f) return;

    float horizontal_speed = sqrtf(player->velocity_x * player->velocity_x +
                                   player->velocity_z * player->velocity_z);
    if (horizontal_speed < MOVE_SLIDE_MIN_SPEED) return;

    player_slide_begin(player);
}

void player_slide_update(PlayerController* player,
                         const PlayerFrameInput* in) {
    float horizontal_speed = sqrtf(player->velocity_x * player->velocity_x +
                                   player->velocity_z * player->velocity_z);
    player->slide_time += in->delta_time;
    if (in->slide_backward) {
        horizontal_speed = fmaxf(0.0f, horizontal_speed -
                                 MOVE_SLIDE_BRAKE * in->delta_time);
    } else {
        /* Holding forward buys a longer line, not a faster one. There is no
           slope in this arena for gravity to pay an accelerating slide with, so
           a slide that gains speed on the flat is just a better sprint that
           costs nothing - and it used to be exactly that, running until the
           player hit something. Both cases decay; forward decays slower. */
        float friction = in->slide_forward ? MOVE_SLIDE_FORWARD_FRICTION
                                           : MOVE_SLIDE_FRICTION;
        horizontal_speed = fmaxf(0.0f, horizontal_speed -
                                 friction * in->delta_time);
    }
    float old_speed = sqrtf(player->velocity_x * player->velocity_x +
                            player->velocity_z * player->velocity_z);
    if (old_speed > 0.001f) {
        player->velocity_x *= horizontal_speed / old_speed;
        player->velocity_z *= horizontal_speed / old_speed;

        /* A/D temporarily bend away from the captured forward slide line, and
           the direction of view bends it a little further. Looking used to do
           nothing at all without a strafe key down - the camera term fell out
           of the target entirely at lateral 0, so a player could sweep the mouse
           through ninety degrees mid-slide and travel in exactly the same
           direction. A slide you cannot aim is a slide you can only commit to.

           Deliberately small. It is added to a unit-length captured line, so it
           is bounded: the target can never sit further than atan(this) off the
           line however far the player turns, which is about fourteen degrees.
           That is enough to place a slide through a doorway and not enough to
           turn one into a walk with extra steps. */
        int lateral = in->slide_right - in->slide_left;
        if (!in->slide_backward && horizontal_speed > 0.001f) {
            float yaw = in->camera_yaw_degrees * MATH_DEG_TO_RAD;
            float look_x = -sinf(yaw);
            float look_z = -cosf(yaw);
            float target_x = player->slide_forward_x +
                             look_x * MOVE_SLIDE_LOOK_STEER +
                             cosf(yaw) * (float)lateral * .65f;
            float target_z = player->slide_forward_z +
                             look_z * MOVE_SLIDE_LOOK_STEER -
                             sinf(yaw) * (float)lateral * .65f;
            float target_length = sqrtf(target_x * target_x + target_z * target_z);
            target_x /= target_length;
            target_z /= target_length;

            float current_x = player->velocity_x / horizontal_speed;
            float current_z = player->velocity_z / horizontal_speed;
            float steering = fminf(1.0f, MOVE_SLIDE_STEERING * in->delta_time);
            current_x += (target_x - current_x) * steering;
            current_z += (target_z - current_z) * steering;
            float current_length = sqrtf(current_x * current_x + current_z * current_z);
            player->velocity_x = current_x / current_length * horizontal_speed;
            player->velocity_z = current_z / current_length * horizontal_speed;
        }
    }
    if (!in->crouch_held || horizontal_speed < MOVE_CROUCH_SPEED)
        player->is_sliding = 0;
}

void player_slide_resolve_state(PlayerController* player,
                                const PlayerFrameInput* in) {
    /* Leaving the ground drops both states outright: crouch held through a
       jump neither keeps the pose nor resumes on its own. */
    if (!player->is_grounded) {
        player->is_sliding = 0;
        player->is_crouching = 0;
        return;
    }
    float horizontal_speed = sqrtf(player->velocity_x * player->velocity_x +
                             player->velocity_z * player->velocity_z);
    if (!in->crouch_held || horizontal_speed < MOVE_CROUCH_SPEED)
        player->is_sliding = 0;
    player->is_crouching = in->crouch_held || player->is_sliding;
}
