#include "gameplay/spawn_select.h"

#include <math.h>

/* Squared distance from a slot to the nearest live body, or INFINITY when
   there are none. Squared throughout: the comparison between two of these is
   the same either way and a square root per body per slot buys nothing. */
static float nearest_live_body(const ArenaSpawn* spawn, const Combatant* bodies,
                               int body_count) {
    float nearest = INFINITY;
    if (!bodies) return nearest;

    for (int i = 0; i < body_count; ++i) {
        const Combatant* body = &bodies[i];
        if (!body->active || !body->alive) continue;
        float dx = body->x - spawn->x;
        float dz = body->z - spawn->z;
        /* Ground distance only. A body on a roof fourteen metres up is directly
           over the slot and is exactly as much of a problem as one standing on
           it, which a three-dimensional distance would have called safe. */
        float distance = dx * dx + dz * dz;
        if (distance < nearest) nearest = distance;
    }
    return nearest;
}

int spawn_select(ArenaTeam team, const Combatant* bodies, int body_count,
                 ArenaSpawn* out) {
    if (!out) return 0;

    int count = 0;
    const ArenaSpawn* spawns = arena_team_spawns(team, &count);
    if (!spawns || count <= 0) return 0;

    int best = 0;
    float best_clearance = -1.0f;
    for (int i = 0; i < count; ++i) {
        float clearance = nearest_live_body(&spawns[i], bodies, body_count);
        /* Strictly greater, so a tie - which is what a world with no live
           bodies is, every slot at INFINITY - keeps the first slot. The map
           authored them in order and the first one is the front door. */
        if (clearance > best_clearance) {
            best_clearance = clearance;
            best = i;
        }
    }

    *out = spawns[best];
    return 1;
}
