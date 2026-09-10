#include "gameplay/player_internal.h"
#include "gameplay/arena.h"
#include "gameplay/movement_tuning.h"
#include "utils/math_utils.h"

#include <math.h>

/* Ground and air locomotion: acceleration, friction, jumping, the horizontal
   integration step, and everything that decides what the player is standing
   on. */

/* Quake's accelerator. The addition is made along the wish direction only, and
   the headroom it is allowed is measured by *projecting* current velocity onto
   that direction rather than by taking its magnitude. Two things fall out of
   that, and both of them are the feel:

   Momentum across the wish direction is never touched, so a turn at speed
   carves through the old heading instead of pivoting onto the new one. The
   accelerator has no way to remove the old direction at all - friction does
   that, which is why the two have to be a pair.

   And a player already faster than the target simply gets nothing, without any
   need for a separate overspeed rule: the projection is already past the
   target, so there is no headroom to spend. Steering still works at any speed,
   because a wish direction across the current one always projects short. */
static void accelerate_along(PlayerController* player, float wish_x, float wish_z,
                             float wish_speed, float rate, float delta_time) {
    float along_wish = player->velocity_x * wish_x + player->velocity_z * wish_z;
    float available = wish_speed - along_wish;
    if (available <= 0.0f) return;
    float addition = fminf(available, rate * wish_speed * delta_time);
    player->velocity_x += wish_x * addition;
    player->velocity_z += wish_z * addition;
}

/* Friction is proportional to speed, which makes it an exponential settle
   instead of a brake pedal: the same fraction goes every second whatever the
   player is doing, so coming off a sprint eases down rather than hitting a
   wall and stopping dead. Below the stop speed the charge is levied as if the
   player were at it, which is what keeps a slow walk from crawling forever.

   Charged on every grounded frame, held or not. The accelerator above only
   ever adds, so without this there is nothing to remove a heading the player
   has stopped asking for, and circling on the ground would compound speed for
   free. Being on the ground is the cost; the jump, the slide and every wall
   verb are ways of not paying it. */
static void ground_friction(PlayerController* player, float delta_time) {
    float speed = sqrtf(player->velocity_x * player->velocity_x +
                        player->velocity_z * player->velocity_z);
    if (speed < 0.0001f) return;
    float control = fmaxf(speed, MOVE_GROUND_STOP_SPEED);
    float drop = control * MOVE_GROUND_FRICTION * delta_time;
    float scale = fmaxf(0.0f, speed - drop) / speed;
    player->velocity_x *= scale;
    player->velocity_z *= scale;
}

/* The air-strafe rule: acceleration is allowed only up to MOVE_AIR_WISH_SPEED
   *along the requested direction*, not up to the full ground target. Holding a
   direction straight ahead saturates almost at once and stops helping; turning
   the mouse so the wish direction stays near-perpendicular to velocity keeps a
   sliver of headroom open every frame, and that sliver is what compounds into
   speed. Because a perpendicular addition grows the magnitude by only
   a^2/(2*speed), the gain decays as the player speeds up and the curve settles
   into a soft ceiling rather than a hard cap. */
static void air_strafe(PlayerController* player, float wish_x, float wish_z,
                       float wish_speed, float delta_time) {
    float capped_wish_speed = fminf(wish_speed, MOVE_AIR_WISH_SPEED);
    float along_wish = player->velocity_x * wish_x + player->velocity_z * wish_z;
    float available = capped_wish_speed - along_wish;
    if (available <= 0.0f) return;
    /* The per-frame addition scales as sqrt(dt), not dt. Speed only grows
       because the addition is perpendicular, and a perpendicular add of size a
       grows the magnitude by a^2/(2*speed) - so the gain per second is
       a^2/(2*speed*dt). A linear a = accel*dt makes that proportional to dt,
       which meant a 144Hz player strafing perfectly gained less than half what
       a 60Hz one did and, past the drag ramp, lost speed doing everything
       right. Scaling by sqrt(dt) against a reference tick cancels the dt in
       the denominator exactly, so the curve is identical at any frame rate and
       unchanged at 60. */
    const float reference_delta_time = 1.0f / 60.0f;
    float addition = fminf(available, MOVE_AIR_ACCELERATION *
                           sqrtf(delta_time * reference_delta_time));
    player->velocity_x += wish_x * addition;
    player->velocity_z += wish_z * addition;
}

