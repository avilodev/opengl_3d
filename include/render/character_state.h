#ifndef RENDER_CHARACTER_STATE_H
#define RENDER_CHARACTER_STATE_H

/* Everything the character rig reads about one body, and nothing else.

   The rig used to read a PlayerController directly, reaching into about twenty
   fields across six nested substructs. That works for exactly one body: the
   local player's. A body on the far end of a wire has no controller - it has a
   snapshot, arriving at whatever rate the network manages - and a body driven
   by a replay or a dummy in a shooting range has neither.

   So this is the narrow thing in between. It is a plain struct of scalars with
   no pointers and no padding worth caring about, which makes it the
   replication payload as well as the rig's input: the local player fills it
   from their controller (player_visual_state_fill), the network fills it from a
   packet, and the same code draws both. Adding a field here is adding a field
   to the wire, which is the right amount of friction for that. */

/* One-shot moments, as explicit bits rather than something the rig infers.

   The rig used to spot these itself by comparing this frame's flags against
   last frame's - leaving the ground meant a jump, arriving on it meant a
   landing. That only works when every frame is seen. Snapshots arrive at maybe
   a third of the frame rate and some of them do not arrive at all, and an edge
   that fell in the gap between two packets is a landing animation that never
   plays. A bit set by whoever knows the transition happened survives the trip.

   Set for the frame the transition happened on, and cleared by the next fill.
   A producer sending at less than the frame rate should hold each bit until it
   has been sent once, since the rig consumes them the frame it sees them. */
enum {
    CHARACTER_EVENT_LEFT_GROUND  = 1 << 0,
    CHARACTER_EVENT_LANDED       = 1 << 1,
    CHARACTER_EVENT_SLIDE_BEGAN  = 1 << 2,
    CHARACTER_EVENT_SLIDE_ENDED  = 1 << 3,
    CHARACTER_EVENT_MANTLE_BEGAN = 1 << 4
};

typedef struct {
    /* Where the body is, in world units - its feet, not its middle. */
    float x, y, z;

    /* Horizontal velocity, which sets the pace the locomotion clips play at.
       The vertical component is not here: nothing in the rig reads it. */
    float velocity_x, velocity_z;

    /* Which way the body is travelling, in the atan2(vx, vz) degrees the rest
       of the game measures headings in, so zero faces +Z. */
    float heading_degrees;

    /* Which way the body is looking, which is a different thing from where it
       is going and outranks it above the waist: the chest, the arms, the head
       and the rifle turn here and the legs are twisted back underneath.

       For the local player these come off their camera, which is why they were
       once named for one. For anyone else they are look direction and nothing
       to do with a camera, which is the point of the rename: a remote body has
       a facing, and whoever is watching it has a camera, and those are not the
       same number. */
    float look_yaw_degrees;
    float look_pitch_degrees;

    /* How far the sight is up, 0 carried at the ready to 1 on the sight. */
    float ads;

    /* Authored lean in [-1, 1], positive to the body's own left - the side its
       left arm is on, which for a rig facing its own +z with +y up is +x. */
    float lean;

    /* The stride clock, for the box fallback body's one animation. The
       mannequin has its own playhead and ignores this. */
    float walk_time;

    /* The point on the ledge's top edge a hanging body is holding, in world
       units. Meaningless unless `ledge_hanging`. */
    float grip_x, grip_y, grip_z;

    /* On a supporting surface. Not the controller's raw flag: a body partway
       through a mantle is off the ground as far as the rig is concerned,
       whatever the collision says. */
    unsigned char grounded;
    unsigned char moving;
    unsigned char has_movement_input;
    unsigned char sprinting;
    unsigned char crouching;
    unsigned char sliding;
    unsigned char mantling;
    unsigned char ledge_hanging;
    unsigned char wall_climbing;
    unsigned char wall_running;
    unsigned char grappling;
    /* Which side the wall is on during a wall run: -1, 0 or +1. */
    signed char wall_run_side;

    /* CHARACTER_EVENT_* bits for this frame. */
    unsigned char events;
} CharacterVisualState;

#endif
