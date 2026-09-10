#include "gameplay/combatant_visual_state.h"

#include <string.h>

void combatant_visual_state_fill(CharacterVisualState* out,
                                 const CharacterVisualState* previous,
                                 const Combatant* combatant) {
    if (!out || !combatant) return;

    /* The edges are the difference against last frame, and `previous` is
       allowed to be `out`, so they are read before anything is written. */
    unsigned short events = 0;
    unsigned char was_dead = previous ? previous->dead : 0;
    unsigned char dead = (unsigned char)(!combatant->alive);
    if (previous) {
        if (dead && !was_dead) events |= CHARACTER_EVENT_DIED;
        if (!dead && was_dead) events |= CHARACTER_EVENT_RESPAWNED;
    }

    /* Everything a standing body is not doing is zero, so it is cleared rather
       than assigned field by field: a field added to the payload later is then
       zero here by default instead of being silently uninitialised. */
    memset(out, 0, sizeof *out);

    out->x = combatant->x;
    out->y = combatant->y;
    out->z = combatant->z;

    /* Heading is atan2(vx, vz) and a look yaw of zero looks down -Z, so the two
       conventions are half a turn apart - see update_facing in
       character_animation.c, which is the code that reconciles them. Setting
       both off the one facing is what leaves the rig with no twist to apply. */
    out->heading_degrees = combatant->yaw_degrees;
    out->look_yaw_degrees = combatant->yaw_degrees - 180.0f;
    out->look_pitch_degrees = 0.0f;

    /* The reaction, in world space and on its own three fields. It used to be
       driven onto `lean`, which is the peek channel, because a rig with no hit
       clip had nowhere else to bend a spine - see the comment on the field. A
       mannequin does not peek, so the collision never showed here; the player,
       who does, never saw a flinch at all. */
    out->flinch = combatant->flinch;
    out->flinch_x = combatant->flinch_x;
    out->flinch_z = combatant->flinch_z;
    out->flinch_region = (unsigned char)combatant->flinch_region;

    out->dead = dead;
    out->events = events;
    /* A corpse is not standing on anything as far as the rig is concerned: the
       death clip is authored falling to the floor and a grounded body would
       have the locomotion roles competing with it. */
    out->grounded = (unsigned char)(!dead);
}