/* Air strafing is how speed is earned, but it is not how a player aims a jump,
   and a trajectory that cannot be corrected once it has left the ground reads
   as the game taking the controls away. This rotates velocity toward the wish
   direction and rescales it back to the speed it already had, so it buys aim
   and never speed - the strafe above remains the only way to go faster.

   It is scaled by how far the wish direction already points along the way the
   player is travelling, and switched off entirely once it does not. That is
   exactly the case a pure air strafe lives in - wish direction across the
   velocity, projection near zero - so a strafe is left completely untouched
   and the two mechanics do not fight over the same frames. */
static void air_steer(PlayerController* player, float wish_x, float wish_z,
                      float delta_time) {
    float speed = sqrtf(player->velocity_x * player->velocity_x +
                        player->velocity_z * player->velocity_z);
    if (speed < 0.0001f) return;
    float direction_x = player->velocity_x / speed;
    float direction_z = player->velocity_z / speed;
    float alignment = direction_x * wish_x + direction_z * wish_z;
    if (alignment <= 0.0f) return;
    float blend = fminf(1.0f, MOVE_AIR_STEER * alignment * delta_time);
    direction_x += (wish_x - direction_x) * blend;
    direction_z += (wish_z - direction_z) * blend;
    float length = sqrtf(direction_x * direction_x + direction_z * direction_z);
    if (length < 0.0001f) return;
    player->velocity_x = direction_x / length * speed;
    player->velocity_z = direction_z / length * speed;
}

/* Takes the component of velocity driving into a surface and leaves the rest.
   This is the difference between geometry that stops the player and geometry
   that steers them: brushing a wall at an angle now sheds only the part of the
   speed aimed at it and keeps everything running along it, so corners and
   doorways redirect at nearly full speed.

   Without it, pushing the player back out of a box each frame while leaving
   the velocity still pointing into it makes the integrator and the
   depenetration pass fight every frame - the player buries into the surface,
   gets shoved out, and buries again. That judder was the scraping feel along
   every wall in the arena, and it is also why running square into one needed a
   special case to stop the player: nothing else ever would.

   Clipping is skipped while a wall verb owns the contact. A wall climb holds
   itself against the surface with a deliberate inward velocity, and a wall run
   overwrites velocity with the tangent anyway; taking the inward component off
   either of them would be undoing the mechanic rather than resolving a
   collision. */
static void clip_velocity(PlayerController* player,
                          float normal_x, float normal_z) {
    if (player->is_wall_climbing || player->is_wall_running) return;
    float into_wall = player->velocity_x * normal_x + player->velocity_z * normal_z;
    if (into_wall >= 0.0f) return;
    player->velocity_x -= normal_x * into_wall;
    player->velocity_z -= normal_z * into_wall;
}

static void resolve_wall_collisions(PlayerController* player) {
    /* On the ground, a low enough box is a step rather than an obstacle; the
       support snap afterwards lifts the player onto it. */
    float step = player->is_grounded ? MOVE_STEP_UP_HEIGHT : 0.0f;
    for (int i = 0; i < ARENA_WALL_COUNT; ++i) {
        const ArenaWall* wall = &g_arena_walls[i];
        if (player->y >= wall->top - 0.05f) continue;
        if (wall->top <= player->y + step) continue;
        /* Overhead entirely: a catwalk, a lintel, the floor of a storey above.
           Without this every raised box would be a wall at ground level and no
           doorway would have a way in. */
        if (wall->base >= player->y + MOVE_PLAYER_HEIGHT) continue;
        float half_width = wall->width * 0.5f;
        float half_depth = wall->depth * 0.5f;
        float nearest_x = clampf(player->x, wall->x - half_width,
                                wall->x + half_width);
        float nearest_z = clampf(player->z, wall->z - half_depth,
                                wall->z + half_depth);
        float dx = player->x - nearest_x;
        float dz = player->z - nearest_z;
        float distance_squared = dx * dx + dz * dz;

        if (distance_squared >= PLAYER_RADIUS * PLAYER_RADIUS) continue;

        if (distance_squared > 0.000001f) {
            float distance = sqrtf(distance_squared);
            float push = PLAYER_RADIUS - distance;
            float normal_x = dx / distance;
            float normal_z = dz / distance;
            player->x += normal_x * push;
            player->z += normal_z * push;
            clip_velocity(player, normal_x, normal_z);
        } else {
            /* Dead inside the box, with no face to read a normal off. Leave by
               the shortest axis and clip against that. */
            float push_x = half_width + PLAYER_RADIUS - fabsf(player->x - wall->x);
            float push_z = half_depth + PLAYER_RADIUS - fabsf(player->z - wall->z);
            if (push_x < push_z) {
                float sign = player->x < wall->x ? -1.0f : 1.0f;
                player->x += sign * push_x;
                clip_velocity(player, sign, 0.0f);
            } else {
                float sign = player->z < wall->z ? -1.0f : 1.0f;
                player->z += sign * push_z;
                clip_velocity(player, 0.0f, sign);
            }
        }
    }
}

