#include "gameplay/third_person_camera.h"
#include "gameplay/movement_tuning.h"
#include "gameplay/traversal.h"
#include "gameplay/weapon_tuning.h"
#include "utils/math_utils.h"

#include <math.h>

void third_person_camera_init(ThirdPersonCamera* camera) {
    camera->yaw_degrees = 0.0f;
    /* Level. Eighteen degrees up was the other half of the orbit camera this
       used to be - a resting tilt that makes sense for a view hanging behind a
       body and none at all for a first-person one, which is what the game
       starts in. It spawned the player looking at the sky. */
    camera->pitch_degrees = 0.0f;
    camera->distance = 0.0f;
    camera->target_height = 2.12f;
    camera->fov_degrees = VIEW_BASE_FOV;
    camera->roll_degrees = 0.0f;
    camera->lean_offset = 0.0f;
    camera->view_mode = CAMERA_VIEW_FIRST_PERSON;
    camera->is_first_person = 1;
    camera->dip = 0.0f;
    camera->dip_velocity = 0.0f;
    camera->fov_impulse = 0.0f;
    camera->roll_impulse = 0.0f;
    camera->zoom = 0.0f;
    camera->zoom_fov = VIEW_BASE_FOV;
    camera->zoom_is_scoped = 0;
    camera->scope_fov_degrees = VIEW_BASE_FOV;
    camera->step_smooth = 0.0f;
    camera->previous_player_y = 0.0f;
    camera->previous_vertical_velocity = 0.0f;
    camera->was_grounded = 1;
    camera->was_sliding = 0;
}

/* The view mode, and the flag the rest of the engine reads it through. One
   function so the two cannot drift apart. */
static void set_view(ThirdPersonCamera* camera, CameraViewMode mode) {
    camera->view_mode = mode;
    camera->is_first_person = mode == CAMERA_VIEW_FIRST_PERSON;
}

/* Which side of the screen a wall is on, so a lean or a kick goes the right
   way. Positive is to the player's right. */
static float wall_screen_side(const ThirdPersonCamera* camera,
                              const PlayerController* player) {
    float yaw = camera->yaw_degrees * MATH_DEG_TO_RAD;
    float camera_right_x = cosf(yaw);
    float camera_right_z = -sinf(yaw);
    /* The wall is opposite its outward normal. */
    return -(player->wall_normal_x * camera_right_x +
             player->wall_normal_z * camera_right_z);
}

/* Fires the one-shot kicks on the frames the player changed state. Landing is
   the only one scaled by magnitude - a heavy landing dips further than a hop,
   which is what tells the player how much speed they just arrived with. */
static void detect_transitions(ThirdPersonCamera* camera,
                               const PlayerController* player) {
    if (!camera->was_grounded && player->ground.is_grounded) {
        float impact = fmaxf(0.0f, -camera->previous_vertical_velocity);
        camera->dip_velocity -= fminf(impact * VIEW_LAND_DIP_PER_SPEED,
                                      VIEW_LAND_DIP_IMPULSE_MAX);
    }
    if (!camera->was_sliding && player->slide.active)
        camera->fov_impulse += VIEW_SLIDE_FOV_KICK;
    /* Only an actual wall jump punches the view. Leaving a wall while still
       rising is not enough to go on - steering away off the wall does that
       too, and inferring a jump from it fires a kick nobody asked for. */
    if (player->wall_run.jumped) {
        camera->fov_impulse += VIEW_WALL_JUMP_FOV_KICK;
        camera->roll_impulse -= wall_screen_side(camera, player) *
                                VIEW_WALL_JUMP_ROLL_KICK;
    }
}

/* Cancels a step-up on the view and lets it catch up. The player's feet move
   the whole height in one frame; the eyes keep the height they had and close
   the gap over about a tenth of a second, which is the difference between a
   kerb reading as a step and reading as a stutter.

   Gated on the player being grounded both this frame and last, so only a real
   step is caught. A landing, a fall, and a mantle all move the player further
   and are transitions they are meant to feel - the dip spring speaks for the
   first and the mantle animates itself. */
static void smooth_step_up(ThirdPersonCamera* camera,
                           const PlayerController* player, float delta_time) {
    float risen = player->y - camera->previous_player_y;
    if (camera->was_grounded && player->ground.is_grounded &&
        fabsf(risen) <= VIEW_STEP_SMOOTH_MAX)
        camera->step_smooth -= risen;
    camera->step_smooth = clampf(camera->step_smooth, -VIEW_STEP_SMOOTH_MAX,
                                 VIEW_STEP_SMOOTH_MAX);
    camera->step_smooth *= expf(-delta_time * VIEW_STEP_SMOOTH_RATE);
    camera->previous_player_y = player->y;
}

/* The dip is a real spring so a landing overshoots and settles; the FOV and
   roll kicks only need to fall off, so they decay. */
