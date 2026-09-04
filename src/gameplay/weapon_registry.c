#include "gameplay/weapon_registry.h"

#include <string.h>

/* The list, expanded from the same file that declares the externs. There is no
   second place to add a weapon, which is the whole point: see
   include/gameplay/weapon_list.h. */
static const WeaponSpec* const registry[] = {
#define WEAPON(id) &WEAPON_SPEC_##id,
#include "gameplay/weapon_list.h"
#undef WEAPON
};

int weapon_registry_count(void) {
    return (int)(sizeof(registry) / sizeof(registry[0]));
}

const WeaponSpec* weapon_registry_get(int index) {
    int count = weapon_registry_count();
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    return registry[index];
}

const WeaponSpec* weapon_registry_find(const char* name) {
    if (!name) return NULL;
    for (int i = 0; i < weapon_registry_count(); ++i)
        if (strcmp(registry[i]->name, name) == 0) return registry[i];
    return NULL;
}

int weapon_registry_index_of(const WeaponSpec* spec) {
    for (int i = 0; i < weapon_registry_count(); ++i)
        if (registry[i] == spec) return i;
    return -1;
}