/* Both ramps read the same clock, so a player who has been off surfaces for a
   while falls harder and holds speed worse at the same time. */
static float air_ramp(const PlayerController* player) {
    return clampf(player->air_time / MOVE_AIR_RAMP_TIME, 0.0f, 1.0f);
}

float player_locomotion_air_gravity(const PlayerController* player) {
    return lerpf(MOVE_GRAVITY, MOVE_AIR_GRAVITY_END, air_ramp(player));
}

void player_locomotion_apply_air_drag(PlayerController* player,
                                      const PlayerFrameInput* in) {
    float speed = sqrtf(player->velocity_x * player->velocity_x +
                        player->velocity_z * player->velocity_z);
    if (speed < 0.0001f) return;
    float drag = lerpf(MOVE_AIR_DRAG_START, MOVE_AIR_DRAG_END,
                       air_ramp(player));
    float slowed = fmaxf(0.0f, speed - drag * in->delta_time);
    player->velocity_x *= slowed / speed;
    player->velocity_z *= slowed / speed;
}

/* Steers a wall jump's carried speed toward where the player is looking. The
   look direction is first flattened against the wall, so aiming back into the
   surface just gives the jump straight along it instead of cancelling it. */
static void steer_toward_look(PlayerController* player,
                              const PlayerFrameInput* in, float speed) {
    float yaw = in->camera_yaw_degrees * MATH_DEG_TO_RAD;
    float look_x = -sinf(yaw);
    float look_z = -cosf(yaw);
    float into_wall = look_x * player->wall_normal_x +
                      look_z * player->wall_normal_z;
    if (into_wall < 0.0f) {
        look_x -= player->wall_normal_x * into_wall;
        look_z -= player->wall_normal_z * into_wall;
    }
    float look_length = sqrtf(look_x * look_x + look_z * look_z);
    if (look_length < 0.0001f) return;
    look_x /= look_length;
    look_z /= look_length;

    float direction_x = player->velocity_x / speed;
    float direction_z = player->velocity_z / speed;
    direction_x += (look_x - direction_x) * MOVE_WALL_JUMP_LOOK;
    direction_z += (look_z - direction_z) * MOVE_WALL_JUMP_LOOK;
    float length = sqrtf(direction_x * direction_x + direction_z * direction_z);
    if (length < 0.0001f) return;
    /* The blend only rotates the velocity; the speed it carried is untouched. */
    player->velocity_x = direction_x / length * speed;
    player->velocity_z = direction_z / length * speed;
}

/* Rescales horizontal velocity to the speed a burst leaves it at. The ceiling
   bounds what the burst may add and never the player's speed: arriving above
   it means the burst pays nothing, and nothing is ever taken away. */
static void apply_burst(PlayerController* player, float burst, float ceiling) {
    float speed = sqrtf(player->velocity_x * player->velocity_x +
                        player->velocity_z * player->velocity_z);
    if (speed < 0.001f) return;
    float boosted = fmaxf(speed, fminf(speed + burst, ceiling));
    player->velocity_x *= boosted / speed;
    player->velocity_z *= boosted / speed;
}