static void settle_impulses(ThirdPersonCamera* camera, float delta_time) {
    camera->dip_velocity += (-VIEW_DIP_STIFFNESS * camera->dip -
                             VIEW_DIP_DAMPING * camera->dip_velocity) * delta_time;
    camera->dip += camera->dip_velocity * delta_time;

    float decay = expf(-delta_time * VIEW_IMPULSE_DECAY);
    camera->fov_impulse *= decay;
    camera->roll_impulse *= decay;
}

/* Nearer than this a point is behind the eye or so close that the divide blows
   up. It is the projection's own near plane, which renderer3d sets to the same
   number. */
#define CAMERA_PROJECT_NEAR 0.1f

int third_person_camera_project(const ThirdPersonCamera* camera,
                                const PlayerController* player,
                                float x, float y, float z, float aspect_ratio,
                                float* screen_x, float* screen_y, float* depth) {
    if (!camera || !player || aspect_ratio <= 0.0f) return 0;

    float view_yaw, view_pitch, view_roll;
    third_person_camera_view_angles(camera, &view_yaw, &view_pitch, &view_roll);
    float forward[3], right[3], up[3];
    traversal_view_basis(view_yaw, view_pitch, view_roll, forward, right, up);

    /* Relative to what the camera orbits, which is the player's eye - not to
       the camera itself. The orbit distance and the lean's shift are applied
       below, in the same order and with the same signs draw_world translates
       them, because this has to agree with that pass and not merely resemble
       it. */
    float rx = x - player->x;
    float ry = y - (player->y + camera->target_height);
    float rz = z - player->z;

    float across = rx * right[0] + ry * right[1] + rz * right[2] -
                   camera->lean_offset;
    float above = rx * up[0] + ry * up[1] + rz * up[2];
    float along = rx * forward[0] + ry * forward[1] + rz * forward[2] +
                  camera->distance;
    if (along < CAMERA_PROJECT_NEAR) return 0;

    float tangent = tanf(camera->fov_degrees * 0.5f * MATH_DEG_TO_RAD);
    if (tangent < 0.0001f) return 0;
    if (screen_x) *screen_x = 0.5f + 0.5f * (across / along) /
                              (tangent * aspect_ratio);
    /* Screen y runs downward and world y runs up, so this one is subtracted. */
    if (screen_y) *screen_y = 0.5f - 0.5f * (above / along) / tangent;
    if (depth) *depth = along;
    return 1;
}

void third_person_camera_set_zoom(ThirdPersonCamera* camera, float zoom,
                                  float zoom_fov_degrees, int is_scoped) {
    camera->zoom = clampf(zoom, 0.0f, 1.0f);
    camera->zoom_fov = clampf(zoom_fov_degrees, 5.0f, VIEW_BASE_FOV);
    camera->zoom_is_scoped = is_scoped;
}

/* Recoil is not here any more. The kick lives on the weapon, as the gun's own
   rotation, because a holographic reticle points where the GUN points while
   the bullet leaves the EYE - and those agree only if one number drives both.
   Kicking the camera moved the eye and left the gun to be rotated separately
   for looks, which would have put the reticle and the round out of step the
   moment the gun rotated. See gun_pitch_degrees in weapon.h.

   The impulses below still rest on the old invariant among themselves: the
   composed angles carry exactly the accumulator's worth at all times. They
   keep it for free, because nothing outside this file touches them - which was
   never true of recoil, and was the whole difficulty. */

void third_person_camera_apply_look(ThirdPersonCamera* camera,
                                   const CameraCommand* command) {
    /* Scaled by the zoom and not by the live field of view: speed widens the
       FOV too, and letting that touch the mouse would mean sprinting quietly
       changed the player's aim. */
    const float zoomed_tangent = tanf(camera->zoom_fov * 0.5f * MATH_DEG_TO_RAD);
    const float base_tangent = tanf(VIEW_BASE_FOV * 0.5f * MATH_DEG_TO_RAD);
    const float sensitivity = 0.12f *
        lerpf(1.0f, zoomed_tangent / base_tangent, camera->zoom);
    /* The pitch clamp is measured against where the player is aiming, and with
       the kick on the gun that is simply where these angles already are. */
    camera->yaw_degrees -= command->look_x * sensitivity;
    camera->pitch_degrees = clampf(camera->pitch_degrees - command->look_y * sensitivity,
                                   VIEW_PITCH_MIN, VIEW_PITCH_MAX);

    if (command->toggle_view_pressed)
        set_view(camera, (camera->view_mode + 1) % CAMERA_VIEW_COUNT);
    /* And the shortcut straight to the front view, for looking at what the body
       is actually doing without cycling past the view behind it. It goes back
       to first person rather than to wherever it came from: a key that returns
       somewhere different each time it is pressed is a key nobody can use
       without looking. */
    if (command->front_view_pressed)
        set_view(camera, camera->view_mode == CAMERA_VIEW_THIRD_FRONT
                 ? CAMERA_VIEW_FIRST_PERSON : CAMERA_VIEW_THIRD_FRONT);
}

