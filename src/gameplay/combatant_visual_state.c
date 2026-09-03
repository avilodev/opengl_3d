#include "gameplay/combatant_visual_state.h"

#include <string.h>

void combatant_visual_state_fill(CharacterVisualState* out,
                                 const Combatant* combatant) {
    if (!out || !combatant) return;
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

    /* The flinch rides the lean channel: the spine bends and the feet stay
       planted, which is the shape of a hit reaction and is the only one a rig
       with no hit clip can give. See COMBAT_FLINCH_AMOUNT. */
    out->lean = combatant->flinch;

    out->grounded = 1;
}