void player_locomotion_apply_jump(PlayerController* player,
                                  const PlayerFrameInput* in) {
    /* Space kicks away from a wall the player is on, run or climb, and remains
       the normal ground jump everywhere else. Bailing out of a climb early with
       a jump is the point of the climb: it converts the height into a leap. */
    if ((player->is_wall_running || player->is_wall_climbing) &&
        player->jump_buffer_time > 0.0f) {
        apply_burst(player, MOVE_WALL_JUMP_BURST, MOVE_WALL_JUMP_BURST_CEILING);
        float carried_speed = sqrtf(player->velocity_x * player->velocity_x +
                                    player->velocity_z * player->velocity_z);
        if (carried_speed > 0.001f)
            steer_toward_look(player, in, carried_speed);
        player->velocity_x += player->wall_normal_x * MOVE_WALL_JUMP_PUSH;
        player->velocity_z += player->wall_normal_z * MOVE_WALL_JUMP_PUSH;
        player->vertical_velocity = MOVE_WALL_JUMP_SPEED;
        player->is_wall_running = 0;
        player->is_wall_climbing = 0;
        player->wall_jumped = 1;
        player->wall_run_cooldown = MOVE_WALL_REATTACH_DELAY;
        player->jump_buffer_time = 0.0f;
    } else if ((player->is_grounded || player->coyote_time > 0.0f) &&
               player->jump_buffer_time > 0.0f) {
        if (player->is_sliding) {
            float slide_speed = sqrtf(player->velocity_x * player->velocity_x +
                                      player->velocity_z * player->velocity_z);
            if (slide_speed >= MOVE_SLIDE_MIN_SPEED) {
                float boosted_speed = slide_speed + MOVE_SLIDE_JUMP_BURST;
                player->velocity_x *= boosted_speed / slide_speed;
                player->velocity_z *= boosted_speed / slide_speed;
            }
            player->is_sliding = 0;
            player->slide_reentry_time = MOVE_SLIDE_REENTRY_DELAY;
        } else {
            /* A jump pays out the moment it fires, so a route of short hops
               off surfaces beats one long float - the air ramps charge for
               hanging around and a chained landing keeps resetting them. The
               ceiling is what stops the pump running away: a perfect hop is
               grounded for a single step and pays no friction at all, so
               without one the burst would compound forever. A slide jump has
               its own burst and does not take this as well, and a standing
               jump has no direction to boost. */
            apply_burst(player, MOVE_JUMP_SPEED_BURST, MOVE_JUMP_BURST_CEILING);
        }
        player->vertical_velocity = MOVE_JUMP_SPEED;
        player->is_grounded = 0;
        player->coyote_time = 0.0f;
        player->jump_buffer_time = 0.0f;
    } else if (player->jump_buffer_time > 0.0f &&
               player->air_jumps_used < MOVE_AIR_JUMP_COUNT &&
               !player_mantle_available(player, in)) {
        /* The jump kit. Last of the three branches, so a wall and the ground
           both outrank it and a press is never spent in the air while a real
           surface was offering a better jump.

           Vertical is *set*, not added, so a double jump rescues a fall rather
           than merely slowing it - that is the whole reason it is a safety net
           and not just extra height. It pays no speed burst: a jump that could
           be taken in mid-air with nothing to push off would otherwise be a
           free pump with no surface to gate it, and the point of the move is
           to redirect a route rather than to make one faster.

           It does not reset air_time either. The drag and gravity ramps are
           what charge for hanging around, and the player has not touched
           anything - they have only asked the kit for another push. */
        player->air_jumps_used++;
        player->vertical_velocity = MOVE_AIR_JUMP_SPEED;
        /* The redirect. Rotates the heading toward what the player is asking
           for while keeping the speed they had, which is what makes the second
           jump a correction for a first one that went the wrong way. */
        if (in->has_move_input) {
            float speed = sqrtf(player->velocity_x * player->velocity_x +
                                player->velocity_z * player->velocity_z);
            if (speed > 0.001f) {
                float direction_x = player->velocity_x / speed;
                float direction_z = player->velocity_z / speed;
                direction_x += (in->wish_x - direction_x) * MOVE_AIR_JUMP_STEER;
                direction_z += (in->wish_z - direction_z) * MOVE_AIR_JUMP_STEER;
                float length = sqrtf(direction_x * direction_x +
                                     direction_z * direction_z);
                if (length > 0.0001f) {
                    player->velocity_x = direction_x / length * speed;
                    player->velocity_z = direction_z / length * speed;
                }
            }
        }
        player->jump_buffer_time = 0.0f;
    }
}

