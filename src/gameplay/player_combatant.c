#include "gameplay/player_combatant.h"
#include "gameplay/combat_tuning.h"

void player_combatant_init(Combatant* out, const PlayerController* player) {
    if (!out || !player) return;

    out->max_health = COMBAT_PLAYER_HEALTH;
    out->active = 1;
    /* The same call every later respawn makes, so entering the map and
       re-entering it leave the body in identical states. */
    combatant_respawn(out, player->x, player->y, player->z,
                      player->heading_degrees);
}

void player_combatant_fill(Combatant* out, const PlayerController* player) {
    if (!out || !player || !out->alive) return;

    /* Feet, in world units - which is what the controller's position already
       is, and what CharacterVisualState and the hitbox both mean by it. */
    out->x = player->x;
    out->y = player->y;
    out->z = player->z;
    out->yaw_degrees = player->heading_degrees;
}
