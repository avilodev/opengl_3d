#include "render/character_rig_internal.h"
#include "utils/math_utils.h"

#include <math.h>

/* Body heading, leg twist under the chest, head look, and standing steps. */

/* Max leg twist while moving. Slightly under 90 so a full strafe slides the
   feet a little rather than wringing the waist. */
#define FACING_LEG_YAW_MAX 80.0f

/* Max waist wind-up while standing before the feet step. */
#define FACING_STAND_TWIST_MAX 52.0f

/* Beyond this the legs flip and the walk plays in reverse. 112.5 sits between
   keyboard directions (multiples of 45), so it never chatters under a held key. */
#define FACING_REVERSE_ANGLE 112.5f

/* Leg pivot speed (deg/s) while moving, and time to reverse the walk cycle. */
#define FACING_LEG_TURN_RATE 540.0f
#define FACING_REVERSE_TIME 0.20f

/* Head turn limit off the chest (saturating) and turn speed (deg/s). */
#define FACING_LOOK_MAX 85.0f
#define FACING_LOOK_RATE 400.0f

/* Standing step speed, and how close to square the feet get before the step
   ends. Committed steps avoid per-frame shuffling at the twist limit. */
#define FACING_STEP_RATE 260.0f
#define FACING_STEP_SETTLE 10.0f

/* Time to hand the heading to/from a clip that owns it. Matches the clip fade. */
#define FACING_HANDOVER_TIME 0.14f

/* Lazy unwind rate (deg/s) for small standing twists. Main tuning knob for
   whether turning on the spot looks planted or slippery. */
#define FACING_LEG_SETTLE_RATE 60.0f

/* Degrees wrapped to (-180, 180]. */
static float wrap_degrees(float degrees) {
    while (degrees > 180.0f) degrees -= 360.0f;
    while (degrees <= -180.0f) degrees += 360.0f;
    return degrees;
}

/* Head yaw relative to the chest, eased and clamped. */
static void update_look(CharacterAnimation* animation, float aim_yaw, float delta_time) {
    float look = wrap_degrees(aim_yaw - animation->body_yaw);
    /* Near dead astern, keep the current side so the head doesn't swing through
       the front on jitter. */
    if (fabsf(look) > 170.0f && animation->look_yaw < 0.0f) look = -fabsf(look);
    look = clampf(look, -FACING_LOOK_MAX, FACING_LOOK_MAX) * animation->aim_weight;
    animation->look_yaw = approachf(animation->look_yaw, look,
                                    FACING_LOOK_RATE * delta_time);
}

void character_facing_update(CharacterAnimation* animation,
                             const CharacterVisualState* state, float delta_time) {
    /* Heading zero faces +Z; look yaw zero faces -Z. Convert look to heading. */
    float aim_yaw = state->look_yaw_degrees + 180.0f;
    float move_yaw = state->heading_degrees;

    /* These clips keep their travel heading: the body is attached to a surface
       (wall run/climb, mantle, hang), or it is a corpse. Rigs without a full leg
       chain also keep travel heading. */
    int clip_owns_heading = state->wall_running || state->wall_climbing ||
                            state->mantling || state->ledge_hanging ||
                            state->dead;
    float handover = animation->twist_legs && !clip_owns_heading
        ? animation->aim_weight : 0.0f;
    if (delta_time > 0.0f)
        animation->facing_weight = approachf(animation->facing_weight, handover,
                                            delta_time / FACING_HANDOVER_TIME);

    float was = animation->body_yaw;
    animation->body_yaw = move_yaw + wrap_degrees(aim_yaw - move_yaw) *
                                     animation->facing_weight;
    float turned = wrap_degrees(animation->body_yaw - was);

    if (delta_time <= 0.0f) return;

    update_look(animation, aim_yaw, delta_time);

    float target, rate, rate_target = 1.0f;
    if (state->moving) {
        target = wrap_degrees(move_yaw - animation->body_yaw);
        /* Past the reverse angle, face the legs the other way and run the
           cycle backwards (backpedal). */
        if (target > FACING_REVERSE_ANGLE) { target -= 180.0f; rate_target = -1.0f; }
        else if (target < -FACING_REVERSE_ANGLE) { target += 180.0f; rate_target = -1.0f; }
        target = clampf(target, -FACING_LEG_YAW_MAX, FACING_LEG_YAW_MAX);
        rate = FACING_LEG_TURN_RATE;
        animation->stepping = 0;
    } else {
        /* Planted feet: chest turns wind up the waist. The limit keeps any
           larger twist left over from a strafe, so stopping doesn't snap legs. */
        float limit = fmaxf(FACING_STAND_TWIST_MAX, fabsf(animation->leg_yaw));
        animation->leg_yaw = clampf(animation->leg_yaw - turned, -limit, limit);
        /* Hitting the limit starts a step, held until the feet are nearly square. */
        if (fabsf(animation->leg_yaw) >= FACING_STAND_TWIST_MAX) animation->stepping = 1;
        else if (fabsf(animation->leg_yaw) < FACING_STEP_SETTLE) animation->stepping = 0;
        target = 0.0f;
        rate = animation->stepping ? FACING_STEP_RATE : FACING_LEG_SETTLE_RATE;
    }

    animation->leg_yaw = approachf(animation->leg_yaw, target, rate * delta_time);
    animation->leg_rate = approachf(animation->leg_rate, rate_target,
                                    delta_time * 2.0f / FACING_REVERSE_TIME);
}