void player_locomotion_accelerate(PlayerController* player,
                                  const PlayerFrameInput* in) {
    /* grounded_crouch is already gated on being grounded, so the crouch speed
       target simply does not exist in the air. */
    float target_speed = in->grounded_crouch ? MOVE_CROUCH_SPEED :
                         (player->is_sprinting ? MOVE_SPRINT_SPEED : MOVE_WALK_SPEED);

    if (!player->is_grounded) {
        if (in->has_move_input) {
            air_strafe(player, in->wish_x, in->wish_z, target_speed,
                       in->delta_time);
            air_steer(player, in->wish_x, in->wish_z, in->delta_time);
        }
        return;
    }

    /* Friction first and unconditionally, then acceleration on top - the Quake
       order, and the reason it matters is that the accelerator can only ever
       add along the wish direction. Friction is the only thing in the pair
       able to remove a heading the player has stopped asking for, so it cannot
       be conditional on them having let go. Sliding never reaches here; it
       owns its own much lower friction, which is what makes a slide the way to
       carry speed across ground. */
    ground_friction(player, in->delta_time);
    if (in->has_move_input)
        accelerate_along(player, in->wish_x, in->wish_z, target_speed,
                         MOVE_GROUND_ACCELERATION, in->delta_time);
}

void player_locomotion_integrate(PlayerController* player,
                                 const PlayerFrameInput* in) {
    player->x += player->velocity_x * in->delta_time;
    player->z += player->velocity_z * in->delta_time;
    resolve_wall_collisions(player);

    /* Wall tops are standable now, which makes the boundary wall a walkway.
       Nothing outside the arena is playable, so bound the player hard and drop
       the velocity that is pushing them out. */
    if (player->x < -ARENA_HALF_SIZE) {
        player->x = -ARENA_HALF_SIZE;
        if (player->velocity_x < 0.0f) player->velocity_x = 0.0f;
    } else if (player->x > ARENA_HALF_SIZE) {
        player->x = ARENA_HALF_SIZE;
        if (player->velocity_x > 0.0f) player->velocity_x = 0.0f;
    }
    if (player->z < -ARENA_HALF_SIZE) {
        player->z = -ARENA_HALF_SIZE;
        if (player->velocity_z < 0.0f) player->velocity_z = 0.0f;
    } else if (player->z > ARENA_HALF_SIZE) {
        player->z = ARENA_HALF_SIZE;
        if (player->velocity_z > 0.0f) player->velocity_z = 0.0f;
    }
}

void player_locomotion_resolve_vertical(PlayerController* player,
                                        const PlayerFrameInput* in) {
    if (!player->is_grounded) {
        float previous_y = player->y;
        player->y += player->vertical_velocity * in->delta_time;
        /* Only surfaces at or below where the player started this frame can
           catch them, so rising never snaps them onto a box they have not
           cleared. */
        float support = arena_support_height(player->x, player->z, PLAYER_RADIUS,
                                             previous_y + 0.001f);
        /* Rising into an underside stops the climb rather than passing through
           it. Measured from where the head started, so a player already inside
           a box's span is not shoved by one they have partly cleared. */
        if (player->vertical_velocity > 0.0f) {
            float ceiling = arena_ceiling_height(player->x, player->z,
                                                 PLAYER_RADIUS,
                                                 previous_y + MOVE_PLAYER_HEIGHT);
            if (player->y + MOVE_PLAYER_HEIGHT > ceiling) {
                player->y = ceiling - MOVE_PLAYER_HEIGHT;
                player->vertical_velocity = 0.0f;
            }
        }
        if (player->vertical_velocity <= 0.0f && player->y <= support) {
            player->y = support;
            player->vertical_velocity = 0.0f;
            player->is_grounded = 1;
            player->is_wall_running = 0;
            player->wall_run_time = 0.0f;
            player->wall_run_wall_index = -1;

            /* Holding crouch through a fast landing flows directly into a slide. */
            float landing_speed = sqrtf(player->velocity_x * player->velocity_x +
                                        player->velocity_z * player->velocity_z);
            if (in->crouch_held && landing_speed >= MOVE_SLIDE_MIN_SPEED)
                player_slide_begin(player);
        }
    } else {
        /* Stay glued to the surface underfoot: step up onto low boxes, ride
           small drops, and start falling when the player runs out of ledge.
           Coyote time was already granted this frame. */
        float support = arena_support_height(player->x, player->z, PLAYER_RADIUS,
                                             player->y + MOVE_STEP_UP_HEIGHT);
        if (support >= player->y - MOVE_STEP_DOWN_HEIGHT) player->y = support;
        else player->is_grounded = 0;
    }
}