void third_person_camera_view_angles(const ThirdPersonCamera* camera,
                                     float* yaw_degrees, float* pitch_degrees,
                                     float* roll_degrees) {
    int front = camera->view_mode == CAMERA_VIEW_THIRD_FRONT;
    if (yaw_degrees) *yaw_degrees = camera->yaw_degrees + (front ? 180.0f : 0.0f);
    if (pitch_degrees) *pitch_degrees = front ? -camera->pitch_degrees
                                              : camera->pitch_degrees;
    if (roll_degrees) *roll_degrees = front ? -camera->roll_degrees
                                            : camera->roll_degrees;
}

void third_person_camera_update(ThirdPersonCamera* camera, const CameraCommand* command,
                                const PlayerController* player, float delta_time) {
    (void)command;

    /* The impulses are composed on top of the smoothed camera at the end of
       every frame, so last frame's contribution has to come back off before
       smoothing. Without this the blend absorbs each kick into the base value
       and they pile up instead of decaying. */
    camera->fov_degrees -= camera->fov_impulse;
    camera->roll_degrees -= camera->roll_impulse;
    camera->target_height -= camera->dip;
    camera->target_height -= camera->step_smooth;

    detect_transitions(camera, player);
    settle_impulses(camera, delta_time);
    /* After detect_transitions, which still needs last frame's was_grounded. */
    smooth_step_up(camera, player, delta_time);

    float speed = sqrtf(player->velocity_x * player->velocity_x +
                        player->velocity_z * player->velocity_z);
    /* Saturates around 31 m/s. The old curve topped out at 15 m/s, which is
       below a wall chain and well below a grapple - so exactly where speed
       starts to matter, the view stopped reporting it. */
    float target_fov = VIEW_BASE_FOV + clampf((speed - 5.0f) * 0.85f, 0.0f, 22.0f);
    /* The sight wins over speed: a player who raised it asked for the narrow
       view and should not have it widened back out by going fast. A magnified
       optic narrows only its own circle, so there the periphery is left alone
       and the narrowing is published separately. */
    if (!camera->zoom_is_scoped)
        target_fov = lerpf(target_fov, camera->zoom_fov, camera->zoom);
    float target_roll = player->lean * VIEW_LEAN_ROLL;
    if (player->wall_run.active)
        target_roll = wall_screen_side(camera, player) * VIEW_WALL_RUN_ROLL;
    float target_height = camera->is_first_person
        ? (player->is_crouching ? 1.18f : 2.12f)
        : (player->is_crouching ? 0.92f : 1.35f);
    /* Hanging drops the eyes toward the lip the player is holding, which is the
       second half of the tell the stowed weapon starts. Eased by the same blend
       as every other height change below, so grabbing a ledge settles into it. */
    if (player->mantle.ledge_hanging) target_height -= VIEW_LEDGE_HANG_DROP;
    if (!camera->is_first_person && player->has_movement_input &&
        player->ground.is_grounded && !player->slide.active)
        target_height += sinf(player->walk_time * 2.0f) * .035f;
    /* Running up a wall lifts the view so the climb reads as vertical. */
    if (player->wall_climb.active) target_height += VIEW_CLIMB_RISE;
    float target_distance = camera->is_first_person ? 0.0f : 6.5f;
    float blend = clampf(delta_time * 8.0f, 0.0f, 1.0f);
    camera->fov_degrees += (target_fov - camera->fov_degrees) * blend;
    camera->roll_degrees += (target_roll - camera->roll_degrees) * blend;
    /* In first person this is the actual corner peek. In third person moving
       the camera sideways makes the stationary avatar slide the opposite way
       on screen (the view transform is inverse), fighting its authored pose. */
    camera->lean_offset = camera->is_first_person
        ? -player->lean * VIEW_LEAN_OFFSET : 0.0f;
    camera->target_height += (target_height - camera->target_height) * blend;
    camera->distance += (target_distance - camera->distance) *
                        clampf(delta_time * 12.0f, 0.0f, 1.0f);

    /* Re-apply on top of the freshly smoothed base, so a kick lands whole on
       the frame it fires instead of easing in. */
    camera->fov_degrees += camera->fov_impulse;
    camera->roll_degrees += camera->roll_impulse;
    camera->target_height += camera->dip;
    camera->target_height += camera->step_smooth;

    /* Settled after the blend, from the FOV the frame actually ended on. */
    camera->scope_fov_degrees = camera->zoom_is_scoped
        ? lerpf(camera->fov_degrees, camera->zoom_fov, camera->zoom)
        : camera->fov_degrees;

    camera->previous_vertical_velocity = player->vertical_velocity;
    camera->was_grounded = player->ground.is_grounded;
    camera->was_sliding = player->slide.active;
}
