#include "render/combatant_renderer.h"
#include "gameplay/combatant_visual_state.h"

#include <math.h>

/* Past this a body is not skinned. The arena's diagonal is a little over two
   hundred metres and every rig is eight thousand vertices, so a set of bodies
   scattered across the map is most of a million vertex transforms a frame spent
   on figures a handful of pixels tall. The clip clock and the pose both still
   run - see character_renderer_update, where only the skinning is behind this -
   so nothing pops when one comes back into range, and a body out of range still
   knows where its own hands are. */
#define COMBATANT_SKIN_RANGE 150.0f

/* How far behind the camera still counts as visible, as a cosine. Generous on
   purpose: the field of view widens with speed, a body is two and a half metres
   of geometry rather than the point this is measured at, and the cost of being
   wrong the tight way is a body that is not there. */
#define COMBATANT_SKIN_BEHIND (-0.35f)

void combatant_bodies_init(CombatantBodies* bodies, const CombatantSet* set,
                           int rig) {
    if (!bodies || !set) return;
    bodies->count = set->count > COMBATANT_MAX ? COMBATANT_MAX : set->count;
    for (int i = 0; i < bodies->count; ++i) {
        character_instance_init(&bodies->instances[i], rig);
        /* Everyone in the arena carries the issued rifle. Stated here rather
           than assumed by the renderer, which is the whole point of the field:
           the day a combatant carries something else it is this line that
           changes, and nothing downstream learns anything new. */
        bodies->instances[i].weapon = &WEAPON_SPEC_ASSAULT_RIFLE;
        combatant_visual_state_fill(&bodies->states[i], &set->items[i]);
        bodies->drawable[i] = 0;
    }
}

/* Whether this body is worth the eight thousand vertex transforms. */
static int worth_skinning(const Combatant* combatant,
                          float eye_x, float eye_y, float eye_z,
                          float look_x, float look_y, float look_z) {
    /* Measured to the chest rather than the feet, so a body directly below a
       player standing on a roof is not culled for being behind them. */
    float dx = combatant->x - eye_x;
    float dy = combatant->y + COMBAT_BODY_HEIGHT * 0.5f - eye_y;
    float dz = combatant->z - eye_z;
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    if (distance > COMBATANT_SKIN_RANGE) return 0;
    if (distance < 1.0e-3f) return 1;
    float facing = (dx * look_x + dy * look_y + dz * look_z) / distance;
    return facing > COMBATANT_SKIN_BEHIND;
}

void combatant_bodies_update(CombatantBodies* bodies, CharacterLibrary* library,
                             const CombatantSet* set,
                             float eye_x, float eye_y, float eye_z,
                             float look_x, float look_y, float look_z,
                             float delta_time) {
    if (!bodies || !library || !set) return;
    int count = bodies->count < set->count ? bodies->count : set->count;
    for (int i = 0; i < count; ++i) {
        const Combatant* combatant = &set->items[i];
        /* A body that is down is not drawn at all. The rig has no death clip
           and nothing to fall with, so the honest choice between a corpse
           standing at attention and no corpse is no corpse - and the raycast
           has already stopped resolving against it, so what is drawn and what
           is solid still agree. */
        if (!combatant->active || !combatant->alive) {
            bodies->drawable[i] = 0;
            continue;
        }
        combatant_visual_state_fill(&bodies->states[i], combatant);
        int visible = worth_skinning(combatant, eye_x, eye_y, eye_z,
                                     look_x, look_y, look_z);
        character_renderer_update(library, &bodies->instances[i],
                                  &bodies->states[i], delta_time, visible);
        bodies->drawable[i] = (unsigned char)visible;
    }
}

void combatant_bodies_draw(const CombatantBodies* bodies,
                           const CharacterLibrary* library) {
    if (!bodies || !library) return;
    for (int i = 0; i < bodies->count; ++i) {
        if (!bodies->drawable[i]) continue;
        character_renderer_draw(library, &bodies->instances[i],
                                &bodies->states[i]);
    }
}

void combatant_bodies_free(CombatantBodies* bodies) {
    if (!bodies) return;
    for (int i = 0; i < bodies->count; ++i)
        character_instance_free(&bodies->instances[i]);
    bodies->count = 0;
}
