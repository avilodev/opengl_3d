#ifndef THIRD_PERSON_CAMERA_H
#define THIRD_PERSON_CAMERA_H

#include "input/commands.h"
#include "gameplay/player_controller.h"

/* Where the camera sits relative to the body, cycled by the view key in this
   order. The front view is the mirror of the one behind - same orbit, half a
   turn round - and it exists to be able to look at the player from the front,
   which is the one thing neither of the other two can do. Aiming is unaffected
   by any of them: `yaw_degrees` and `pitch_degrees` stay where the player is
   pointed, and only the angles the world is drawn through change. */
typedef enum {
    CAMERA_VIEW_FIRST_PERSON,
    CAMERA_VIEW_THIRD_BEHIND,
    CAMERA_VIEW_THIRD_FRONT,
    CAMERA_VIEW_COUNT
} CameraViewMode;

typedef struct {
    float yaw_degrees;
    float pitch_degrees;
    float distance;
    float target_height;
    float fov_degrees;
    float roll_degrees;
    float lean_offset;
    CameraViewMode view_mode;
    /* Derived from `view_mode`, and kept because most of the engine only wants
       to know whether there is a body on screen and a viewmodel in hand. Both
       third-person modes are the same answer to that question. */
    int is_first_person;

    /* One-shot view feedback. Speed-to-FOV can only express a magnitude, so
       these carry the transitions: a dip on landing, a kick off a wall jump,
       a punch into a slide. They spring or decay back to rest on their own. */
    float dip;
    float dip_velocity;
    float fov_impulse;
    float roll_impulse;

    /* Held-back eye height, so a step-up moves the player's feet without
       yanking their eyes. The collision solver lifts the body onto a low box
       in a single frame - which is right, it should not have to jump a kerb -
       and this cancels that jump on the view and lets it glide back. Only a
       genuine step is caught: a landing or a fall is a transition the player is
       meant to feel, and the landing dip above is what expresses it. */
    float step_smooth;
    float previous_player_y;

    /* How far the sight is raised, 0 to 1, and the field of view it is raised
       to - both published by the weapon, because a holographic sight and a
       magnified prism do not zoom the same amount. The mouse slows by the same
       ratio the view narrows, so a given movement covers the same distance on
       screen aimed or not. */
    float zoom;
    float zoom_fov;
    /* A magnified sight does not zoom the screen, it zooms a circle on it. When
       this is set the periphery keeps `fov_degrees` and only `scope_fov_degrees`
       narrows, and the renderer draws the world a second time inside the
       optic. An unmagnified sight leaves it clear and simply zooms everything,
       in which case the two FOVs are equal. */
    int zoom_is_scoped;
    /* What the inside of the optic is drawn at. Equal to fov_degrees whenever
       there is no separate scope pass. */
    float scope_fov_degrees;

    /* ---- being down ----
       A dead player is watched rather than played, so the view leaves their
       head: there is a body on the floor and a death worth seeing, and neither
       of those is visible from inside the skull. `downed` is whether that is in
       force and `held_view` is the mode to give back, because the view a player
       chose is theirs and a death is not an opportunity to change it.

       The view keys go inert while it holds, for the same reason: a player who
       cycled the view while dead would be handed something different back from
       what they picked, and a key that does nothing is easier to understand
       than one whose effect is silently undone a few seconds later. */
    int downed;
    CameraViewMode held_view;

    /* Last frame's player state, kept here so the camera can spot a transition
       without gameplay having to publish events for the renderer's benefit. */
    float previous_vertical_velocity;
    int was_grounded;
    int was_sliding;
} ThirdPersonCamera;

void third_person_camera_init(ThirdPersonCamera* camera);
/* Aiming, and the view toggle. Split out from the update because it belongs to
   the frame rather than to the fixed simulation step: a mouse delta is an
   amount the frame already measured, and holding it back until a step happens
   to run makes turning stutter on any display faster than the simulation.
   Call once per frame, before the steps. */
void third_person_camera_apply_look(ThirdPersonCamera* camera,
                                    const CameraCommand* command);

/* The angles the world is drawn through, which are the player's own except in
   the front view: there the camera has orbited half a turn round to face the
   body, so the yaw is reversed, and the pitch and roll with it - otherwise a
   camera moved in front of the player would be looking at the sky when the
   player looked at the floor. Nothing else may use these. Where a shot goes,
   where the body faces and where the sight points all come off the player's own
   yaw and pitch, and those never move. */
void third_person_camera_view_angles(const ThirdPersonCamera* camera,
                                     float* yaw_degrees, float* pitch_degrees,
                                     float* roll_degrees);
void third_person_camera_update(ThirdPersonCamera* camera, const CameraCommand* command,
                                const PlayerController* player, float delta_time);

/* Down, or back up. Going down remembers the player's own view mode and forces
   the one behind the body; coming back up restores what was remembered. Idempotent,
   so it can be called every step with the body's `alive` flag rather than
   guarded by an edge somebody has to detect. */
void third_person_camera_set_downed(ThirdPersonCamera* camera, int downed);

/* How far into the sight the view is, 0 to 1, and the FOV that sight zooms to. */
void third_person_camera_set_zoom(ThirdPersonCamera* camera, float zoom,
                                  float zoom_fov_degrees, int is_scoped);

/* Where a world point lands on screen, as fractions of the viewport: 0,0 is the
   top-left corner and 1,1 the bottom-right, matching the axes everything that
   draws a screen already uses. Returns zero for a point at or behind the eye,
   in which case nothing is written.

   This is the view transform in renderer3d's draw_world, arithmetic only - the
   orbit distance, the lean's sideways shift, the roll, and the projection - and
   it lives here because the camera owns every one of those numbers. Anything
   that has to put a mark on a thing in the world rather than on the screen
   comes here, so there is one copy of it: a second would be another chance for
   a mark to disagree with what it is marking, which is the mistake
   traversal_aim_direction exists to have stopped being made twice.

   `aspect_ratio` is the viewport's, and the field of view is the camera's live
   one - so a mark keeps its place while speed is widening the view. It is
   deliberately not the scope's: what is inside a magnified optic is a second
   pass of the world drawn at its own field of view, and a mark placed through
   that would be right inside the tube and wrong everywhere else on the screen.

   No GL, no window, nothing to stop a check of it running headless. */
int third_person_camera_project(const ThirdPersonCamera* camera,
                                const PlayerController* player,
                                float x, float y, float z, float aspect_ratio,
                                float* screen_x, float* screen_y, float* depth);

#endif
