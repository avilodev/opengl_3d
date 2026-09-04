#include "gameplay/player_loadout.h"
#include "gameplay/weapon_registry.h"

/* The class list. One row is one class, and adding a class is adding a row:
   nothing else in the build counts them, names them or decides what order they
   come in. The pause menu draws a button per row and the game arms whatever
   weapon the chosen row names. */
static const struct { const char* name; const WeaponSpec* weapon; } rows[] = {
    {"Katana", &WEAPON_SPEC_KATANA},
    {"Rifleman", &WEAPON_SPEC_ASSAULT_RIFLE}
};

int player_loadout_count(void) {
    return (int)(sizeof(rows) / sizeof(rows[0]));
}

const PlayerLoadout* player_loadout_get(int index) {
    if (index < 0 || index >= player_loadout_count()) index = 0;

    /* The weapon comes back through the registry rather than as the pointer
       the row states.

       They are the same weapon, and in a release build they are the same
       pointer. In a tuning build they are not: the registry hands out a
       mutable copy that placement.json has been read over, and the literal in
       the row above is the untuned original. A player armed straight off the
       row would be holding the one weapon in the process that the tuning
       overlay had not reached - which looks exactly like the overlay not
       working, and is instead the loadout going around it. */
    static PlayerLoadout answer;
    answer.name = rows[index].name;
    int in_registry = weapon_registry_index_of(rows[index].weapon);
    answer.weapon = in_registry >= 0 ? weapon_registry_get(in_registry)
                                     : rows[index].weapon;
    return &answer;
}
