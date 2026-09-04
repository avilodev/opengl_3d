#ifndef PLAYER_LOADOUT_H
#define PLAYER_LOADOUT_H

#include "gameplay/weapon_spec.h"

/* A class, as far as anything outside this module is concerned: a name to put
   on a button and the weapon the player walks out holding. Stats, abilities and
   the rest of what a class will eventually mean are not here yet, and the point
   of the struct is that they can arrive without every caller learning about
   them. */
typedef struct {
    const char* name;
    const WeaponSpec* weapon;
} PlayerLoadout;

int player_loadout_count(void);
/* Always answers a loadout - an index off the end is clamped to the first, so a
   caller holding a stale selection gets a playable class rather than a crash. */
const PlayerLoadout* player_loadout_get(int index);

#endif
