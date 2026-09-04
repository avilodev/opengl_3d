#include "gameplay/player_loadout.h"

/* The class list. One row is one class, and adding a class is adding a row:
   nothing else in the build counts them, names them or decides what order they
   come in. The pause menu draws a button per row and the game arms whatever
   weapon the chosen row names. */
static const PlayerLoadout loadouts[] = {
    {"Katana", &WEAPON_SPEC_KATANA},
    {"Rifleman", &WEAPON_SPEC_ASSAULT_RIFLE}
};

int player_loadout_count(void) {
    return (int)(sizeof(loadouts) / sizeof(loadouts[0]));
}

const PlayerLoadout* player_loadout_get(int index) {
    if (index < 0 || index >= player_loadout_count()) index = 0;
    return &loadouts[index];
}
