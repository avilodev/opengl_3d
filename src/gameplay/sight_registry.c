#include "gameplay/sight_registry.h"
#include "gameplay/weapon_registry.h"

#include <stddef.h>

/* The list, expanded from the same file that declares the externs. There is no
   second place to add an optic, which is the whole point: see
   include/gameplay/sight_list.h. */
static const SightSpec* const registry[] = {
#define SIGHT(id) &SIGHT_##id,
#include "gameplay/sight_list.h"
#undef SIGHT
};

#define SIGHT_COUNT ((int)(sizeof(registry) / sizeof(registry[0])))

int sight_registry_count(void) {
    return SIGHT_COUNT;
}

const SightSpec* sight_registry_get(int index) {
    if (index < 0) index = 0;
    if (index >= SIGHT_COUNT) index = SIGHT_COUNT - 1;
    return registry[index];
}

int sight_registry_index_of(const SightSpec* sight) {
    if (!sight) return -1;
    for (int i = 0; i < SIGHT_COUNT; ++i)
        if (registry[i] == sight) return i;
    return -1;
}

const SightSpec* sight_registry_cycle(const WeaponSpec* spec, int step) {
    if (!spec) return NULL;
    /* Through the registry, because the weapon the caller is holding IS the
       registry's copy - weapon_registry_get hands it out - and writing to the
       const literal it was copied from would change nothing anybody can see.
       This is the same trap player_loadout_get documents from the other
       side. */
    int index = weapon_registry_index_of(spec);
    if (index < 0) return NULL;
    WeaponSpec* fitted = weapon_registry_mutable(index);
    /* A weapon with no gun half has no rail to clamp to. A sword is not a
       refusal to answer, it is an answer: there is nothing to put an optic
       on. */
    if (!fitted || !fitted->firearm) return NULL;

    /* A weapon with no optic starts at the first one rather than being
       refused: index_of answers -1 for NULL, and -1 + 1 is 0. */
    FirearmSpec* gun = (FirearmSpec*)fitted->firearm;
    int at = sight_registry_index_of(gun->sight) + step;
    /* Wrapped both ways, so the key can walk backwards later without this
       needing to learn how. C's % keeps the sign of the dividend, which for a
       backwards step would land on a negative index. */
    at %= SIGHT_COUNT;
    if (at < 0) at += SIGHT_COUNT;
    gun->sight = registry[at];
    return gun->sight;
}
