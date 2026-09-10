#include "gameplay/player_visual_state.h"

/* Controller to payload. Flat, on purpose: every line is one field, so what the
   rig can see is exactly what is written here and a field the wire would have
   to carry cannot be added anywhere else. */

void player_visual_state_fill(CharacterVisualState* out,
                              const CharacterVisualState* previous,
                              const PlayerController* player,
                              const Combatant* body,
                              float look_yaw_degrees, float look_pitch_degrees,
                              float ads) {
    if (!out || !player) return;

    /* Built aside and assigned at the end, because `previous` is allowed to be
       `out` and the events below read it after the fields would be written. */
    CharacterVisualState state;

    state.x = player->x;
    state.y = player->y;
    state.z = player->z;
    state.velocity_x = player->velocity_x;
    state.velocity_z = player->velocity_z;
    state.heading_degrees = player->heading_degrees;
    state.look_yaw_degrees = look_yaw_degrees;
    state.look_pitch_degrees = look_pitch_degrees;
    state.ads = ads;
    state.lean = player->lean;
    state.walk_time = player->walk_time;
    state.reach_x = player->mantle.grip_x;
    state.reach_y = player->mantle.grip_y;
    state.reach_z = player->mantle.grip_z;

    /* The pool's half of the picture. `lean` above is the peek and stays the
       peek; the reaction has its own fields, which is what lets a player who is
       peeking a corner and taking rounds do both at once. */
    state.dead = (unsigned char)(body && !body->alive);
    state.flinch = body ? body->flinch : 0.0f;
    state.flinch_x = body ? body->flinch_x : 0.0f;
    state.flinch_z = body ? body->flinch_z : 0.0f;
    state.flinch_region = (unsigned char)(body ? body->flinch_region
                                               : COMBATANT_REGION_CHEST);

    /* A mantle is a climb, and a climb is not standing on anything - the
       collision capsule is still resting on whatever it left, so the raw flag
       says grounded through the whole of it. Folded in here rather than left to
       the rig, so the wire carries the answer and not the two halves of it. */
    /* A corpse is not standing on anything: the death clip is authored falling
       to the floor, and the controller is not stepped while the body is down so
       its own flag is simply whatever it was killed on. */
    state.grounded = (unsigned char)(!state.dead &&
                                     player->ground.is_grounded &&
                                     !player->mantle.active);
    state.moving = (unsigned char)(player->is_moving != 0);
    state.has_movement_input = (unsigned char)(player->has_movement_input != 0);
    state.sprinting = (unsigned char)(player->is_sprinting != 0);
    state.crouching = (unsigned char)(player->is_crouching != 0);
    state.sliding = (unsigned char)(player->slide.active != 0);
    state.mantling = (unsigned char)(player->mantle.active != 0);
    state.ledge_hanging = (unsigned char)(player->mantle.ledge_hanging != 0);
    state.wall_climbing = (unsigned char)(player->wall_climb.active != 0);
    state.wall_running = (unsigned char)(player->wall_run.active != 0);
    state.grappling = (unsigned char)(player->grapple.active != 0);
    state.wall_run_side = (signed char)player->wall_run.side;

    /* The edges. A body with no previous frame gets none: it is spawning, and
       a spawn is not a landing.

       These are transitions and nothing more - whether a transition is worth an
       animation, and which one, is the rig's decision and stays there. */
    state.events = 0;
    if (previous) {
        if (!state.grounded && previous->grounded)
            state.events |= CHARACTER_EVENT_LEFT_GROUND;
        if (state.grounded && !previous->grounded)
            state.events |= CHARACTER_EVENT_LANDED;
        if (state.sliding && !previous->sliding)
            state.events |= CHARACTER_EVENT_SLIDE_BEGAN;
        if (!state.sliding && previous->sliding)
            state.events |= CHARACTER_EVENT_SLIDE_ENDED;
        if (state.mantling && !previous->mantling)
            state.events |= CHARACTER_EVENT_MANTLE_BEGAN;
        if (state.dead && !previous->dead)
            state.events |= CHARACTER_EVENT_DIED;
        if (!state.dead && previous->dead)
            state.events |= CHARACTER_EVENT_RESPAWNED;
    }

    *out = state;
}
