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
    CHARACTER_EVENT_MANTLE_BEGAN = 1 << 4,
    /* The pool emptied. A state as well as an edge - see `dead` - because a
       body that arrives mid-fall from a packet has to know it is down, and
       because the corpse outlasts the frame the bit was set on. */
    CHARACTER_EVENT_DIED         = 1 << 5,
    /* Stood back up, somewhere else. Outranks the ground edges on any frame
       carrying it: player_controller_place is init-then-pose, so it leaves the
       controller ungrounded and the step that follows finds support and emits
       a LANDED that would otherwise fire a landing over the top of the
       get-up. */
    CHARACTER_EVENT_RESPAWNED    = 1 << 6
};

/* Which band of the hitbox a round landed in, for the reaction layer. Kept
   numerically identical to CombatantRegion; declared again rather than
   included, because the rig has no business knowing what a Combatant is. */
enum {
    CHARACTER_HIT_LEGS,
    CHARACTER_HIT_CHEST,
    CHARACTER_HIT_HEAD
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
       left arm is on, which for a rig facing its own +z with +y up is +x.

       This is PEEK and nothing else. It used to carry the flinch as well,
       because a rig with no hit clip had no other channel to bend a spine on,
       which meant the two overwrote each other on any body that could do both -
       and the player, who is the only body that peeks, therefore never showed a
       flinch at all. The reaction has its own three fields below now. */
    float lean;

    /* ---- the hit reaction ----
       How hard the body is rocked, 0 to 1, decaying on the producer's side
       rather than the rig's, and the unit horizontal direction the round was
       travelling in world space.

       Continuous rather than an event bit on purpose. The events above exist
       because a transition that falls between two packets is a transition the
       rig never sees; a decaying scalar has no such problem, because a packet
       that goes missing means it is sampled late rather than lost. Carrying a
       direction rather than a signed magnitude is what lets the reaction bend
       away from the actual round instead of only sideways. */
    float flinch;
    float flinch_x, flinch_z;

    /* The stride clock, for the box fallback body's one animation. The
       mannequin has its own playhead and ignores this. */
    float walk_time;

    /* A point in the world one hand is reaching for. The ledge a hanging body
       is holding today; the anchor a grapple is hauling on when that pose is
       authored, which wants exactly the same field and exactly the same solve.
       Meaningless unless something that reaches is running. */
    float reach_x, reach_y, reach_z;

    /* Down, and not to be drawn standing. Named for the state it is rather than
       for its opposite, which is not a style choice: combatant_visual_state_fill
       clears this whole struct to zero and then writes only what a standing body
       is doing, relying on zero being the sane default for anything added later.
       An `alive` field would make every body in the world a corpse the moment it
       was added and say nothing at the call site. */
    unsigned char dead;

    /* Which band the round that set `flinch` landed in - CHARACTER_HIT_*. Picks
       the head snap apart from the body rock. */
    unsigned char flinch_region;

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

    /* CHARACTER_EVENT_* bits for this frame.

       A short rather than a byte. Seven bits are spoken for and the eighth
       would be the last one free, which puts the next movement verb that wants
       an edge in the position of paying for a change to the wire format to get
       it. Widening now costs a byte on a struct nothing serialises yet. */
    unsigned short events;
} CharacterVisualState;

#endif
